// faults — purely passive kernel-log observation (REQ §7.5, R6). Never
// configures hardware, never raises an alarm. Rule-based matching, dedup,
// hard rate limiting with "suppressed N events" summaries.
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
  std::string dedup_key;      // template, e.g. "bus:$device"
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
  FaultProcessor(std::vector<FaultRule> rules)
      : engine_(std::move(rules)), summarizer_(1) {}

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

 private:
  RuleEngine engine_;
  Deduplicator dedup_;
  SuppressionSummarizer summarizer_;
  std::map<std::string, TokenBucket> buckets_;
  Stats stats_;
};

}  // namespace domain
}  // namespace faults
}  // namespace instr