#include "instr/monitord/transport.h"

#include "instr/monitord/table.h"

namespace instr {
namespace monitord {

void MonitorCore::configure(const platform::ResourceProfile& p) {
  sampler_.set_default_interval_s(p.sample_interval_default_s);
  for (const auto& kv : p.sample_intervals_per_class_s)
    sampler_.set_interval_for(kv.first, kv.second);
  window_ = RollingWindow(p.highres_window_s, p.highres_interval_s);
}

std::vector<MonitorSample> MonitorCore::sample_once() {
  auto samples = sampler_.tick();
  uint64_t now = ts_.mono_ns();
  {
    std::lock_guard<std::mutex> lk(mu_);
    // High-res rolling window: fine-grained classes retained locally (§7.3).
    for (const auto& s : samples) {
      if (s.resource_class == "thermal" || s.resource_class == "load") window_.push(now, s);
    }
    if (emit_) {
      auto cb = emit_;
      for (const auto& s : samples) cb(s);
    }
  }
  return samples;
}

std::vector<WearProjector::Projection> MonitorCore::wear_projections(uint64_t age_days) const {
  std::vector<WearProjector::Projection> out;
  for (const auto& d : plat_.storage_devices()) out.push_back(WearProjector::project(d, age_days));
  return out;
}

std::string MonitorCore::status_json() const {
  JValue v = JValue::object();
  v.set("interval_default_s", JValue(sampler_.interval_for("default")));
  JValue iv = JValue::object();
  for (const char* cls : {"cpu", "memory", "storage", "thermal", "load", "system"})
    iv.set(cls, JValue(sampler_.interval_for(cls)));
  v.set("intervals_per_class_s", iv);
  v.set("samples_produced", JValue(double(sampler_.stats().produced)));
  v.set("window_entries", JValue(double(window_.size())));
  return v.dump();
}

// ---------------- command table ----------------

CommandTable build_monitor_table(MonitorCore* core) {
  CommandTable t;
  t.add({"status", "show sampler status and intervals", {},
         [core](const std::string&) {
           if (!core) return CommandResult::err(Errc::Unavailable, "tool dispatches remotely");
           return CommandResult::good(core->status_json());
         }});
  t.add({"sample-once", "take one sampling pass and print the samples", {},
         [core](const std::string&) {
           if (!core) return CommandResult::err(Errc::Unavailable, "tool dispatches remotely");
           auto samples = core->sample_once();
           JValue arr = JValue::array();
           for (const auto& s : samples) {
             JValue o = JValue::object();
             o.set("metric", JValue(s.metric));
             o.set("value", JValue(s.value));
             o.set("unit", JValue(s.unit));
             o.set("class", JValue(s.resource_class));
             arr.push(o);
           }
           return CommandResult::good(arr.dump());
         }});
  t.add({"set-interval", "set the sampling interval for a resource class",
         {{"class", true, "resource class: cpu|memory|storage|thermal|load|system"},
          {"seconds", true, "sampling interval in seconds"}},
         [core](const std::string& args_json) {
           if (!core) return CommandResult::err(Errc::Unavailable, "tool dispatches remotely");
           auto a = args_object(args_json);
           if (!a.ok()) return CommandResult::from_error(a.error);
           std::string cls = (*a).get_string("class");
           double secs = (*a).get_number("seconds");
           if (cls.empty() || secs <= 0)
             return CommandResult::err(Errc::InvalidArgument, "class and positive seconds required");
           core->sampler().set_interval_for(cls, secs);
           return CommandResult::good(std::string("{\"set\":true}"));
         }});
  t.add({"wear", "storage wear projections for all devices", {},
         [core](const std::string&) {
           if (!core) return CommandResult::err(Errc::Unavailable, "tool dispatches remotely");
           auto p = core->wear_projections(365);
           JValue arr = JValue::array();
           for (const auto& w : p) {
             JValue o = JValue::object();
             o.set("device", JValue(w.device));
             o.set("wear_pct", JValue(w.wear_pct));
             o.set("remaining_days", JValue(double(w.remaining_days)));
             o.set("model", JValue(w.model));
             arr.push(o);
           }
           return CommandResult::good(arr.dump());
         }});
  return t;
}

}  // namespace monitord
}  // namespace instr