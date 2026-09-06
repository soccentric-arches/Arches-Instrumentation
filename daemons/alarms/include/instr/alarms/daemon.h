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
 * @file daemons/alarms/include/instr/alarms/daemon.h
 * @brief alarms daemon facade: core + command table + spoke
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <memory>

#include "instr/alarms/domain.h"
#include "instr/common/command_table.h"
#include "instr/common/config_sink.h"
#include "instr/common/periodic.h"
#include "instr/transport/transport.h"
#include "alarms.grpc.pb.h"

namespace instr {
namespace alarms {
using namespace domain;
using namespace instr::transport;

class AlarmsCore {
 public:
  AlarmsCore(IFileSystem& fs, const std::string& db_path, TimeSource& ts)
      : mgr_(fs, db_path, ts), fs_(fs), ts_(ts) {}

  // Configuration delivered by istate (§7.6, §9).
  struct Config {
    bool enabled = true;
    uint32_t escalation_default_promote_after_s = 3600;
    double escalation_sweep_interval_s = 30.0;
    bool snapshot_on_critical = true;
  };
  const Config& config() const { return cfg_; }
  Error apply_config(const ConfigDocument& doc) {
    Config c;
    c.enabled = doc.get_bool("enabled", true);
    c.escalation_default_promote_after_s =
        uint32_t(doc.get_int("escalation_default_promote_after_s", 3600));
    c.escalation_sweep_interval_s = doc.get_double("escalation_sweep_interval_s", 30.0);
    c.snapshot_on_critical = doc.get_bool("snapshot_on_critical", true);
    if (c.escalation_sweep_interval_s <= 0)
      return Error::make(Errc::InvalidArgument, "escalation_sweep_interval_s must be > 0",
                         "alarms config");
    cfg_ = c;
    mgr_.set_snapshot_on_critical(c.snapshot_on_critical);
    if (on_config_) on_config_(cfg_);
    return Error::none();
  }
  void set_config_listener(std::function<void(const Config&)> f) { on_config_ = std::move(f); }

  AlarmManager& manager() { return mgr_; }
  uint64_t now_mono() const { return ts_.mono_ns(); }
  void set_spoke_sink(std::function<void(const AlarmStateEvent&)> f) {
    spoke_sink_ = std::move(f);
    mgr_.set_event_sink([this](const AlarmStateEvent& ev) {
      if (spoke_sink_) spoke_sink_(ev);
    });
  }

 private:
  AlarmManager mgr_;
  IFileSystem& fs_;
  TimeSource& ts_;
  std::function<void(const AlarmStateEvent&)> spoke_sink_;
  Config cfg_;
  std::function<void(const Config&)> on_config_;
};

CommandTable build_alarms_table(AlarmsCore* core);

class AlarmsSpoke {
 public:
  AlarmsSpoke(AlarmsCore* core, Logger* log, const CommandTable* table,
              const std::string& hub_target, const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  // Configuration is delivered by istate over the channel and validated
  // against this schema before it can change behaviour (R4, §9).
  void set_schema(JValue schema);
  uint64_t config_revision() const { return channel_->config_revision(); }
  void send_event(const AlarmStateEvent& ev);

 private:
  AlarmsCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<ConfigSink> config_sink_;
  std::unique_ptr<
      SpokeChannel<alarms::AlarmHub, alarms::AlarmSpokeFrame, alarms::AlarmHubFrame>>
      channel_;
};

}  // namespace alarms
}  // namespace instr