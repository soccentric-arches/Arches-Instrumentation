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
 * @file daemons/ota/include/instr/ota/domain.h
 * @brief thin manager on top of SWUpdate (REQ §7.9). Artifacts land on disk
 *
 * before applying; the interface is shaped so true streaming can be added
 * later without breaking callers. Signature verification before staging;
 * integrity verification; download resume; stage to the inactive slot,
 * apply, set boot slot; explicit post-boot confirmation with automatic
 * rollback; anti-rollback; /home and /pst preserved; progress as a stream.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "instr/common/backoff.h"
#include "instr/common/crypto.h"
#include "instr/common/error.h"
#include "instr/common/filesystem.h"
#include "instr/common/rate_limiter.h"
#include "instr/common/timestamps.h"
#include "instr/platform/platform.h"

namespace instr {
namespace ota {
namespace domain {

struct ArtifactManifest {
  std::string version;
  std::string sha256;      // artifact integrity
  std::string hmac;        // signature over (version || sha256) with the enrollment secret
  uint64_t size_bytes = 0;
  bool preserve_home = true;   // /home and /pst preserved unless requested otherwise
  bool preserve_pst = true;
};

struct OtaConfig {
  std::string min_version;          // anti-rollback floor
  double confirm_timeout_s = 600;   // unconfirmed update rolls back automatically
  // maintenance windows: per-device schedules where updates are permitted.
  struct Window {
    std::string day;      // "*" or weekday abbreviations "mon,sun"
    std::string start;    // "02:00"
    std::string end;      // "04:00"
  };
  std::vector<Window> windows;
  uint64_t bandwidth_limit_bytes_per_s = 0;  // 0 = unlimited
};

struct UpdateState {
  std::string state = "idle";  // idle / download / verified / staged /
                               // applied_pending_confirm / confirmed / failed / rolled_back
  std::string from_version;
  std::string to_version;
  double progress = 0;
  std::string staged_slot;
  uint64_t confirm_deadline_mono = 0;
  std::string error;
};

struct HistoryEntry {
  std::string from_version;
  std::string to_version;
  std::string result;  // confirmed / rolled_back / failed
  int64_t finished_unix_s = 0;
};

struct OtaProgressEvent {
  std::string stage;
  double progress = 0;
  std::string message;
  std::string from_version;
  std::string to_version;
};

// Version comparison: dotted numeric with v-prefix tolerance ("1.2.10" >
// "1.2.9").
Result<int> compare_versions(const std::string& a, const std::string& b);

class OtaEngine {
 public:
  OtaEngine(IFileSystem& fs, platform::IPlatform& plat, TimeSource& ts, OtaConfig cfg)
      : fs_(fs), plat_(plat), ts_(ts), cfg_(cfg) {}

  // Configuration arrives from istate while the engine is live (§9).
  void set_config(const OtaConfig& c) { cfg_ = c; }
  const OtaConfig& config() const { return cfg_; }
  // SWUpdate is the install engine (§7.9); the path is configurable so a
  // test can point at a stub and CI never shells out to the real thing.
  void set_swupdate_binary(std::string p) { swupdate_binary_ = std::move(p); }
  const std::string& swupdate_binary() const { return swupdate_binary_; }
  // Seam for tests and for the simulated platform: runs the installer and
  // returns its exit status. Default implementation execs SWUpdate.
  using InstallFn = std::function<Result<int>(const std::string& artifact,
                                              const std::string& target_slot,
                                              const ArtifactManifest& m)>;
  void set_install_fn(InstallFn f) { install_fn_ = std::move(f); }

  const UpdateState& state() const { return state_; }
  const std::vector<HistoryEntry>& history() const { return history_; }

  // ---- artifact intake (land on disk before applying) ----
  // chunk-based receive supports resume: offset persisted; re-calls with the
  // same offset append. Returns bytes stored so far.
  Result<uint64_t> receive_chunk(const std::string& data, uint64_t offset);
  Result<uint64_t> received_bytes() const;

  // ---- verification: integrity then signature then anti-rollback ----
  Result<ArtifactManifest> parse_manifest(const std::string& manifest_json) const;
  Error verify(const ArtifactManifest& m, const std::string& enrollment_secret,
               const std::string& running_version);

  // ---- staging and apply ----
  Error stage(const ArtifactManifest& m, const std::string& enrollment_secret,
              const std::string& running_version);
  Error apply();

  // ---- post-boot confirmation (explicit) ----
  // On boot with a pending trial slot, the engine waits for confirm();
  // past the deadline it rolls back automatically (§11).
  Error confirm();
  Error rollback(const std::string& reason);
  // Called periodically: fires the automatic rollback when overdue.
  std::optional<OtaProgressEvent> tick(uint64_t now_mono_ns);

  // ---- maintenance windows & bandwidth ----
  bool in_maintenance_window(int64_t wall_unix_s) const;
  void set_windows(const std::vector<OtaConfig::Window>& w) { cfg_.windows = w; }
  void set_bandwidth_limit(uint64_t bytes_per_s) { cfg_.bandwidth_limit_bytes_per_s = bytes_per_s; }
  // Bytes the limiter permits right now (token bucket).
  uint64_t bandwidth_allotment(double dt_s);

  // ---- canary support: enough state for HQ staged rollouts ----
  std::string canary_json(double health_score, double fault_rate_24h) const;

  void set_event_sink(std::function<void(const OtaProgressEvent&)> f) { event_sink_ = std::move(f); }
  void set_enrollment_secret_provider(std::function<std::string()> f) {
    secret_provider_ = std::move(f);
  }

 private:
  Error emit(const std::string& stage, double progress, const std::string& message);
  // Runs the install engine (SWUpdate by default, install_fn_ when set).
  Result<int> run_install(const std::string& artifact, const std::string& target_slot,
                          const ArtifactManifest& m);
  std::string staging_path() const { return "/pst/state/ota/artifact.bin"; }
  std::string offset_path() const { return "/pst/state/ota/offset"; }
  std::string manifest_path() const { return "/pst/state/ota/manifest.json"; }
  std::string history_path() const { return "/pst/state/ota/history.json"; }
  std::string history_json() const;

  IFileSystem& fs_;
  platform::IPlatform& plat_;
  TimeSource& ts_;
  OtaConfig cfg_;
  UpdateState state_;
  std::vector<HistoryEntry> history_;
  std::function<void(const OtaProgressEvent&)> event_sink_;
  std::function<std::string()> secret_provider_;
  std::string swupdate_binary_ = "/usr/bin/swupdate";
  InstallFn install_fn_;
  TokenBucket bandwidth_{0, 0};
  double last_bw_now_s_ = 0;
  double tokens_ = 0;
};

}  // namespace domain
}  // namespace ota
}  // namespace instr