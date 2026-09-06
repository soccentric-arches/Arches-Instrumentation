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
 * @file lib/common/include/instr/common/atomic_file.h
 * @brief atomic file writes with checksum and redundant
 *
 * copy (REQ §4, §6). Every write: temp → fsync → rename → fsync dir.
 * Checksummed files carry `<name>.checksum` (SHA-256 hex) plus a redundant
 * `<name>.bak` copy per level; readers fall back to the copy on corruption.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

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
  static Error write_protected(IFileSystem& fs, const std::string& path, const std::string& data);

  // Read a protected file; on checksum mismatch of the primary, fall back to
  // the .bak copy. Returns Corrupt only when both fail (REQ §11: /pst corrupt
  // → fall back to redundant copy; if both fail, boot unprovisioned).
  static Result<std::string> read_protected(IFileSystem& fs, const std::string& path);

  static std::string checksum_path(const std::string& path) { return path + ".checksum"; }
  static std::string backup_path(const std::string& path) { return path + ".bak"; }
};

}  // namespace instr