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
 * @file daemons/faults/include/instr/faults/daemon.h
 * @brief faults daemon facade: core + command table + spoke
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <memory>

#include "instr/common/command_table.h"
#include "instr/common/config_sink.h"
#include "instr/common/periodic.h"
#include "instr/faults/domain.h"
#include "instr/platform/platform.h"
#include "instr/transport/transport.h"
#include "faults.grpc.pb.h"

namespace instr {
namespace faults {
using namespace domain;
using namespace instr::transport;

// Configuration delivered by istate (R4: faults never opens /pst itself).
struct FaultsConfig {
  bool enabled = true;
  bool backfill_boot_log = true;
  double poll_interval_s = 1.0;  // kernel ring / journal follow interval
  double max_events_per_s = 0;  // 0 = rule-level limits only
  bool stream_to_hq = true;
};

class FaultsCore {
 public:
  FaultsCore() : processor_(std::vector<FaultRule>{}) {}

  // ---- configuration (§7.5, §9) ----
  const FaultsConfig& config() const { return cfg_; }
  Error apply_config(const ConfigDocument& doc) {
    FaultsConfig c;
    c.enabled = doc.get_bool("enabled", true);
    c.backfill_boot_log = doc.get_bool("backfill_boot_log", true);
    c.poll_interval_s = doc.get_double("poll_interval_s", 1.0);
    c.max_events_per_s = doc.get_double("max_events_per_s", 0);
    c.stream_to_hq = doc.get_bool("stream_to_hq", true);
    if (c.poll_interval_s <= 0)
      return Error::make(Errc::InvalidArgument, "poll_interval_s must be > 0", "faults config");
    cfg_ = c;
    if (processor_) (*processor_).set_global_rate_limit(cfg_.max_events_per_s);
    if (on_config_) on_config_(cfg_);
    return Error::none();
  }
  void set_config_listener(std::function<void(const FaultsConfig&)> f) {
    on_config_ = std::move(f);
  }

  Error load_rules(const std::string& yaml_text) {
    auto parsed = domain::load_rules(yaml_text);
    if (!parsed.ok()) return parsed.error;
    rules_text_ = yaml_text;
    processor_ = FaultProcessor(*parsed);
    return Error::none();
  }
  const std::string& rules_text() const { return rules_text_; }

  std::vector<FaultRecord> feed(const std::vector<std::string>& lines, uint64_t mono_ns,
                                bool backfill) {
    auto out = (*processor_).feed(lines, mono_ns, backfill);
    for (const auto& r : out) {
      if (sink_) sink_(r);
    }
    return out;
  }
  void set_sink(std::function<void(const FaultRecord&)> f) { sink_ = std::move(f); }
  const FaultProcessor::Stats& stats() const { return (*processor_).stats(); }
  const std::vector<FaultRule>& rules() const {
    return processor_ ? (*processor_).engine_rules() : empty_rules_;
  }

 private:
  std::string rules_text_;
  std::optional<FaultProcessor> processor_;
  std::vector<FaultRule> empty_rules_;
  std::function<void(const FaultRecord&)> sink_;
  FaultsConfig cfg_;
  std::function<void(const FaultsConfig&)> on_config_;
};

CommandTable build_faults_table(FaultsCore* core);

class FaultsSpoke {
 public:
  FaultsSpoke(FaultsCore* core, Logger* log, const CommandTable* table,
              const std::string& hub_target, const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  void send_fault(const FaultRecord& r);
  void send_stats();
  // Configuration arrives over the channel (§9); "" = accepted.
  void set_schema(JValue schema);
  uint64_t config_revision() const { return channel_->config_revision(); }

 private:
  FaultsCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<ConfigSink> config_sink_;
  std::unique_ptr<SpokeChannel<faults::FaultHub, faults::FaultSpokeFrame, faults::FaultHubFrame>>
      channel_;
};

// The kernel-log follower (§7.5: "real-time monitoring of the kernel ring
// buffer and journal"). Backfills the boot window once, then polls for new
// records on the configured interval. Purely passive (R6): it observes and
// reports, and never configures hardware or raises an alarm.
class FaultFollower {
 public:
  FaultFollower(FaultsCore* core, platform::IPlatform* plat, TimeSource* ts, Logger* log);
  // Reads the ring from the start and feeds it as backfill. Call once.
  size_t backfill();
  // One follow pass: feeds only records not seen before.
  size_t poll();
  void start();
  void stop();
  void set_interval_s(double s) { worker_.set_interval_s(s); }
  uint64_t polls() const { return worker_.ticks(); }

 private:
  FaultsCore* core_;
  platform::IPlatform* plat_;
  TimeSource* ts_;
  Logger* log_;
  PeriodicWorker worker_;
};

}  // namespace faults
}  // namespace instr