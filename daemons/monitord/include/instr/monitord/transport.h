// monitord — domain+state facade wired into the command table. The core owns
// the sampler, the high-res window, and the schedule; everything reachable
// from _ctrl is reachable here, and everything here is dispatchable from
// istate commands too (one table, two surfaces, §3.2).
#pragma once
#include <atomic>
#include <functional>
#include <mutex>

#include "instr/monitord/domain.h"
#include "instr/platform/descriptors.h"
#include "instr/transport/transport.h"

namespace instr {
namespace monitord {

using namespace instr::monitord::domain;

class MonitorCore {
 public:
  MonitorCore(platform::IPlatform& plat, TimeSource& ts)
      : plat_(plat), sampler_(plat, ts), window_(60.0, 1.0), ts_(ts) {}

  void configure(const platform::ResourceProfile& profile);
  // One sampling pass: samples → window + emit callback (spoke enqueue).
  std::vector<MonitorSample> sample_once();
  // Storage wear projections for all devices.
  std::vector<WearProjector::Projection> wear_projections(uint64_t age_days) const;

  const RollingWindow& window() const { return window_; }
  Sampler& sampler() { return sampler_; }
  std::string status_json() const;

  void set_emit(std::function<void(const MonitorSample&)> f) {
    std::lock_guard<std::mutex> lk(mu_);
    emit_ = std::move(f);
  }

 private:
  platform::IPlatform& plat_;
  Sampler sampler_;
  RollingWindow window_;
  TimeSource& ts_;
  std::function<void(const MonitorSample&)> emit_;
  std::mutex mu_;
};

}  // namespace monitord
}  // namespace instr