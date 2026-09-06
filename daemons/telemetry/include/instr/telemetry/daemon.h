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
 * @file daemons/telemetry/include/instr/telemetry/daemon.h
 * @brief telemetry daemon facade: core (relays from monitord/hardware/faults via
 *
 * istate), command table, spoke.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <memory>

#include "instr/common/command_table.h"
#include "instr/common/config_sink.h"
#include "instr/common/periodic.h"
#include "instr/telemetry/domain.h"
#include "instr/transport/transport.h"
#include "telemetry.grpc.pb.h"

namespace instr {
namespace telemetry {
using namespace domain;
using namespace instr::transport;

class TelemetryCore {
 public:
  TelemetryCore(TimeSource& ts) : ts_(ts), shaper_(200, 30.0), buffer_(50) {}

  // Configuration delivered by istate (§7.8, §9).
  struct Config {
    bool enabled = true;
    double interval = 30.0;
    size_t batch_max_records = 200;
    double aggregation_window_s = 30.0;
    size_t retention_batches = 50;
    std::vector<std::string> health_inputs{"monitord", "hardware", "faults", "alarms"};
  };
  const Config& config() const { return cfg_; }
  Error apply_config(const ConfigDocument& doc) {
    Config c;
    c.enabled = doc.get_bool("enabled", true);
    c.interval = doc.get_double("interval", 30.0);
    c.batch_max_records = size_t(doc.get_int("batch_max_records", 200));
    c.aggregation_window_s = doc.get_double("aggregation_window_s", c.interval);
    c.retention_batches = size_t(doc.get_int("retention_batches", 50));
    auto inputs = doc.get_list("health_inputs");
    if (!inputs.empty()) c.health_inputs = inputs;
    if (c.interval <= 0)
      return Error::make(Errc::InvalidArgument, "interval must be > 0", "telemetry config");
    if (c.batch_max_records == 0)
      return Error::make(Errc::InvalidArgument, "batch_max_records must be >= 1",
                         "telemetry config");
    cfg_ = c;
    shaper_.set_max_records(c.batch_max_records);
    shaper_.set_flush_interval_s(c.interval);
    buffer_.set_max_records(c.retention_batches);
    if (on_config_) on_config_(cfg_);
    return Error::none();
  }
  void set_config_listener(std::function<void(const Config&)> f) { on_config_ = std::move(f); }

  MetricRegistry& registry() { return registry_; }
  BatchShaper& shaper() { return shaper_; }
  RetentionBuffer& buffer() { return buffer_; }
  TimeSource& clock() { return ts_; }

  // Application metric registration API. Duplicate → alarm-visible event.
  MetricRegistry::Reg register_metric(const std::string& name, const std::string& unit,
                                      const std::string& type, const std::string& app);
  // Relayed samples (monitord via istate) are ingested into the shaper.
  void ingest_relayed(const std::string& metric, double value, const std::string& unit,
                      const std::map<std::string, std::string>& tags);
  // Health inputs from the other daemons (via istate relays / spool stats).
  void update_health(const HealthInputs& in) {
    auto h = compute_health_score(in);
    shaper().set_health(h);
    last_health_ = h;
  }
  const HealthScoreValue& last_health() const { return last_health_; }
  void set_repro(const ReproMetadata& m) { shaper().set_repro(m); }
  void set_sink(std::function<void(const MetricBatch&)> f) { sink_ = std::move(f); }

  uint64_t alarm_registrations_conflict = 0;

 public:

 private:
  TimeSource& ts_;
  MetricRegistry registry_;
  BatchShaper shaper_;
  RetentionBuffer buffer_;
  HealthScoreValue last_health_;
  std::function<void(const MetricBatch&)> sink_;
  Config cfg_;
  std::function<void(const Config&)> on_config_;
};

CommandTable build_telemetry_table(TelemetryCore* core);

class TelemetrySpoke {
 public:
  TelemetrySpoke(TelemetryCore* core, Logger* log, const CommandTable* table,
                 const std::string& hub_target, const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  // Configuration is delivered by istate over the channel and validated
  // against this schema before it can change behaviour (R4, §9).
  void set_schema(JValue schema);
  uint64_t config_revision() const { return channel_->config_revision(); }
  void send_batch(const MetricBatch& b);

 private:
  TelemetryCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<ConfigSink> config_sink_;
  std::unique_ptr<SpokeChannel<telemetry::TelemetryHub, telemetry::TelemetrySpokeFrame,
                               telemetry::TelemetryHubFrame>>
      channel_;
};

}  // namespace telemetry
}  // namespace instr