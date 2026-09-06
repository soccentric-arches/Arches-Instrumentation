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
 * @file daemons/logmanager/include/instr/logmanager/domain.h
 * @brief collection, rotation, retention, compression, live tail,
 *
 * bundles with handle-based bulk transfer (payload never traverses istate's
 * control path), and crash forensics (REQ §7.7).
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "instr/common/error.h"
#include "instr/common/filesystem.h"
#include "instr/common/timestamps.h"

namespace instr {
namespace logmanager {
namespace domain {

struct SourceSpec {
  std::string name;          // logical source name
  std::string kind;          // journal | file
  std::string path;          // file path (or journal unit)
  std::string level;         // runtime level: TRACE..CRITICAL
  uint64_t rotate_size_bytes = 8 * 1024 * 1024;
  uint32_t retention_files = 5;   // rotated files kept per source
  uint64_t max_age_s = 7 * 24 * 3600;
  bool compress = true;
};

struct TailFilter {
  std::vector<std::string> sources;
  std::string min_level;      // "" = all
  uint64_t since_mono_ns = 0;
  uint32_t max_rate_per_s = 50;
  uint32_t timeout_s = 30;    // auto-stop
};

struct LogLine {
  uint64_t mono_ns = 0;
  std::string source;
  std::string level;
  std::string message;
};

// ---- storage: per-source files with rotation by size, retention by count
// and age, optional compression of rotated files ----
class LogStore {
 public:
  LogStore(IFileSystem& fs, std::string base_dir) : fs_(fs), base_(std::move(base_dir)) {}

  void configure(const std::vector<SourceSpec>& sources) { sources_ = sources; }
  const std::vector<SourceSpec>& sources() const { return sources_; }
  Result<const SourceSpec*> find(const std::string& name) const;

  // Append one line; rotates when the active file exceeds the size limit.
  Error append(const std::string& source, const LogLine& line);
  // Enforce retention (count + age) and the per-class storage budget.
  struct PruneStats { size_t files_removed = 0; uint64_t bytes_freed = 0; };
  PruneStats enforce_retention(uint64_t now_mono_ns);
  void set_budget_bytes(uint64_t b) { budget_bytes_ = b; }

  // Rotation helpers exposed for tests.
  std::vector<std::string> files_for(const std::string& source) const;
  uint64_t total_bytes() const;

  // Read the active file tail for a source (bounded).
  Result<std::vector<LogLine>> read_tail(const std::string& source, size_t max_lines) const;

  // Runtime level control per source (REQ §4/§7.7).
  void set_runtime_level(const std::string& source, const std::string& level);
  std::string runtime_level(const std::string& source) const;

 private:
  std::string active_path(const std::string& source) const {
    return fs_.join(base_, source + ".log");
  }
  IFileSystem& fs_;
  std::string base_;
  std::vector<SourceSpec> sources_;
  std::map<std::string, std::string> runtime_levels_;
  uint64_t budget_bytes_ = 128ull * 1024 * 1024;
};

// ---- live tail session: bounded, filtered, rate-limited, auto-stop ----
class TailSession {
 public:
  TailSession(TailFilter filter, uint64_t started_mono_ns)
      : filter_(std::move(filter)), started_mono_ns_(started_mono_ns) {}

  // Returns true when the line should be emitted; enforces the rate limit.
  bool accept(const LogLine& line, uint64_t now_mono_ns);
  bool expired(uint64_t now_mono_ns) const;
  bool matches(const LogLine& line) const;
  uint64_t emitted() const { return emitted_; }
  uint64_t dropped() const { return dropped_; }

 private:
  TailFilter filter_;
  uint64_t started_mono_ns_;
  uint64_t window_start_ns_ = 0;
  uint32_t window_emitted_ = 0;
  bool have_window_ = false;
  uint64_t emitted_ = 0;
  uint64_t dropped_ = 0;
};

// ---- bundles: support archive for a time window with integrity checksum;
// bulk transfer by handle ----
struct BundleMeta {
  std::string handle;
  uint64_t size_bytes = 0;
  std::string sha256;
  std::string window_json;   // window, sources, manifest summary
};

class BundleStore {
 public:
  BundleStore(IFileSystem& fs, std::string dir) : fs_(fs), dir_(std::move(dir)) {}
  // Where the LogStore keeps its files (needed to read payloads).
  void set_log_base_dir(const std::string& d) { log_base_ = d; }

  // Build a bundle from the log store for the given sources.
  Result<BundleMeta> create(LogStore& store, const std::vector<std::string>& sources,
                            uint64_t now_mono_ns);
  Result<BundleMeta> meta(const std::string& handle) const;
  // Payload fetch — served on the daemon's data socket, never via istate.
  Result<std::string> payload(const std::string& handle) const;
  std::vector<BundleMeta> list() const;

 private:
  std::string handle_path(const std::string& handle) const { return fs_.join(dir_, "bundle-" + handle + ".tar.gz"); }
  std::string meta_path(const std::string& handle) const { return fs_.join(dir_, "bundle-" + handle + ".json"); }
  IFileSystem& fs_;
  std::string dir_;
  std::string log_base_;
};

// ---- crash forensics: bounded coredump capture + last-N lines on abnormal
// daemon exit, folded into the bundle store ----
class CrashForensics {
 public:
  CrashForensics(IFileSystem& fs, std::string dir) : fs_(fs), dir_(std::move(dir)) {}
  struct Capture {
    std::string daemon;
    std::string coredump_file;
    uint32_t tail_lines = 0;
    uint64_t coredump_bytes = 0;
    bool ok = false;
  };
  // Coredump truncated to max_coredump_bytes; last_lines from the daemon's
  // log tail. Stored under the forensics dir for bundling.
  Result<Capture> capture(const std::string& daemon, const std::string& coredump_path,
                          const std::vector<std::string>& last_lines,
                          uint64_t max_coredump_bytes);
  std::vector<std::string> captured_files() const;

 private:
  IFileSystem& fs_;
  std::string dir_;
};

}  // namespace domain
}  // namespace logmanager
}  // namespace instr