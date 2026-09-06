// monitord spoke runner: periodic sampling loop, spoke channel to istate,
// ctrl server. The daemon main wires these; the harness does the same
// in-process.
#pragma once
#include <atomic>
#include <thread>

#include "instr/ctrl/ctrl.h"
#include "instr/monitord/transport.h"
#include "monitord.grpc.pb.h"

namespace instr {
namespace monitord {
using namespace domain;
using namespace instr::transport;
namespace domain {

class MonitorSpoke {
 public:
  MonitorSpoke(MonitorCore* core, Logger* log, const CommandTable* table,
               const std::string& hub_target, const std::string& build_version);

  void start();
  void stop();
  bool registered() const { return channel_->registered(); }

  // Number of frames handed to the transport (for tests).
  uint64_t frames_enqueued() const {
    return frames_.load();
  }

 private:
  void sample_loop();

  MonitorCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<SpokeChannel<monitord::MonitorHub, monitord::MonitorSpokeFrame,
                               monitord::MonitorHubFrame>>
      channel_;
  std::atomic<bool> running_{false};
  std::thread sampler_thread_;
  std::atomic<uint64_t> frames_{0};
};

// Build the spoke channel with hub-frame dispatch wired to the command table.
std::unique_ptr<SpokeChannel<monitord::MonitorHub, monitord::MonitorSpokeFrame,
                             monitord::MonitorHubFrame>>
make_monitor_channel(MonitorCore* core, const CommandTable* table, Logger* log,
                     const std::string& hub_target, const std::string& build_version,
                     SpokeChannel<monitord::MonitorHub, monitord::MonitorSpokeFrame,
                                  monitord::MonitorHubFrame>::Options opts);

}  // namespace domain
}  // namespace monitord
}  // namespace instr