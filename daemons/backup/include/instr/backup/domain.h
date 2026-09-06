// backup — YAML lists the paths; tar them into an archive; restore by
// untarring (REQ §7.10). Snapshot /pst/system under istate's direction;
// manifest inside the archive; excludes factory/ and WireGuard keys; local
// archives encrypted with a key derived from device identity; transactional
// restore with preview.
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