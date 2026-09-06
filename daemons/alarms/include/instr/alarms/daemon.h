// alarms daemon facade: core + command table + spoke.
#pragma once
#include <memory>

#include "instr/alarms/domain.h"
#include "instr/common/command_table.h"
#include "instr/transport/transport.h"
#include "alarms.grpc.pb.h"

namespace instr {
namespace alarms {
using namespace domain;
using namespace instr::transport;

class AlarmsCore {
 public:
  AlarmsCore(IFileSystem& fs, const std::string& db_path, TimeSource& ts)
      : mgr_(fs, db_path, ts), fs_(fs), ts_(ts) {}

  AlarmManager& manager() { return mgr_; }
  uint64_t now_mono() const { return ts_.mono_ns(); }
  void set_spoke_sink(std::function<void(const AlarmStateEvent&)> f) {
    spoke_sink_ = std::move(f);
    mgr_.set_event_sink([this](const AlarmStateEvent& ev) {
      if (spoke_sink_) spoke_sink_(ev);
    });
  }

 private:
  AlarmManager mgr_;
  IFileSystem& fs_;
  TimeSource& ts_;
  std::function<void(const AlarmStateEvent&)> spoke_sink_;
};

CommandTable build_alarms_table(AlarmsCore* core);

class AlarmsSpoke {
 public:
  AlarmsSpoke(AlarmsCore* core, Logger* log, const CommandTable* table,
              const std::string& hub_target, const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  void send_event(const AlarmStateEvent& ev);

 private:
  AlarmsCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<
      SpokeChannel<alarms::AlarmHub, alarms::AlarmSpokeFrame, alarms::AlarmHubFrame>>
      channel_;
};

}  // namespace alarms
}  // namespace instr