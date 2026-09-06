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
 * @file daemons/backup/include/instr/backup/domain.h
 * @brief YAML lists the paths; tar them into an archive; restore by
 *
 * untarring (REQ §7.10). Snapshot /pst/system under istate's direction;
 * manifest inside the archive; excludes factory/ and WireGuard keys; local
 * archives encrypted with a key derived from device identity; transactional
 * restore with preview.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "instr/common/crypto.h"
#include "instr/common/error.h"
#include "instr/common/filesystem.h"
#include "instr/common/tar.h"
#include "instr/common/timestamps.h"

namespace instr {
namespace backup {
namespace domain {

struct BackupManifest {
  std::string device_serial;
  std::string hardware_revision;
  std::string schema_version;
  std::string firmware_version;
  int64_t created_unix_s = 0;
  std::vector<std::string> files;  // relative paths inside the archive
  std::map<std::string, std::string> sha256;  // per-file integrity
};

struct BackupOptions {
  std::string device_serial;
  std::string hardware_revision;
  std::string schema_version;
  std::string firmware_version;
  std::string identity_secret;  // key derivation input (never logged, never stored)
  // HQ's X25519 public key (32 raw bytes). An archive destined for HQ is
  // sealed to this key, so the upload is unreadable to anything between the
  // device and HQ — including whatever holds the local archive key (§7.10).
  std::string hq_public_key;
};

struct ArchiveInfo {
  std::string name;
  uint64_t size_bytes = 0;
  std::string sha256;
  BackupManifest manifest;
};

class BackupEngine {
 public:
  BackupEngine(IFileSystem& fs, const BackupOptions& opts) : fs_(fs), opts_(opts) {}

  // ---- create ----
  // Paths listed by config; istate directs access. Excludes factory/ and
  // WireGuard keys unconditionally — device identity is not portable (a
  // backup restores config, never identity).
  Result<ArchiveInfo> create(const std::vector<std::string>& paths,
                             const std::vector<std::string>& excludes,
                             const std::string& archive_dir, int64_t now_unix_s);

  // ---- list / verify ----
  std::vector<ArchiveInfo> list(const std::string& archive_dir) const;
  Result<BackupManifest> read_manifest(const std::string& archive_path) const;

  // ---- restore preview: what will change before committing ----
  struct PreviewEntry {
    std::string path;
    std::string action;  // add / replace / delete / unchanged
    std::string detail;
  };
  enum class Compatibility { Ok, HardwareMismatch, SchemaMismatch };
  struct RestorePreview {
    Compatibility compatibility = Compatibility::Ok;
    bool replacement_board = false;  // manifest serial != ours → config-only + re-enroll
    std::vector<PreviewEntry> entries;
  };
  Result<RestorePreview> restore_preview(const std::string& archive_path,
                                         const std::vector<std::string>& restore_roots);

  // ---- transactional restore: completes fully or leaves prior state ----
  // 1) extract to a staging dir; 2) verify every hash; 3) commit via commit
  // callback (istate direction); 4) on any failure, leave prior state intact.
  using CommitFn = std::function<Error(const std::vector<tar::FileEntry>&)>;
  Result<size_t> restore_commit(const std::string& archive_path, const CommitFn& commit);

  static bool excluded_path(const std::string& rel, const std::vector<std::string>& excludes);

  // ---- encryption for upload (§7.10) ----
  // Local archives are sealed with a key derived from device identity;
  // archives going to HQ are sealed to HQ's public key instead. The two are
  // different threat models: the local key protects an archive at rest on
  // the device, HQ's key protects it in transit and at rest in the cloud.
  //
  // Ephemeral X25519: a fresh keypair per archive, the shared secret run
  // through HKDF. The ephemeral public key travels with the ciphertext, so
  // HQ needs only its own private key to open it and the device keeps no
  // long-term secret that could decrypt what it has already uploaded.
  //   layout: "AHQ1" || ephemeral_pub(32) || aead_seal(...)
  Result<std::string> seal_for_hq(const std::string& plaintext) const;
  static Result<std::string> open_from_hq(const std::string& sealed,
                                          const std::string& hq_private_key);
  bool hq_upload_available() const { return opts_.hq_public_key.size() == 32; }

 private:
  std::string key_for_archive() const;  // HKDF(identity serial+secret)
  Result<tar::FileEntry> decrypt_archive(const std::string& blob) const;
  std::string encrypt_archive(const std::string& tar_bytes) const;

  IFileSystem& fs_;
  BackupOptions opts_;
};

}  // namespace domain
}  // namespace backup
}  // namespace instr