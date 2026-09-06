// ota daemon facade: core + command table + spoke.
#pragma once
#include <memory>

#include "instr/common/command_table.h"
#include "instr/ota/domain.h"
#include "instr/transport/transport.h"
#include "ota.grpc.pb.h"

namespace instr {
namespace ota {
using namespace domain;
using namespace instr::transport;

class OtaCore {
 public:
  OtaCore(IFileSystem& fs, platform::IPlatform& plat, TimeSource& ts, const OtaConfig& cfg)
      : engine_(fs, plat, ts, cfg) {}

  OtaEngine& engine() { return engine_; }
  // Hooks the table needs; provided by the wiring layer.
  void set_secret_provider(std::function<std::string()> f) { secret_ = std::move(f); }
  void set_running_version_provider(std::function<std::string()> f) { running_ = std::move(f); }
  void set_clock_provider(std::function<uint64_t()> f) { clock_fn_ = std::move(f); }
  void set_wall_provider(std::function<int64_t()> f) { wall_fn_ = std::move(f); }
  std::string secret() const { return secret_ ? secret_() : std::string(); }
  std::string running_version() const { return running_ ? running_() : std::string("0.0.0"); }
  uint64_t now_mono() const { return clock_fn_ ? clock_fn_() : 0; }
  int64_t wall_now_s() const { return wall_fn_ ? wall_fn_() : 0; }
  void set_spoke_sink(std::function<void(const OtaProgressEvent&)> f) {
    spoke_sink_ = std::move(f);
    engine_.set_event_sink([this](const OtaProgressEvent& ev) {
      if (spoke_sink_) spoke_sink_(ev);
    });
  }
  std::string status_json() const;

 private:
  OtaEngine engine_;
  std::function<void(const OtaProgressEvent&)> spoke_sink_;
  std::function<std::string()> secret_;
  std::function<std::string()> running_;
  std::function<uint64_t()> clock_fn_;
  std::function<int64_t()> wall_fn_;
};

CommandTable build_ota_table(OtaCore* core);

class OtaSpoke {
 public:
  OtaSpoke(OtaCore* core, Logger* log, const CommandTable* table, const std::string& hub_target,
           const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  void send_event(const OtaProgressEvent& ev);
  void send_status();

 private:
  OtaCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<SpokeChannel<ota::OtaHub, ota::OtaSpokeFrame, ota::OtaHubFrame>> channel_;
};

}  // namespace ota
}  // namespace instr