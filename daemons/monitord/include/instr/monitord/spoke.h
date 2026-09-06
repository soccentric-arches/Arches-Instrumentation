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
 * @file daemons/monitord/include/instr/monitord/spoke.h
 * @brief monitord spoke runner: periodic sampling loop, spoke channel to istate,
 *
 * ctrl server. The daemon main wires these; the harness does the same
 * in-process.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <atomic>
#include <thread>

#include "instr/ctrl/ctrl.h"
#include "instr/monitord/transport.h"
#include "monitord.grpc.pb.h"

namespace instr {
namespace monitord {
using namespace domain;
using namespace instr::transport;
namespace domain {

class MonitorSpoke {
 public:
  MonitorSpoke(MonitorCore* core, Logger* log, const CommandTable* table,
               const std::string& hub_target, const std::string& build_version);

  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  // Configuration is delivered by istate over the channel (R4, §9).
  void set_schema(JValue schema);
  uint64_t config_revision() const { return channel_->config_revision(); }

  // Number of frames handed to the transport (for tests).
  uint64_t frames_enqueued() const {
    return frames_.load();
  }

 private:
  void sample_loop();

  MonitorCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<ConfigSink> config_sink_;
  std::unique_ptr<SpokeChannel<monitord::MonitorHub, monitord::MonitorSpokeFrame,
                               monitord::MonitorHubFrame>>
      channel_;
  std::atomic<bool> running_{false};
  std::thread sampler_thread_;
  std::atomic<uint64_t> frames_{0};
};

// Build the spoke channel with hub-frame dispatch wired to the command table.
std::unique_ptr<SpokeChannel<monitord::MonitorHub, monitord::MonitorSpokeFrame,
                             monitord::MonitorHubFrame>>
make_monitor_channel(MonitorCore* core, const CommandTable* table, Logger* log,
                     const std::string& hub_target, const std::string& build_version,
                     SpokeChannel<monitord::MonitorHub, monitord::MonitorSpokeFrame,
                                  monitord::MonitorHubFrame>::Options opts);

}  // namespace domain
}  // namespace monitord
}  // namespace instr