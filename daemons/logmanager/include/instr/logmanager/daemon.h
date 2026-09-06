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
 * @file daemons/logmanager/include/instr/logmanager/daemon.h
 * @brief logmanager daemon facade: core + command table + spoke + data socket for
 *
 * bulk payload transfer (handle-based; never via istate's control path).
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <memory>
#include <mutex>

#include "instr/common/command_table.h"
#include "instr/common/config_sink.h"
#include "instr/common/periodic.h"
#include "instr/logmanager/domain.h"
#include "instr/transport/transport.h"
#include "logmanager.grpc.pb.h"

namespace instr {
namespace logmanager {
using namespace domain;
using namespace instr::transport;

class LogCore {
 public:
  LogCore(IFileSystem& fs, const std::string& base_dir, TimeSource& ts)
      : store_(fs, base_dir),
        bundles_(fs, base_dir + "-bundles"),
        forensics_(fs, base_dir + "-crash"),
        ts_(ts) {
    bundles_.set_log_base_dir(base_dir);
  }

  // Configuration delivered by istate (§7.7, §9): which logs to collect,
  // rotation, retention and the storage budget for this resource class.
  struct Config {
    bool enabled = true;
    uint64_t budget_bytes = 128ull * 1024 * 1024;
    double prune_interval_s = 300.0;
    uint32_t tail_max_rate_per_s = 50;
    uint64_t coredump_max_bytes = 32ull * 1024 * 1024;
    std::vector<SourceSpec> sources;
  };
  const Config& config() const { return cfg_; }
  Error apply_config(const ConfigDocument& doc);
  void set_config_listener(std::function<void(const Config&)> f) { on_config_ = std::move(f); }

  // Where the bulk payload socket lives, reported to callers by bundle-fetch.
  void set_bulk_socket_path(std::string p) { bulk_socket_path_ = std::move(p); }
  const std::string& bulk_socket_path() const { return bulk_socket_path_; }

  LogStore& store() { return store_; }
  BundleStore& bundles() { return bundles_; }
  CrashForensics& forensics() { return forensics_; }
  TimeSource& clock() { return ts_; }

  void set_sink(std::function<void(const LogLine&)> f) {
    std::lock_guard<std::mutex> lk(mu_);
    sink_ = std::move(f);
  }
  void emit(const LogLine& l) {
    std::lock_guard<std::mutex> lk(mu_);
    if (sink_) sink_(l);
  }

 private:
  LogStore store_;
  BundleStore bundles_;
  CrashForensics forensics_;
  TimeSource& ts_;
  std::function<void(const LogLine&)> sink_;
  std::mutex mu_;
  Config cfg_;
  std::function<void(const Config&)> on_config_;
  std::string bulk_socket_path_ = "/run/instrumentation/logmanager-bulk.sock";
};

CommandTable build_log_table(LogCore* core);
int level_index_public(const std::string& level);
std::string first_source(LogCore* core);

class LogSpoke {
 public:
  LogSpoke(LogCore* core, Logger* log, const CommandTable* table, const std::string& hub_target,
           const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  // Configuration is delivered by istate over the channel and validated
  // against this schema before it can change behaviour (R4, §9).
  void set_schema(JValue schema);
  uint64_t config_revision() const { return channel_->config_revision(); }
  void send_line(const LogLine& l);
  void send_bundle(const BundleMeta& m);
  void send_crash(const std::string& daemon, const std::string& coredump, uint32_t tail_lines);

 private:
  LogCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<ConfigSink> config_sink_;
  std::unique_ptr<
      SpokeChannel<logmanager::LogHub, logmanager::LogSpokeFrame, logmanager::LogHubFrame>>
      channel_;
};

}  // namespace logmanager
}  // namespace instr