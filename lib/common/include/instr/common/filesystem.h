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
 * @file lib/common/include/instr/common/filesystem.h
 * @brief filesystem abstraction (REQ §4, §12)
 *
 * Domain logic that must touch storage goes through this interface so every
 * component is unit-testable without a real filesystem and fault injection
 * (corrupt /pst, full disk, storage errors) is structural, not bolted on.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "instr/common/error.h"

namespace instr {

class IFileSystem {
 public:
  virtual ~IFileSystem() = default;

  virtual bool exists(const std::string& path) = 0;
  virtual Result<std::string> read(const std::string& path) = 0;
  // Plain write (not atomic); callers needing atomicity use AtomicFile.
  virtual Error write(const std::string& path, const std::string& data, bool append = false) = 0;
  virtual Error mkdirs(const std::string& path) = 0;
  virtual Error remove(const std::string& path, bool recursive = false) = 0;
  virtual Error rename(const std::string& from, const std::string& to) = 0;
  virtual Result<std::vector<std::string>> list_dir(const std::string& path) = 0;
  virtual Result<uint64_t> file_size(const std::string& path) = 0;
  // Last-modification time, unix seconds. Age-based retention (§7.7) needs
  // it, and a store that cannot tell how old a file is cannot honour a
  // retention policy expressed in days.
  virtual Result<int64_t> file_mtime_s(const std::string& path) = 0;
  virtual Result<uint64_t> free_space(const std::string& path) = 0;
  virtual Error fsync_path(const std::string& path) = 0;
  virtual std::string join(const std::string& a, const std::string& b) const {
    return a.empty() ? b : (a.back() == '/' ? a + b : a + "/" + b);
  }
};

// POSIX implementation: temp+fsync+rename discipline lives in atomic_file.h;
// this type provides the primitives.
class NativeFileSystem : public IFileSystem {
 public:
  bool exists(const std::string& path) override;
  Result<std::string> read(const std::string& path) override;
  Error write(const std::string& path, const std::string& data, bool append = false) override;
  Error mkdirs(const std::string& path) override;
  Error remove(const std::string& path, bool recursive) override;
  Error rename(const std::string& from, const std::string& to) override;
  Result<std::vector<std::string>> list_dir(const std::string& path) override;
  Result<uint64_t> file_size(const std::string& path) override;
  Result<int64_t> file_mtime_s(const std::string& path) override;
  Result<uint64_t> free_space(const std::string& path) override;
  Error fsync_path(const std::string& path) override;
};

// In-memory implementation with fault injectors (REQ §12 fault injection:
// storage errors, corrupt reads, full disk, failed renames).
class SimulatedFileSystem : public IFileSystem {
 public:
  bool exists(const std::string& path) override;
  Result<std::string> read(const std::string& path) override;
  Error write(const std::string& path, const std::string& data, bool append = false) override;
  Error mkdirs(const std::string& path) override;
  Error remove(const std::string& path, bool recursive) override;
  Error rename(const std::string& from, const std::string& to) override;
  Result<std::vector<std::string>> list_dir(const std::string& path) override;
  Result<uint64_t> file_size(const std::string& path) override;
  Result<int64_t> file_mtime_s(const std::string& path) override;
  Result<uint64_t> free_space(const std::string& path) override;
  Error fsync_path(const std::string& path) override;

  // ---- fault injection knobs ----
  // Fail writes from the n-th write onwards (1-based; 0 disables).
  void fail_writes_after(int n) { fail_writes_after_ = n; }
  // Corrupt data returned by reads of `path` from the n-th read onwards
  // (simulates bit rot / torn sector).
  void corrupt_reads_of(const std::string& path, int from_read) { corrupt_[path] = from_read; }
  // Clamp reported free space (simulates a filling / full partition).
  void set_free_space(uint64_t bytes) { free_space_ = bytes; }
  // Simulated clock for file timestamps: tests advance it to age files
  // without waiting, so retention-by-age is testable in milliseconds.
  void set_now_unix_s(int64_t t) { now_unix_s_ = t; }
  int64_t now_unix_s() const { return now_unix_s_; }
  void set_file_mtime_s(const std::string& path, int64_t t) { mtimes_[path] = t; }
  void fail_renames(bool v) { fail_renames_ = v; }
  void fail_mkdirs(bool v) { fail_mkdirs_ = v; }
  void reset_injections() {
    fail_writes_after_ = 0;
    corrupt_.clear();
    fail_renames_ = false;
    fail_mkdirs_ = false;
  }

  // Test introspection.
  const std::map<std::string, std::string>& raw() const { return files_; }
  uint64_t used_bytes() const;

 private:
  bool is_dir(const std::string& p) const;
  std::map<std::string, std::string> files_;  // files only
  std::map<std::string, bool> dirs_;  // dir → recursive flag
  int fail_writes_after_ = 0;
  int write_count_ = 0;
  std::map<std::string, int> corrupt_;
  std::map<std::string, int> read_count_;
  std::map<std::string, int64_t> mtimes_;
  int64_t now_unix_s_ = 1'700'000'000;
  uint64_t free_space_ = 1ull << 40;
  bool fail_renames_ = false;
  bool fail_mkdirs_ = false;
};

}  // namespace instr