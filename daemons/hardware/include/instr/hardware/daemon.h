// hardware daemon facade: core + command table + spoke.
#pragma once
#include <memory>
#include <mutex>

#include "instr/common/command_table.h"
#include "instr/hardware/domain.h"
#include "instr/platform/platform.h"
#include "instr/transport/transport.h"
#include "hardware.grpc.pb.h"

namespace instr {
namespace hardware {
using namespace domain;
using namespace instr::transport;

class HardwareCore {
 public:
  HardwareCore(platform::IPlatform& plat, TimeSource& ts, const platform::DeviceDescriptor& desc)
      : mgr_(plat, ts, desc) {}

  DeviceManager& manager() { return mgr_; }
  void set_result_sink(std::function<void(const CheckResult&)> f) {
    std::lock_guard<std::mutex> lk(mu_);
    sink_ = std::move(f);
  }
  void emit(const CheckResult& r) {
    std::lock_guard<std::mutex> lk(mu_);
    if (sink_) sink_(r);
  }
  std::string descriptor_summary_json() const;

 private:
  DeviceManager mgr_;
  std::function<void(const CheckResult&)> sink_;
  std::mutex mu_;
};

CommandTable build_hardware_table(HardwareCore* core);

class HardwareSpoke {
 public:
  HardwareSpoke(HardwareCore* core, Logger* log, const CommandTable* table,
                const std::string& hub_target, const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  void send_result(const CheckResult& r);

 private:
  HardwareCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<
      SpokeChannel<hardware::HardwareHub, hardware::HardwareSpokeFrame,
                   hardware::HardwareHubFrame>>
      channel_;
};

}  // namespace hardware
}  // namespace instr