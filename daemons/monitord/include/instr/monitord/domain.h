// monitord — system resource monitoring domain (REQ §7.3).
// Observation only; raises no alarms. Pure domain logic: no sockets, no
// direct hardware — platform access via IPlatform (testable, simulated).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "instr/common/timestamps.h"
#include "instr/platform/platform.h"

namespace instr {
namespace monitord {
namespace domain {

struct MonitorSample {
  std::string metric;
  double value = 0;
  std::string unit;
  std::map<std::string, std::string> tags;
  std::string resource_class;  // cpu / memory / storage / thermal / load / system
  uint64_t mono_ns = 0;
};

// Per-tick sampler: computes rate deltas (CPU%), gauges, and wear indicators.
class Sampler {
 public:
  Sampler(platform::IPlatform& plat, TimeSource& ts) : plat_(plat), ts_(ts) {}

  // One sampling pass. First tick after construction yields no CPU util
  // (deltas need two snapshots) but yields gauges.
  std::vector<MonitorSample> tick();

  void set_default_interval_s(double s) { default_interval_ = s; }
  void set_interval_for(const std::string& resource_class, double s) {
    intervals_[resource_class] = s;
  }
  double interval_for(const std::string& resource_class) const;

  struct Stats {
    uint64_t produced = 0;
    uint64_t dropped = 0;
  };
  const Stats& stats() const { return stats_; }

 private:
  platform::IPlatform& plat_;
  TimeSource& ts_;
  platform::CpuSnapshot prev_cpu_;
  bool have_prev_cpu_ = false;
  double default_interval_ = 5.0;
  std::map<std::string, double> intervals_;
  Stats stats_;
};

// High-resolution rolling window for thermal and power retained locally at
// finer granularity than telemetry ships (REQ §7.3).
class RollingWindow {
 public:
  RollingWindow(double window_s, double interval_s)
      : window_ns_(uint64_t(window_s * 1e9)), interval_ns_(uint64_t(interval_s * 1e9)) {}

  void push(uint64_t mono_ns, const MonitorSample& s);
  // Samples within the window, oldest first.
  std::vector<MonitorSample> window() const;
  size_t size() const { return buf_.size(); }
  void clear() { buf_.clear(); }

 private:
  struct Entry {
    uint64_t mono_ns;
    MonitorSample sample;
  };
  std::deque<Entry> buf_;
  uint64_t window_ns_;
  uint64_t interval_ns_;
};

// Predictive storage wear: project remaining life from wear indicators
// (REQ §7.3). Linear model over measured wear rate; falls back to a
// conservative 10-year estimate when the indicator is unknown.
class WearProjector {
 public:
  struct Projection {
    std::string device;
    double wear_pct = 0;
    uint32_t remaining_days = 0;
    std::string model;
  };
  static Projection project(const platform::StorageDevice& d, uint64_t age_days);
};

}  // namespace domain
}  // namespace monitord
}  // namespace instr