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
 * @file daemons/istate/include/instr/istate/pst.h
 * @brief /pst persistent store (REQ §6, R4). istate is the sole writer
 *
 * Layout: factory/ (read-only after manufacture), system/ (config per
 * daemon, .checksum per folder, redundant copies), state/ (runtime,
 * not backed up), backup/ (local archives). Storage headroom reserved with
 * hard per-consumer budgets enforced and alarms raised before the limit.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <map>
#include <string>
#include <vector>

#include "instr/common/atomic_file.h"
#include "instr/common/config.h"
#include "instr/common/filesystem.h"

namespace instr {
namespace istate {

// ---- per-consumer storage budgets (§6: spool, logs, OTA staging can never
// fill the partition and wedge the device) ----
class BudgetManager {
 public:
  struct Budget {
    std::string consumer;   // spool / logs / ota / backup
    uint64_t limit_bytes = 0;
    double alarm_at_pct = 0.8;  // alarm before the limit
  };
  explicit BudgetManager(IFileSystem& fs, const std::string& root) : fs_(fs), root_(root) {}

  void set_budget(const std::string& consumer, uint64_t limit_bytes) {
    budgets_[consumer] = Budget{consumer, limit_bytes};
  }
  // Reserve bytes for a consumer: fails loudly when over budget.
  Result<bool> reserve(const std::string& consumer, uint64_t bytes);
  // Usage tracking per consumer (registered by the components themselves).
  void record_usage(const std::string& consumer, uint64_t bytes) { usage_[consumer] = bytes; }
  uint64_t usage(const std::string& consumer) const {
    auto it = usage_.find(consumer);
    return it == usage_.end() ? 0 : it->second;
  }
  // Consumers currently over their 80% warning line (alarm before the limit).
  std::vector<std::string> over_warning() const;
  std::vector<std::string> over_limit() const;
  const std::map<std::string, Budget>& budgets() const { return budgets_; }

 private:
  IFileSystem& fs_;
  std::string root_;
  std::map<std::string, Budget> budgets_;
  std::map<std::string, uint64_t> usage_;
};

// ---- the /pst store ----
class PstStore {
 public:
  PstStore(IFileSystem& fs, const std::string& root, BudgetManager& budgets)
      : fs_(fs), root_(root), budgets_(budgets) {}

  // Mount and validate: verify checksums with fallback to redundant copies
  // (REQ §11: /pst corrupt → fall back; if both fail, boot unprovisioned).
  struct MountReport {
    bool ok = false;
    bool virgin = true;              // no system/ yet
    std::vector<std::string> recovered_from_backup;
    std::vector<std::string> corrupt;   // unrecoverable
  };
  MountReport mount();
  // Seed system/ from the shipped default YAML set (first boot / system reset).
  Error seed_defaults(const std::map<std::string, std::string>& daemon_default_yaml);

  // ---- config access (sole writer) ----
  Result<ConfigDocument> load_config(const std::string& daemon) const;
  Error save_config(const std::string& daemon, const ConfigDocument& doc);
  Result<std::string> read_protected(const std::string& rel_path) const;
  Error write_protected(const std::string& rel_path, const std::string& data);

  bool exists(const std::string& rel_path) const { return fs_.exists(fs_.join(root_, rel_path)); }

  // ---- schema version and migration (§6: .schema-version drives migration) ----
  std::string schema_version() const;
  Error set_schema_version(const std::string& v);
  // Merge a new release's defaults: adds missing keys, preserves existing
  // values by origin. Returns added paths.
  std::vector<std::string> migrate(const std::map<std::string, std::string>& new_defaults);

  // ---- reset semantics (§6.1) ----
  // System reset: wipes system/ and state/; keeps factory/ and backup/;
  // reseeds system/ from image defaults after.
  Error system_reset(const std::map<std::string, std::string>& defaults);
  // Factory reset: everything; manufacturing operation only, explicitly gated.
  Error factory_reset();

  std::string path(const std::string& rel) const { return fs_.join(root_, rel); }

 private:
  IFileSystem& fs_;
  std::string root_;
  BudgetManager& budgets_;
};

}  // namespace istate
}  // namespace instr