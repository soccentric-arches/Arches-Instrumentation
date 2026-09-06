// Arches Instrumentation — atomic file writes with checksum and redundant
// copy (REQ §4, §6). Every write: temp → fsync → rename → fsync dir.
// Checksummed files carry `<name>.checksum` (SHA-256 hex) plus a redundant
// `<name>.bak` copy per level; readers fall back to the copy on corruption.
#pragma once
#include <string>

#include "instr/common/error.h"
#include "instr/common/filesystem.h"

namespace instr {

class AtomicFile {
 public:
  // Plain atomic write (no checksum sidecars). temp = path + ".tmp-<pid>".
  static Error write(IFileSystem& fs, const std::string& path, const std::string& data);

  // Atomic write with .checksum and .bak redundant copy.
  static Error write_protected(IFileSystem& fs, const std::string& path,
                               const std::string& data);

  // Read a protected file; on checksum mismatch of the primary, fall back to
  // the .bak copy. Returns Corrupt only when both fail (REQ §11: /pst corrupt
  // → fall back to redundant copy; if both fail, boot unprovisioned).
  static Result<std::string> read_protected(IFileSystem& fs, const std::string& path);

  static std::string checksum_path(const std::string& path) { return path + ".checksum"; }
  static std::string backup_path(const std::string& path) { return path + ".bak"; }
};

}  // namespace instr