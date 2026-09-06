// istate — the hub core (REQ §7.1). Owns system state, provisioning,
// persistent configuration, orchestration, and every external interface
// (R2: the only process with an external interface).
#pragma once
#include <memory>
#include <mutex>

#include "instr/common/log.h"
#include "instr/istate/config_service.h"
#include "instr/istate/northbound.h"
#include "instr/istate/orchestrator.h"
#include <future>

#include "instr/transport/transport.h"
#include "instr/istate/provisioning.h"
#include "instr/istate/pst.h"
#include "instr/platform/platform.h"

namespace instr {
namespace istate {

struct IstateOptions {
  std::string pst_root;             // /pst (or test root)
  std::string run_dir;              // /run/instrumentation
  std::string build_version;
  double liveness_hang_timeout_s = 30;
  uint64_t spool_max_bytes = 64ull * 1024 * 1024;
  double budget_spool = 64ull * 1024 * 1024;
  double budget_logs = 128ull * 1024 * 1024;
  double budget_ota = 512ull * 1024 * 1024;
  double budget_backup = 256ull * 1024 * 1024;
};

class IstateCore {
 public:
  IstateCore(IstateOptions opts, IFileSystem& fs, platform::IPlatform& plat, TimeSource& ts,
             Logger& log);

  // ---- lifecycle ----
  // Mount /pst, validate, seed defaults on virgin; provisioning; start the
  // spokes in order. Returns false only when the platform is unusable.
  bool boot(const std::map<std::string, std::string>& default_yaml);
  void shutdown();

  // ---- subsystems (wired into the hub server) ----
  PstStore& pst() { return pst_; }
  BudgetManager& budgets() { return budgets_; }
  Provisioner& provisioner() { return provisioner_; }
  Orchestrator& orchestrator() { return orchestrator_; }
  EventTimeline& timeline() { return timeline_; }
  TailFanout& tail_fanout() { return tail_fanout_; }
  NorthboundSpool& northbound_spool() { return northbound_spool_; }
  HqSession& hq_session() { return hq_session_; }
  ConfigService& config_service() { return config_service_; }
  AuditLog& audit() { return audit_; }
  Logger& log() { return log_; }
  TimeSource& clock() { return ts_; }
  platform::IPlatform& platform() { return plat_; }
  const IstateOptions& options() const { return opts_; }

  // ---- time policy (istate owns policy; networkd applies, §R8) ----
  struct TimePolicyState {
    std::vector<std::string> source_priority = {"gnss", "ptp", "ntp"};
    std::string current_source = "unsynced";
    double offset_ms = 0;
    double lock_quality = 0;
    bool locked = false;
  };
  TimePolicyState& time_policy() { return time_policy_; }
  // Called when a time source acquires lock: re-stamp spooled data (§4).
  size_t on_time_lock(const std::string& source, int64_t offset_ns);
  TimeReconciler& reconciler() { return reconciler_; }

  // ---- northbound ingest from spokes ----
  // Each spoke's typed frames arrive as GenericSpokeMsg; istate routes them:
  // telemetry → northbound spool (SPOOL_TELEMETRY), faults → protected,
  // alarms → protected + timeline, samples → relay to telemetry, logs → tail
  // fanout + spool refs, ota/backup events → timeline.
  void on_spoke_msg(const transport::GenericSpokeMsg& msg);

  // Spoke command relay (Configuration → spoke, §3.3): correlated acks.
  struct PendingRelay {
    std::promise<std::string> promise;
  };
  std::future<std::string> relay_command(const std::string& daemon, const common::Command& cmd);
  void on_spoke_ack(const std::string& daemon, const common::Ack& ack);

  // HQ session client hooks (implemented in hubs.cpp against HqPeer stub).
  void set_hq_send(std::function<bool(const std::string& type, const std::string& json)> f) {
    hq_send_ = std::move(f);
  }
  // Wiring: forward hub messages to a spoke's session (per-daemon sessions
  // register themselves with the core).
  using RelayFn = std::function<bool(const std::string& daemon, const transport::GenericHubMsg&)>;
  void set_relay_fn(RelayFn f) { relay_to_spoke_ = std::move(f); }
  // Image default YAML provider (used for seeding + system reset).
  void set_default_provider(std::function<std::map<std::string, std::string>()> f) {
    default_provider_ = std::move(f);
  }
  // Enrollment secret (factory-provisioned) for signature verification.
  void set_enrollment_secret(std::string s) { enrollment_secret_ = std::move(s); }
  const std::string& enrollment_secret() const { return enrollment_secret_; }
  void set_hq_backpressure(bool pause);
  bool hq_backpressured() const { return hq_backpressured_; }
  // Drain the northbound spool toward HQ (called when connected).
  NorthboundSpool::DrainReport drain_northbound(size_t max_batch = 50);

  // ---- system info (Configuration API) ----
  std::string system_info_json() const;
  std::string budgets_json() const;

  // ---- reset (§6.1, §3.3 Configuration API) ----
  Result<bool> system_reset(const std::string& actor);
  Result<bool> factory_reset(const std::string& actor, const std::string& confirmation);

  // Unclean shutdown detection (§11): clean-shutdown marker.
  void write_clean_shutdown_marker();
  bool last_shutdown_clean() const;

  void push_event_to_spool(const std::string& payload_json) {
    northbound_spool_.enqueue(SpoolPriority::Event, payload_json, ts_.mono_ns());
  }

 private:
  IstateOptions opts_;
  IFileSystem& fs_;
  platform::IPlatform& plat_;
  TimeSource& ts_;
  Logger& log_;

  BudgetManager budgets_;
  PstStore pst_;
  Provisioner provisioner_;
  Orchestrator orchestrator_;
  EventTimeline timeline_;
  TailFanout tail_fanout_;
  NorthboundSpool northbound_spool_;
  HqSession hq_session_;
  ConfigService config_service_;
  AuditLog audit_;
  TimeReconciler reconciler_;
  TimePolicyState time_policy_;

  std::map<uint64_t, std::shared_ptr<PendingRelay>> pending_relays_;
  std::mutex relay_mu_;
  uint64_t next_relay_id_ = 1;
  std::atomic<bool> hq_backpressured_{false};
  std::function<bool(const std::string&, const std::string&)> hq_send_;
  std::string enrollment_secret_;
  RelayFn relay_to_spoke_;
  std::function<std::map<std::string, std::string>()> default_provider_;
};

}  // namespace istate
}  // namespace instr