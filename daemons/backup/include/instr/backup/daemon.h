// backup daemon facade: core + command table + spoke.
#pragma once
#include <memory>

#include "instr/backup/domain.h"
#include "instr/common/command_table.h"
#include "instr/transport/transport.h"
#include "backup.grpc.pb.h"

namespace instr {
namespace backup {
using namespace domain;
using namespace instr::transport;

class BackupCore {
 public:
  BackupCore(IFileSystem& fs, const BackupOptions& opts, TimeSource& ts)
      : engine_(fs, opts), ts_(ts) {}

  BackupEngine& engine() { return engine_; }
  void set_archive_dir(const std::string& d) { archive_dir_ = d; }
  const std::string& archive_dir() const { return archive_dir_; }
  void set_restore_roots(const std::vector<std::string>& roots) { restore_roots_ = roots; }
  const std::vector<std::string>& restore_roots() const { return restore_roots_; }
  // istate-directed commit: writes staged files under istate's supervision.
  void set_commit_fn(BackupEngine::CommitFn f) { commit_fn_ = std::move(f); }
  TimeSource& clock() { return ts_; }
  void set_event_sink(std::function<void(const std::string& action, const std::string& meta,
                                         bool ok, const std::string& err)> f) {
    sink_ = std::move(f);
  }
  void emit_event(const std::string& action, const std::string& meta, bool ok,
                  const std::string& err) {
    if (sink_) sink_(action, meta, ok, err);
  }
  // Schedule (runtime state; persisted config lives in /pst via istate).
  bool schedule_enabled() const { return schedule_enabled_; }
  double schedule_interval_s() const { return schedule_interval_s_; }
  void set_schedule(bool enabled, double interval_s) {
    schedule_enabled_ = enabled;
    schedule_interval_s_ = interval_s;
  }
  bool has_commit_fn() const { return bool(commit_fn_); }
  Error commit(const std::vector<tar::FileEntry>& files) { return commit_fn_(files); }

 private:
  bool schedule_enabled_ = false;
  double schedule_interval_s_ = 86400;
  BackupEngine engine_;
  TimeSource& ts_;
  std::string archive_dir_ = "/pst/backup";
  std::vector<std::string> restore_roots_ = {"/pst/system"};
  BackupEngine::CommitFn commit_fn_;
  std::function<void(const std::string&, const std::string&, bool, const std::string&)> sink_;
};

CommandTable build_backup_table(BackupCore* core);

class BackupSpoke {
 public:
  BackupSpoke(BackupCore* core, Logger* log, const CommandTable* table,
              const std::string& hub_target, const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  void send_event(const std::string& action, const std::string& archive, const std::string& meta,
                  bool ok, const std::string& err);

 private:
  BackupCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<
      SpokeChannel<backup::BackupHub, backup::BackupSpokeFrame, backup::BackupHubFrame>>
      channel_;
};

}  // namespace backup
}  // namespace instr