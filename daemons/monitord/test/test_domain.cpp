// Unit tests — monitord domain (REQ §12).
#include <gtest/gtest.h>

#include "instr/common/log.h"
#include "instr/common/timestamps.h"
#include "instr/monitord/domain.h"
#include "instr/platform/simulated.h"

using namespace instr;
using namespace instr::monitord;
using namespace instr::monitord::domain;

namespace {
class FakeTs : public TimeSource {
 public:
  uint64_t mono = 1'000'000'000;
  uint64_t mono_ns() override { return mono; }
  int64_t wall_ns() override { return 1'700'000'000'000'000'000; }
  std::string boot_id() override { return "boot-x"; }
  uint64_t next_seq() override { return ++seq; }
  uint64_t seq = 0;
};
}  // namespace

TEST(MonitordSampler, FirstTickYieldsGaugesButNoCpuUtil) {
  platform::SimulatedPlatform plat;
  plat.set_cpu([]() {
    platform::CpuSnapshot c;
    c.cores = 2;
    c.busy_ticks = {100, 100};
    c.total_ticks = {200, 200};
    c.freq_mhz = {1500, 1500};
    c.governor = "performance";
    return c;
  }());
  plat.set_mem([]() {
    platform::MemInfo m;
    m.total_bytes = 8ull << 30;
    m.available_bytes = 4ull << 30;
    return m;
  }());
  plat.set_thermal("zone0", "cpu", 55.0);

  FakeTs ts;
  Sampler s(plat, ts);
  auto samples = s.tick();
  // No CPU util (need deltas) but gauges present.
  bool has_util = false, has_thermal = false, has_mem = false;
  for (const auto& sm : samples) {
    if (sm.metric == "cpu.util") has_util = true;
    if (sm.metric == "thermal.zone0") has_thermal = true;
    if (sm.metric == "mem.used_pct") has_mem = true;
  }
  EXPECT_FALSE(has_util);
  EXPECT_TRUE(has_thermal);
  EXPECT_TRUE(has_mem);
}

TEST(MonitordSampler, CpuUtilDeltaBetweenTicks) {
  platform::SimulatedPlatform plat;
  FakeTs ts;
  Sampler s(plat, ts);
  platform::CpuSnapshot c1;
  c1.cores = 1;
  c1.busy_ticks = {100};
  c1.total_ticks = {200};
  plat.set_cpu(c1);
  s.tick();
  platform::CpuSnapshot c2;
  c2.cores = 1;
  c2.busy_ticks = {150};   // +50 busy
  c2.total_ticks = {300};  // +100 total
  plat.set_cpu(c2);
  auto samples = s.tick();
  double util = -1;
  for (const auto& sm : samples)
    if (sm.metric == "cpu.util") util = sm.value;
  EXPECT_NEAR(util, 50.0, 0.001);
}

TEST(MonitordSampler, PerClassIntervals) {
  platform::SimulatedPlatform plat;
  FakeTs ts;
  Sampler s(plat, ts);
  s.set_default_interval_s(5);
  s.set_interval_for("thermal", 1);
  EXPECT_DOUBLE_EQ(s.interval_for("thermal"), 1.0);
  EXPECT_DOUBLE_EQ(s.interval_for("memory"), 5.0);
}

TEST(MonitordRollingWindow, KeepsOnlyWindowSize) {
  RollingWindow w(60.0, 1.0);
  // Push samples 1 second apart for 70 seconds → only last ~60 retained.
  for (uint64_t t = 0; t < 70; ++t) {
    MonitorSample s;
    s.metric = "thermal.z0";
    s.value = double(t);
    w.push(t * 1'000'000'000ull, s);
  }
  EXPECT_LE(w.size(), size_t(61));
  auto win = w.window();
  // Oldest retained sample must be >= window start.
  EXPECT_GE(win.front().value, 9.0);  // t=9 at ns 9e9; window = 60s
}

TEST(MonitordWear, LinearProjection) {
  platform::StorageDevice d;
  d.name = "mmcblk0";
  d.wear_pct = 10;  // 10% wear
  auto p = WearProjector::project(d, 365);  // 10%/365d rate
  EXPECT_EQ(p.model, "linear-wear-rate");
  EXPECT_EQ(p.remaining_days, 3285u);  // 90%/ (10/365)
}

TEST(MonitordWear, NoIndicatorFallsBackConservatively) {
  platform::StorageDevice d;
  d.name = "sd0";
  d.wear_pct = -1;
  auto p = WearProjector::project(d, 100);
  EXPECT_EQ(p.model, "no-indicator-fallback");
  EXPECT_EQ(p.remaining_days, 3650u);
}

TEST(MonitordWear, ZeroRateFloor) {
  platform::StorageDevice d;
  d.name = "emmc";
  d.wear_pct = 0;
  auto p = WearProjector::project(d, 500);
  EXPECT_EQ(p.model, "zero-rate");
}

// Domain must be constructible without any real platform: the simulated
// implementation is a plain class with no sockets/files (§12 design rule).
TEST(MonitordDesign, DomainWorksWithSimulatedPlatformOnly) {
  platform::SimulatedPlatform plat;
  FakeTs ts;
  Sampler s(plat, ts);
  EXPECT_NO_FATAL_FAILURE(s.tick());
}