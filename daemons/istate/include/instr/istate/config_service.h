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
 * @file daemons/istate/include/instr/istate/config_service.h
 * @brief configuration service (REQ §9, §10). Every key carries an
 *
 * origin; HQ pushes are signed and verified on device (tunnel compromise
 * alone must not reconfigure a fleet); audit trail records who changed
 * what, when, from where; drift report exposes effective config.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

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
  std::string actor;  // who (local operator / hq / app)
  std::string source;  // from where (configuration-ui / hq-tunnel / local-console)
  std::string action;  // config_set / update / remote_access / reset
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
  SetResult set(const std::string& daemon, const std::string& yaml_text, Origin origin, bool force);

  // ---- signing keys (§9) ----
  // HQ's Ed25519 config-signing public keys, by key id. The device holds
  // only public keys: opening one device yields nothing that can sign for
  // the fleet, which is the property §9 is after and that an HMAC — where
  // the verifier is also a signer — cannot provide.
  //
  // Several ids can be trusted at once so a key can be rotated without a
  // flag day: publish the new key, switch signing, retire the old one.
  void add_signing_key(const std::string& key_id, const std::string& public_key_raw32) {
    signing_keys_[key_id] = public_key_raw32;
  }
  void remove_signing_key(const std::string& key_id) { signing_keys_.erase(key_id); }
  const std::map<std::string, std::string>& signing_keys() const { return signing_keys_; }

  // Signed config push from HQ (§9): verified before applying, so tunnel
  // compromise alone cannot reconfigure a fleet.
  //
  // `signature` is Ed25519 over "<daemon>|<yaml>" and is the required path.
  // `hmac_hex` is the legacy shared-secret form: it is accepted only when no
  // signing key is provisioned at all (a device enrolled before key
  // distribution), and its use is reported by last_push_used_legacy_hmac().
  Result<std::vector<std::string>> apply_signed(const std::string& daemon,
                                                const std::string& yaml_text,
                                                const std::string& key_id,
                                                const std::string& signature,
                                                const std::string& hmac_hex,
                                                const std::string& enrollment_secret);
  bool last_push_used_legacy_hmac() const { return last_push_legacy_; }

  const std::map<std::string, JValue>& schemas() const { return schemas_; }

 private:
  PstStore& pst_;
  IFileSystem& fs_;
  std::map<std::string, JValue> schemas_;
  std::map<std::string, std::string> signing_keys_;
  bool last_push_legacy_ = false;
};

}  // namespace istate
}  // namespace instr