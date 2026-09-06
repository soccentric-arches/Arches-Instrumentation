// ============================================================================
//  Arches Instrumentation
//
//  Copyright (C) 2026 Soccentric. All rights reserved.
//  Author: Sandesh Ghimire (sandesh@soccentric)
//
//  This file is part of a proprietary product of Soccentric.
//
//  NO WARRANTY:
//  This software is provided "AS IS", without warranty of any kind, either
//  express or implied, including but not limited to the warranties of
//  merchantability, fitness for a particular purpose, and non-infringement.
//  In no event shall Soccentric be liable for any claim, damages, or other
//  liability arising from the use of this software.
//
//  This software may not be used, copied, modified, distributed, or
//  disclosed, in whole or in part, without the express prior written
//  consent of Soccentric.
// ============================================================================
/**
 * @file daemons/monitord/include/instr/monitord/transport.h
 * @brief domain+state facade wired into the command table. The core owns
 *
 * the sampler, the high-res window, and the schedule; everything reachable
 * from _ctrl is reachable here, and everything here is dispatchable from
 * istate commands too (one table, two surfaces, §3.2).
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <atomic>
#include <functional>
#include <mutex>

#include "instr/common/config_sink.h"
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

  // Configuration delivered by istate (§7.3, §9). monitord observes only —
  // the thresholds here are reported, never turned into alarms (R5).
  struct Config {
    bool enabled = true;
    double interval = 5.0;
    std::map<std::string, double> intervals_per_class;
    double thermal_warn_c = 75.0;
    double thermal_critical_c = 85.0;
    double storage_warn_pct = 90.0;
    uint32_t wear_projection_threshold_days = 180;
    double highres_window_s = 60.0;
    double highres_interval_s = 1.0;
  };
  const Config& config() const { return cfg_; }
  Error apply_config(const ConfigDocument& doc);
  void set_config_listener(std::function<void(const Config&)> f) { on_config_ = std::move(f); }
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
  Config cfg_;
  std::function<void(const Config&)> on_config_;
};

}  // namespace monitord
}  // namespace instr