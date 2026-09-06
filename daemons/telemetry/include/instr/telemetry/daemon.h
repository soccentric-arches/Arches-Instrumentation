// telemetry daemon facade: core (relays from monitord/hardware/faults via
// istate), command table, spoke.
#pragma once
#include <memory>

#include "instr/common/command_table.h"
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
};

CommandTable build_telemetry_table(TelemetryCore* core);

class TelemetrySpoke {
 public:
  TelemetrySpoke(TelemetryCore* core, Logger* log, const CommandTable* table,
                 const std::string& hub_target, const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  void send_batch(const MetricBatch& b);

 private:
  TelemetryCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<SpokeChannel<telemetry::TelemetryHub, telemetry::TelemetrySpokeFrame,
                               telemetry::TelemetryHubFrame>>
      channel_;
};

}  // namespace telemetry
}  // namespace instr