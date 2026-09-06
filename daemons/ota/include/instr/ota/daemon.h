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
 * @file daemons/ota/include/instr/ota/daemon.h
 * @brief ota daemon facade: core + command table + spoke
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <memory>

#include "instr/common/command_table.h"
#include "instr/common/config_sink.h"
#include "instr/common/periodic.h"
#include "instr/ota/domain.h"
#include "instr/transport/transport.h"
#include "ota.grpc.pb.h"

namespace instr {
namespace ota {
using namespace domain;
using namespace instr::transport;

class OtaCore {
 public:
  OtaCore(IFileSystem& fs, platform::IPlatform& plat, TimeSource& ts, const OtaConfig& cfg)
      : engine_(fs, plat, ts, cfg) {}

  // Configuration delivered by istate (§7.9, §9).
  Error apply_config(const ConfigDocument& doc) {
    OtaConfig c;
    c.min_version = doc.get_string("min_version", "0.0.0");
    c.confirm_timeout_s = doc.get_double("confirm_timeout_s", 600.0);
    c.bandwidth_limit_bytes_per_s =
        uint64_t(doc.get_int("bandwidth_limit_bytes_per_s", 0));
    for (size_t i = 0;; ++i) {
      const std::string b = "maintenance_windows." + std::to_string(i) + ".";
      if (!doc.has(b + "day")) break;
      OtaConfig::Window w;
      w.day = doc.get_string(b + "day", "*");
      w.start = doc.get_string(b + "start");
      w.end = doc.get_string(b + "end");
      if (w.start.empty() || w.end.empty())
        return Error::make(Errc::InvalidArgument,
                           "maintenance window " + std::to_string(i) + " needs start and end",
                           "ota config");
      c.windows.push_back(w);
    }
    if (c.confirm_timeout_s <= 0)
      return Error::make(Errc::InvalidArgument, "confirm_timeout_s must be > 0", "ota config");
    tick_interval_s_ = doc.get_double("tick_interval_s", 5.0);
    engine_.set_config(c);
    engine_.set_swupdate_binary(doc.get_string("swupdate_binary", "/usr/bin/swupdate"));
    if (on_config_) on_config_(tick_interval_s_);
    return Error::none();
  }
  void set_config_listener(std::function<void(double)> f) { on_config_ = std::move(f); }
  double tick_interval_s() const { return tick_interval_s_; }

  OtaEngine& engine() { return engine_; }
  // Hooks the table needs; provided by the wiring layer.
  void set_secret_provider(std::function<std::string()> f) { secret_ = std::move(f); }
  void set_running_version_provider(std::function<std::string()> f) { running_ = std::move(f); }
  void set_clock_provider(std::function<uint64_t()> f) { clock_fn_ = std::move(f); }
  void set_wall_provider(std::function<int64_t()> f) { wall_fn_ = std::move(f); }
  std::string secret() const { return secret_ ? secret_() : std::string(); }
  std::string running_version() const { return running_ ? running_() : std::string("0.0.0"); }
  uint64_t now_mono() const { return clock_fn_ ? clock_fn_() : 0; }
  int64_t wall_now_s() const { return wall_fn_ ? wall_fn_() : 0; }
  void set_spoke_sink(std::function<void(const OtaProgressEvent&)> f) {
    spoke_sink_ = std::move(f);
    engine_.set_event_sink([this](const OtaProgressEvent& ev) {
      if (spoke_sink_) spoke_sink_(ev);
    });
  }
  std::string status_json() const;

 private:
  OtaEngine engine_;
  std::function<void(const OtaProgressEvent&)> spoke_sink_;
  std::function<std::string()> secret_;
  std::function<std::string()> running_;
  std::function<uint64_t()> clock_fn_;
  std::function<int64_t()> wall_fn_;
  double tick_interval_s_ = 5.0;
  std::function<void(double)> on_config_;
};

CommandTable build_ota_table(OtaCore* core);

class OtaSpoke {
 public:
  OtaSpoke(OtaCore* core, Logger* log, const CommandTable* table, const std::string& hub_target,
           const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  // Configuration is delivered by istate over the channel and validated
  // against this schema before it can change behaviour (R4, §9).
  void set_schema(JValue schema);
  uint64_t config_revision() const { return channel_->config_revision(); }
  void send_event(const OtaProgressEvent& ev);
  void send_status();

 private:
  OtaCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<ConfigSink> config_sink_;
  std::unique_ptr<SpokeChannel<ota::OtaHub, ota::OtaSpokeFrame, ota::OtaHubFrame>> channel_;
};

}  // namespace ota
}  // namespace instr