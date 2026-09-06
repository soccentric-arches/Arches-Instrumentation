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
 * @file daemons/telemetry/include/instr/telemetry/domain.h
 * @brief aggregation from monitord, Hardware, Faults and applications;
 *
 * batch shaping; bounded retention with drop policy; per-device health score
 * with contributing factors; reproducibility metadata (REQ §7.8).
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <deque>
#include <optional>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "instr/common/error.h"
#include "instr/common/timestamps.h"

namespace instr {
namespace telemetry {
namespace domain {

struct MetricDef {
  std::string name;
  std::string unit;
  std::string type;  // gauge | counter
  std::string app;  // registering application
};

struct AggWindow {
  std::string metric;
  std::string fn;  // min / max / mean / count / last
  double value = 0;
  uint32_t count = 0;
  uint64_t window_start_mono_ns = 0;
  uint64_t window_end_mono_ns = 0;
  std::string unit;
  std::map<std::string, std::string> tags;
};

struct HealthScoreValue {
  double score = 100;
  std::map<std::string, double> factors;
};

struct ReproMetadata {
  std::string firmware_version;
  std::string config_schema_version;
  std::string hardware_revision;
  std::string device_serial;
};

struct MetricRecord {
  std::string metric;
  double value = 0;
  std::string unit;
  std::string agg_fn;
  uint64_t window_start_mono_ns = 0;
  uint64_t window_end_mono_ns = 0;
  std::map<std::string, std::string> tags;
  uint32_t count = 0;
};

struct MetricBatch {
  std::vector<MetricRecord> records;
  HealthScoreValue health;
  ReproMetadata repro;
  uint64_t mono_ns = 0;
};

// Application metric registration API: duplicates are loud (alarm-visible).
class MetricRegistry {
 public:
  enum class Reg { Ok, Duplicate, Invalid };
  Reg register_metric(const std::string& name, const std::string& unit, const std::string& type,
                      const std::string& app);
  const std::map<std::string, MetricDef>& metrics() const { return metrics_; }
  const MetricDef* find(const std::string& name) const;

 private:
  std::map<std::string, MetricDef> metrics_;
};

// Aggregation windows: functions min, max, mean, count, last.
class Aggregator {
 public:
  void add(const std::string& metric, double value, uint64_t mono_ns,
           const std::map<std::string, std::string>& tags = {});
  // Close the current window and produce records for each metric×function.
  std::vector<AggWindow> close(uint64_t window_end_mono_ns);
  void set_functions(const std::vector<std::string>& fns) { functions_ = fns; }

 private:
  struct Sample {
    double v;
    uint64_t ns;
    std::map<std::string, std::string> tags;
  };
  std::map<std::string, std::deque<Sample>> current_;
  std::vector<std::string> functions_ = {"min", "max", "mean", "count", "last"};
  uint64_t window_start_ns_ = 0;
};

// Bounded local retention of unsent data; drop policy oldest-first, never
// faults or alarms (those are protected classes).
class RetentionBuffer {
 public:
  RetentionBuffer(size_t max_records) : max_records_(max_records) {}
  enum class Class { Telemetry, Protected };
  // Bounded local retention of unsent data; oldest telemetry dropped first
  // (§7.8, §11). Shrinking the bound drops immediately, never lazily.
  void set_max_records(size_t n);
  size_t max_records() const { return max_records_; }
  void push(const MetricBatch& b, Class cls);
  // Returns dropped telemetry batches count since last query.
  uint64_t dropped() const { return dropped_; }
  std::vector<MetricBatch> drain();
  size_t size() const { return buf_.size(); }

 private:
  struct Item {
    MetricBatch batch;
    Class cls;
  };
  std::deque<Item> buf_;
  size_t max_records_;
  uint64_t dropped_ = 0;
};

// Per-device health score: rolls monitord + Hardware + Faults + Alarms into
// a single number with contributing factors exposed.
struct HealthInputs {
  double cpu_util_pct = 0;
  double mem_used_pct = 0;
  double max_thermal_c = 0;
  double storage_wear_pct = -1;
  double storage_free_pct = 100;
  uint32_t hardware_fails = 0;
  double fault_rate_per_hour = 0;
  uint32_t alarms_critical = 0;
  uint32_t alarms_major = 0;
  uint32_t alarms_minor = 0;
};
HealthScoreValue compute_health_score(const HealthInputs& in);

// Batch shaping: bounded records per batch, flush interval.
class BatchShaper {
 public:
  BatchShaper(size_t max_records, double flush_interval_s)
      : max_records_(max_records), flush_interval_s_(flush_interval_s) {}
  // Ingest raw metric samples; when the batch is full or the interval
  // elapsed, emits a shaped batch of aggregation windows.
  std::optional<MetricBatch> ingest(const std::string& metric, double value,
                                    const std::string& unit, uint64_t mono_ns,
                                    const std::map<std::string, std::string>& tags);
  std::optional<MetricBatch> flush(uint64_t now_mono_ns);
  void set_health(const HealthScoreValue& h) { pending_health_ = h; }
  // Retuned when configuration arrives from istate (§7.8, §9).
  void set_max_records(size_t n) { max_records_ = n; }
  void set_flush_interval_s(double s) { flush_interval_s_ = s; }
  double flush_interval_s() const { return flush_interval_s_; }
  size_t max_records() const { return max_records_; }
  void set_repro(const ReproMetadata& m) { repro_ = m; }
  size_t pending_samples() const { return total_pending_; }

 private:
  Aggregator agg_;
  size_t max_records_;
  double flush_interval_s_;
  uint64_t last_flush_ns_ = 0;
  uint64_t window_start_ns_ = 0;
  size_t total_pending_ = 0;
  HealthScoreValue pending_health_;
  ReproMetadata repro_;
};

}  // namespace domain
}  // namespace telemetry
}  // namespace instr