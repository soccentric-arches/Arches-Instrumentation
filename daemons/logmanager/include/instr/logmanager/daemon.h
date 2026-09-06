// logmanager daemon facade: core + command table + spoke + data socket for
// bulk payload transfer (handle-based; never via istate's control path).
#pragma once
#include <memory>
#include <mutex>

#include "instr/common/command_table.h"
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
      : store_(fs, base_dir), bundles_(fs, base_dir + "-bundles"),
        forensics_(fs, base_dir + "-crash"), ts_(ts) {
    bundles_.set_log_base_dir(base_dir);
  }

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
  void send_line(const LogLine& l);
  void send_bundle(const BundleMeta& m);
  void send_crash(const std::string& daemon, const std::string& coredump, uint32_t tail_lines);

 private:
  LogCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<
      SpokeChannel<logmanager::LogHub, logmanager::LogSpokeFrame, logmanager::LogHubFrame>>
      channel_;
};

}  // namespace logmanager
}  // namespace instr