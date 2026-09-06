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
 * @file daemons/alarms/include/instr/alarms/domain.h
 * @brief classic telecom-style alarm manager (REQ §7.6, R5)
 *
 * Only applications set and clear alarms. State machine: raise → active →
 * acknowledged → cleared, plus severity change on an already-active alarm.
 * X.733 fields; time-based escalation; snapshot-on-fault; catalog merge
 * preserving operator edits. Active table persists across restart.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "common.pb.h"
#include "instr/common/alarm_codec.h"
#include "instr/common/error.h"
#include "instr/common/filesystem.h"
#include "instr/common/timestamps.h"

namespace instr {
namespace alarms {
namespace domain {

struct AlarmRecord {
  uint32_t alarm_id = 0;
  std::string symbolic_name;
  common::Severity severity = common::SEV_INFO;
  std::string owner;
  std::string additional_text;
  std::string specific_problem;
  std::string probable_cause;
  Stamp first_seen;
  Stamp last_seen;
  uint32_t count = 0;
  bool acknowledged = false;
  std::vector<uint32_t> correlated;
};

// X.733-flavoured events emitted to istate directly — never through
// telemetry batching (escalation semantics must not be delayed).
struct AlarmStateEvent {
  std::string action;  // raise / clear / ack / severity_change / snapshot_request
  AlarmRecord record;
  bool needs_snapshot = false;
};

struct EscalationRule {
  // Applies to an alarm id or (when alarm_id == 0) as the default policy.
  uint32_t alarm_id = 0;
  uint64_t promote_after_s = 3600;
  common::Severity promote_to = common::SEV_CRITICAL;
};

struct CatalogEntry {
  uint32_t id = 0;
  std::string name;
  common::Severity severity = common::SEV_INFO;
  std::string probable_cause;
  std::string specific_problem;
  std::string description;
  std::string escalation;
  std::string auto_clear;
  bool hq_reportable = true;
  // operator edit marker: severity overridden by a local operator is
  // preserved when the catalog is regenerated (§7.6).
  bool operator_edited = false;
};

class AlarmManager {
 public:
  AlarmManager(IFileSystem& fs, const std::string& active_db_path, TimeSource& ts)
      : fs_(fs), db_path_(active_db_path), ts_(ts) {}

  // ---- state machine ----
  // raise: new → active; existing → count++, last_seen update; severity
  // change on an already-active alarm emits severity_change.
  Result<AlarmStateEvent> raise(uint32_t alarm_id, const std::string& owner,
                           const std::string& additional_text,
                           const std::vector<uint32_t>& correlated = {});
  Result<AlarmStateEvent> acknowledge(uint32_t alarm_id);
  Result<AlarmStateEvent> clear(uint32_t alarm_id, const std::string& owner);

  const std::vector<AlarmRecord>& active() const { return active_; }
  Result<const AlarmRecord*> find(uint32_t alarm_id) const;

  // ---- time-based escalation ----
  void set_escalation(const std::vector<EscalationRule>& rules) { escalation_ = rules; }
  const std::vector<EscalationRule>& escalation() const { return escalation_; }
  // Sweep active, unacknowledged alarms past their promotion threshold.
  std::vector<AlarmStateEvent> escalation_sweep(uint64_t now_mono_ns);

  // ---- persistence across restart (state/alarms/active.db) ----
  Error save();
  Error load();

  // ---- snapshot-on-fault: a bounded diagnostic capture is requested when a
  // CRITICAL alarm fires; the evidence must exist at the moment it matters.
  void set_snapshot_sink(std::function<void(uint32_t alarm_id)> f) { snapshot_sink_ = std::move(f); }
  // Configuration may turn the capture off on resource-constrained boards.
  void set_snapshot_on_critical(bool on) { snapshot_on_critical_ = on; }
  bool snapshot_on_critical() const { return snapshot_on_critical_; }
  void set_event_sink(std::function<void(const AlarmStateEvent&)> f) { event_sink_ = std::move(f); }

  // ---- catalog merge (software update regeneration, §7.6) ----
  // Adds new entries; preserves operator-edited severities and operator
  // fields for existing ids. Returns counts added/updated.
  struct MergeStats {
    size_t added = 0;
    size_t updated = 0;
    size_t preserved_operator_edits = 0;
  };
  MergeStats merge_catalog(const std::vector<CatalogEntry>& incoming);
  const std::vector<CatalogEntry>& catalog() const { return catalog_; }
  std::vector<CatalogEntry>& mutable_catalog() { return catalog_; }
  Error save_catalog(const std::string& path);
  Error load_catalog(const std::string& path);

  std::string active_table_json() const;

 private:
  std::string active_json() const;
  IFileSystem& fs_;
  std::string db_path_;
  TimeSource& ts_;
  std::vector<AlarmRecord> active_;
  std::vector<EscalationRule> escalation_;
  std::vector<CatalogEntry> catalog_;
  std::function<void(uint32_t)> snapshot_sink_;
  bool snapshot_on_critical_ = true;
  std::function<void(const AlarmStateEvent&)> event_sink_;
};

// Catalog YAML parsing (shared shape with the codegen catalog).
Result<std::vector<CatalogEntry>> load_catalog_entries(const std::string& yaml_text);

}  // namespace domain
}  // namespace alarms
}  // namespace instr