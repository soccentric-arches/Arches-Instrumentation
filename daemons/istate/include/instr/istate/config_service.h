// istate — configuration service (REQ §9, §10). Every key carries an
// origin; HQ pushes are signed and verified on device (tunnel compromise
// alone must not reconfigure a fleet); audit trail records who changed
// what, when, from where; drift report exposes effective config.
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "instr/common/config.h"
#include "instr/common/crypto.h"
#include "instr/common/filesystem.h"
#include "instr/istate/pst.h"
#include "instr/common/timestamps.h"

namespace instr {
namespace istate {

struct AuditEntry {
  uint64_t seq = 0;
  int64_t wall_unix_s = 0;
  std::string actor;      // who (local operator / hq / app)
  std::string source;     // from where (configuration-ui / hq-tunnel / local-console)
  std::string action;     // config_set / update / remote_access / reset
  std::string detail_json;
};

class AuditLog {
 public:
  AuditLog(IFileSystem& fs, const std::string& path, size_t max_entries)
      : fs_(fs), path_(path), max_entries_(max_entries) {}
  void append(const std::string& actor, const std::string& source, const std::string& action,
              const std::string& detail_json, TimeSource& ts);
  const std::deque<AuditEntry>& entries() const { return entries_; }
  Error persist();
  Error load();

 private:
  IFileSystem& fs_;
  std::string path_;
  size_t max_entries_;
  std::deque<AuditEntry> entries_;
  uint64_t next_seq_ = 1;
};

class ConfigService {
 public:
  ConfigService(PstStore& pst, IFileSystem& fs) : pst_(pst), fs_(fs) {}

  // Per-daemon JSON schema (validated at load; rejected with a clear
  // file-and-line error).
  void set_schema(const std::string& daemon, const JValue& schema) { schemas_[daemon] = schema; }

  // Get effective config (drift report: actual effective config so HQ can
  // compare against intent).
  Result<ConfigDocument> get(const std::string& daemon) const;
  std::string drift_report_json(const std::string& daemon) const;

  // Set: merges incoming values by origin rules. Returns rejected paths.
  struct SetResult {
    std::vector<std::string> rejected;
    std::string error;
    bool ok = false;
  };
  SetResult set(const std::string& daemon, const std::string& yaml_text, Origin origin,
                bool force);

  // Signed config push from HQ (§9): verified with the enrollment secret
  // before applying — tunnel compromise alone cannot reconfigure a fleet.
  Result<std::vector<std::string>> apply_signed(const std::string& daemon,
                                                const std::string& yaml_text,
                                                const std::string& key_id,
                                                const std::string& hmac_hex,
                                                const std::string& enrollment_secret);

  const std::map<std::string, JValue>& schemas() const { return schemas_; }

 private:
  PstStore& pst_;
  IFileSystem& fs_;
  std::map<std::string, JValue> schemas_;
};

}  // namespace istate
}  // namespace instr