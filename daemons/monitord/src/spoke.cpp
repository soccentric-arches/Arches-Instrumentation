#include "instr/monitord/spoke.h"

#include <algorithm>

namespace instr {
namespace monitord {
namespace domain {

using ChannelT = SpokeChannel<monitord::MonitorHub, monitord::MonitorSpokeFrame,
                              monitord::MonitorHubFrame>;

MonitorSpoke::MonitorSpoke(MonitorCore* core, Logger* log, const CommandTable* table,
                           const std::string& hub_target, const std::string& build_version)
    : core_(core), log_(log), table_(table) {
  ChannelT::Options o;
  o.target = hub_target;
  o.daemon = "monitord";
  o.build_version = build_version;
  o.capabilities = {"samples", "wear-projection", "highres-window"};
  channel_ = std::make_unique<ChannelT>(std::move(o), *log);
  // Hub-frame handling: commands dispatch through the shared table and are
  // acked; relays/time-sync reach the core via the ingest path.
  channel_->set_hub_handler([this](const monitord::MonitorHubFrame& hf) {
    if (hf.has_command()) {
      CommandResult r = table_->dispatch(hf.command().op(), hf.command().json_args());
      common::Ack ack;
      ack.set_id(hf.command().id());
      ack.set_ok(r.ok);
      if (r.ok) {
        ack.set_json_payload(r.json_data);
      } else {
        ack.set_code(r.error_code);
        ack.set_message(r.error_message);
      }
      channel_->send_ack(ack);
    }
  });
}

void MonitorSpoke::start() {
  running_ = true;
  channel_->start();
  sampler_thread_ = std::thread([this] { sample_loop(); });
}

void MonitorSpoke::stop() {
  running_ = false;
  if (sampler_thread_.joinable()) sampler_thread_.join();
  channel_->stop();
}

void MonitorSpoke::sample_loop() {
  while (running_.load()) {
    auto samples = core_->sample_once();
    for (const auto& s : samples) {
      channel_->enqueue([&s](monitord::MonitorSpokeFrame& f) {
        monitord::Sample* m = f.mutable_sample();
        m->mutable_ts()->set_mono_ns(s.mono_ns);
        m->set_metric(s.metric);
        m->set_value(s.value);
        m->set_unit(s.unit);
        m->set_resource_class(s.resource_class);
        for (const auto& kv : s.tags) (*m->mutable_tags())[kv.first] = kv.second;
      });
      ++frames_;
    }
    // Sleep until the nearest class deadline (min interval across classes).
    double interval = 1e9;
    for (const char* cls : {"cpu", "memory", "storage", "thermal", "load", "system"})
      interval = std::min(interval, core_->sampler().interval_for(cls));
    struct timespec ts;
    ts.tv_sec = time_t(interval);
    ts.tv_nsec = long((interval - double(time_t(interval))) * 1e9);
    nanosleep(&ts, nullptr);
  }
}

}  // namespace domain
}  // namespace monitord
}  // namespace instr