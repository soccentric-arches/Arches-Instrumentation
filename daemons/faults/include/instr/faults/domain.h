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
 * @file daemons/faults/include/instr/faults/domain.h
 * @brief purely passive kernel-log observation (REQ §7.5, R6). Never
 *
 * configures hardware, never raises an alarm. Rule-based matching, dedup,
 * hard rate limiting with "suppressed N events" summaries.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "common.pb.h"
#include "instr/common/alarm_codec.h"
#include "instr/common/error.h"
#include "instr/common/rate_limiter.h"
#include "instr/common/timestamps.h"

namespace instr {
namespace faults {
namespace domain {

struct FaultRule {
  std::string name;
  std::string pattern;
  std::regex re;
  uint32_t fault_id = 0;
  common::Severity severity = common::SEV_MAJOR;
  std::string x733_type;
  std::string probable_cause;
  std::string dedup_key;  // template, e.g. "bus:$device"
  uint32_t dedup_window_s = 60;
  double rate_limit_per_s = 5;
};

struct FaultRecord {
  uint32_t fault_id = 0;
  uint64_t mono_ns = 0;
  std::string rule;
  common::Severity severity = common::SEV_MAJOR;
  std::string raw_line;
  uint32_t occurrences = 1;
  std::string device_key;
  std::string x733_type;
  std::string probable_cause;
  bool backfill = false;
};

// Loads and validates a fault-rules.yaml document (rules are data).
Result<std::vector<FaultRule>> load_rules(const std::string& yaml_text);

// Rule matching with $device extraction heuristics for dedup keys.
class RuleEngine {
 public:
  explicit RuleEngine(std::vector<FaultRule> rules) : rules_(std::move(rules)) {}
  const std::vector<FaultRule>& rules() const { return rules_; }
  // First matching rule for a kernel line, or nullopt.
  std::optional<const FaultRule*> match(const std::string& line) const;
  // Renders a dedup template: replaces $device with an extracted token.
  static std::string render_dedup_key(const std::string& tmpl, const std::string& line);

 private:
  std::vector<FaultRule> rules_;
};

// Deduplication: identical keys within the window merge occurrence counts.
class Deduplicator {
 public:
  // Returns the occurrence count for the key (1 when new), and true when
  // this is the first occurrence within the window.
  std::pair<uint32_t, bool> observe(const std::string& key, uint64_t mono_ns, uint32_t window_s);
  size_t active_keys() const { return map_.size(); }

 private:
  struct Entry {
    uint64_t first_ns = 0;
    uint64_t last_ns = 0;
    uint32_t count = 0;
  };
  std::map<std::string, Entry> map_;
};

// The full passive processor: scan lines → faults with dedup + suppression.
class FaultProcessor {
 public:
  // A suppressed streak must reach this before a summary is emitted.
  static constexpr uint64_t kSuppressionSummaryThreshold = 10;

  FaultProcessor(std::vector<FaultRule> rules) : engine_(std::move(rules)) {}

  struct Stats {
    uint64_t lines_scanned = 0;
    uint64_t faults_raised = 0;
    uint64_t duplicates_merged = 0;
    uint64_t suppressed_events = 0;
    uint64_t backfill_lines = 0;
  };

  // Feed kernel-log lines. backfill marks records recovered from the boot
  // ring (read from the start on launch, §7.5).
  std::vector<FaultRecord> feed(const std::vector<std::string>& lines, uint64_t mono_ns,
                                bool backfill);
  const Stats& stats() const { return stats_; }
  const std::vector<FaultRule>& engine_rules() const { return engine_.rules(); }

  // Daemon-wide ceiling on emitted faults, on top of the per-rule limits
  // (§7.5: a stuck driver emitting 10k lines a second must not drown the
  // system, even when it trips several rules at once). 0 disables it.
  void set_global_rate_limit(double per_s) {
    global_rate_per_s_ = per_s;
    global_tokens_ = per_s;
    global_last_mono_ = 0;
  }
  double global_rate_limit() const { return global_rate_per_s_; }
  uint64_t globally_suppressed() const { return global_suppressed_; }

 private:
  // Returns false when the global ceiling is exhausted for this instant.
  bool global_allow(uint64_t mono_ns);

  RuleEngine engine_;
  Deduplicator dedup_;
  std::map<std::string, double> bucket_tokens_;
  std::map<std::string, uint64_t> bucket_last_mono_;
  std::map<std::string, uint64_t> suppressed_streak_;
  Stats stats_;
  double global_rate_per_s_ = 0;
  double global_tokens_ = 0;
  uint64_t global_last_mono_ = 0;
  uint64_t global_suppressed_ = 0;
};

}  // namespace domain
}  // namespace faults
}  // namespace instr