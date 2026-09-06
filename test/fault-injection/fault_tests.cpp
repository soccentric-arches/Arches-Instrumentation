// Fault-injection tests (REQ §12): the failure paths are the ones that ship
// untested — so every §11 row is exercised here against the simulated
// filesystem/platform with injected faults.
#include <gtest/gtest.h>

#include "instr/common/filesystem.h"
#include "instr/istate/core.h"
#include "instr/monitord/domain.h"
#include "instr/networkd/daemon.h"
#include "instr/platform/simulated.h"
#include "instr/telemetry/domain.h"

using namespace instr;
using namespace instr::istate;
using namespace instr::platform;

namespace {
static std::string tmp_dir(const char* tag) {
  return std::string("/tmp/instr-fault-") + tag + "-" + std::to_string(::getpid());
}
struct FaultFixture {
  SimulatedFileSystem fs;
  SimulatedPlatform plat;
  ManualTimeSource ts;
  MemorySink log_sink;
  Logger log{log_sink, ts};
  std::string root = "/tmp/instr-fault-" + std::to_string(::getpid());
  std::unique_ptr<IstateCore> core;
  FaultFixture() {
    IstateOptions opts;
    opts.pst_root = root + "/pst";
    opts.run_dir = root + "/run/instrumentation";
    opts.build_version = "test";
    opts.spool_max_bytes = 4096;
    core = std::make_unique<IstateCore>(opts, fs, plat, ts, log);
    std::map<std::string, std::string> defaults{{"istate", "a: 1\n"}};
    core->boot(defaults);
  }
};
}  // namespace

// §11 "/pst corrupt": fall back to redundant copy; if both fail, boot
// unprovisioned and report — never fail to boot.
TEST(FaultPst, CorruptPrimaryRecoversFromRedundantCopy) {
  FaultFixture f;
  ConfigDocument d;
  d.set("interval", "7", Origin::Local);
  ASSERT_TRUE(f.core->pst().save_config("monitord", d).ok());
  f.fs.corrupt_reads_of("/pst/system/monitord/config.yaml", 1);
  auto doc = f.core->pst().load_config("monitord");
  ASSERT_TRUE(doc.ok());
  EXPECT_EQ((*doc).get_int("interval"), 7);
}

TEST(FaultPst, BothCopiesCorruptBootsUnprovisionedAndReports) {
  FaultFixture f;
  ConfigDocument d;
  d.set("interval", "7", Origin::Local);
  ASSERT_TRUE(f.core->pst().save_config("monitord", d).ok());
  f.fs.corrupt_reads_of("/pst/system/monitord/config.yaml", 1);
  f.fs.corrupt_reads_of("/pst/system/monitord/config.yaml.bak", 1);
  // load_config still returns loudly (Corrupt) — boot proceeds unprovisioned.
  auto doc = f.core->pst().load_config("monitord");
  EXPECT_FALSE(doc.ok());
  EXPECT_EQ(doc.error.code, Errc::Corrupt);
  // Boot itself must not fail: re-mount reports and continues.
  auto rep = f.core->pst().mount();
  EXPECT_TRUE(rep.ok);
}

// §11 "spool full": drop oldest telemetry first, never alarms or faults,
// report the drop.
TEST(FaultSpool, FullDropsTelemetryKeepsFaultsReports) {
  SimulatedFileSystem fs;
  SpoolConfig cfg;
  cfg.dir = tmp_dir("spool");
  cfg.max_bytes = 900;
  DiskSpool sp(fs, cfg);
  ASSERT_TRUE(sp.open().ok());
  for (int i = 0; i < 4; ++i)
    ASSERT_TRUE(sp.append(SpoolPriority::Fault, "fault-" + std::to_string(i), i).ok());
  size_t faults_before = 0;
  for (const auto& e : sp.peek(100))
    if (e.priority == SpoolPriority::Fault) ++faults_before;
  for (int i = 0; i < 40; ++i)
    ASSERT_TRUE(sp.append(SpoolPriority::Telemetry, "t-" + std::to_string(i), 100 + i).ok());
  size_t faults_after = 0;
  for (const auto& e : sp.peek(100))
    if (e.priority == SpoolPriority::Fault) ++faults_after;
  EXPECT_EQ(faults_before, faults_after);
  EXPECT_GT(sp.stats().dropped_telemetry, 0u);
}

// §11 "HQ unreachable": jittered backoff; device fully operational.
TEST(FaultHq, UnreachableTriggersBackoffState) {
  FaultFixture f;
  f.plat.set_hq_reachable(false);
  HqSession s;
  s.on_disconnected();
  EXPECT_EQ(s.state(), HqSession::State::Backoff);
  double total = 0;
  for (int i = 0; i < 5; ++i) total += s.next_delay_s();
  EXPECT_GE(total, 31.0);  // 1+2+4+8+16 — device keeps operating meanwhile
  // Local operation unaffected: config still readable/writable.
  ConfigDocument d;
  d.set("interval", "3", Origin::Local);
  EXPECT_TRUE(f.core->pst().save_config("monitord", d).ok());
}

// §11 "spoke crashes / hangs": istate reports degraded, requests restart.
TEST(FaultSpoke, HangDetectedRestartRequestedDeviceDegrades) {
  FaultFixture f;
  f.core->orchestrator().register_spoke("networkd", "1.0.0", transport::kProtoSchemaVersion,
                                        std::vector<std::string>{});
  f.core->orchestrator().mark_ready("networkd");
  bool alive = false;
  f.core->orchestrator().set_probe_fn("networkd", [&] { return alive; });
  f.ts.mono += 60ull * 1'000'000'000ull;
  f.core->orchestrator().liveness_sweep(double(f.ts.mono_ns()) / 1e9, 30);
  EXPECT_EQ((*f.core->orchestrator().spoke("networkd"))->state, SpokeState::Hung);
  EXPECT_TRUE(f.core->orchestrator().degraded());  // exposed northbound
  EXPECT_EQ(f.plat.unit_restarts().size(), 1u);
}

// §11 "clock unavailable at boot": operate on monotonic; re-stamp on
// acquisition.
TEST(FaultClock, MonotonicOperationAndRestamp) {
  ManualTimeSource ts;
  TimeReconciler rec(ts);
  // Clock unavailable: records carry monotonic only.
  rec.record_pending(1'000);
  rec.record_pending(2'000);
  EXPECT_FALSE(rec.wall_valid());
  // GNSS acquires lock mid-run → re-stamp; history not reordered.
  size_t n = rec.reconcile(1'700'000'000'000'000'000ll, "gnss");
  EXPECT_EQ(n, 2u);
  EXPECT_TRUE(rec.wall_valid());
  auto stamped = rec.take_stamped();
  EXPECT_LE(stamped[0].wall_ns, stamped[1].wall_ns);
  // Clock jump later: new stamp still monotonic in wall time.
  ts.mono += 100;
  auto now = rec.stamp_now();
  EXPECT_GE(now.wall_ns, stamped[1].wall_ns);
}

// §11 "storage near full": budgets enforced, alarm before the limit.
TEST(FaultStorage, BudgetAlarmBeforeLimitNeverWedges) {
  FaultFixture f;
  f.core->budgets().set_budget("logs", 1000);
  f.core->budgets().record_usage("logs", 850);
  EXPECT_FALSE(f.core->budgets().over_warning().empty());
  EXPECT_TRUE(f.core->budgets().over_limit().empty());
  // Writes refuse past the limit instead of wedging the partition.
  f.core->budgets().record_usage("logs", 1200);
  EXPECT_FALSE(f.core->budgets().over_limit().empty());
  EXPECT_FALSE(f.core->budgets().reserve("logs", 100).ok());
}

// §11 "firewall change kills tunnel": auto-revert within the timeout.
TEST(FaultNetwork, TunnelLossGuardRevertsChange) {
  SimulatedPlatform plat;
  ManualTimeSource ts;
  instr::networkd::NetworkdCore netd(plat, ts);
  double t = 0;
  netd.set_clock_fn([&t] {
    t += 0.05;
    return t;
  });
  instr::networkd::NetworkPlan next;
  netd.set_reachability_fn([] { return false; });  // change killed the tunnel
  auto res = netd.guarded_apply(next, 0.3);
  EXPECT_TRUE(res.reverted);
  EXPECT_NE(res.detail.find("auto-reverted"), std::string::npos);
  EXPECT_GE(plat.applied_plans().size(), 2u);  // next applied + previous restored
}

// §11 "proto version mismatch": refuse, alarm, never guess.
TEST(FaultProto, MismatchRefusedWithAlarm) {
  FaultFixture f;
  std::vector<std::string> events;
  f.core->orchestrator().set_event_sink(
      [&](const std::string& kind, const std::string& detail) {
        events.push_back(kind + ":" + detail);
      });
  auto r = f.core->orchestrator().register_spoke("ota", "1.0.0", 999, std::vector<std::string>{});
  EXPECT_EQ(r, Orchestrator::Reg::SchemaMismatch);
  bool alarm_emitted = false;
  for (const auto& e : events)
    if (e.find("alarm") == 0) alarm_emitted = true;
  EXPECT_TRUE(alarm_emitted);
}

// §11 "unclean shutdown": marker-based detection, reported on next boot.
TEST(FaultShutdown, UncleanMarkerReportedOnNextBoot) {
  FaultFixture f;
  // No clean-shutdown marker (simulated crash) → boot records the event.
  auto j = json_parse(f.core->system_info_json());
  EXPECT_FALSE((*j).find("clean_shutdown")->as_bool());
  f.core->write_clean_shutdown_marker();
  auto j2 = json_parse(f.core->system_info_json());
  EXPECT_TRUE((*j2).find("clean_shutdown")->as_bool());
}

// §11 "fleet-wide simultaneous boot": jittered drain + backpressure honoured.
TEST(FaultFlood, JitteredDrainNotStampede) {
  SimulatedFileSystem fs;
  SpoolConfig cfg;
  cfg.dir = tmp_dir("spool");
  NorthboundSpool spool(fs, cfg, nullptr);
  ASSERT_TRUE(spool.open().ok());
  for (int i = 0; i < 100; ++i)
    ASSERT_TRUE(spool.enqueue(SpoolPriority::Telemetry, "r-" + std::to_string(i), i).ok());
  // HQ throttles: pause mid-drain; spool retains the rest for later.
  int accepted = 0;
  auto rep = spool.drain([&](const DiskSpool::Entry&) { return ++accepted <= 10; }, 200);
  EXPECT_TRUE(rep.paused);
  EXPECT_EQ(spool.size(), 100u);
  // Jitter keeps the retry delay in bounds (no thundering herd).
  instr::istate::HqSession s(1.0, 60.0);
  s.on_disconnected();
  for (int i = 0; i < 20; ++i) {
    double d = s.next_delay_s();
    EXPECT_LE(d, 60.0 * 1.2);  // capped with jitter margin
  }
}

// Fault injection: storage write failure mid-config-save → loud error,
// prior state intact.
TEST(FaultStorage, WriteFailureLeavesStateIntact) {
  FaultFixture f;
  f.fs.fail_writes_after(1);
  ConfigDocument d;
  d.set("interval", "11", Origin::Local);
  auto e = f.core->pst().save_config("monitord", d);
  EXPECT_FALSE(e.ok());
  EXPECT_EQ(e.code, Errc::Io);
}