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
 * @file lib/common/include/instr/common/tar.h
 * @brief minimal POSIX ustar archive writer/reader
 *
 * Used by Backup/Restore and LogManager bundles: tar the files into an
 * archive; restore by untarring (REQ §7.10). No external tar binary — the
 * format is implemented here so it is testable and deterministic.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "instr/common/error.h"

namespace instr {
namespace tar {

struct FileEntry {
  std::string name;  // relative path inside the archive
  std::string data;
  uint32_t mode = 0644;
  uint64_t mtime = 0;
};

std::string write_ustar(const std::vector<FileEntry>& files);
Result<std::vector<FileEntry>> read_ustar(const std::string& archive);

}  // namespace tar
}  // namespace instr