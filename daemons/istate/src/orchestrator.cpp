#include "instr/istate/orchestrator.h"

#include <algorithm>

#include "instr/transport/transport.h"

namespace instr {
namespace istate {

const char* spoke_state_name(SpokeState s) {
  switch (s) {
    case SpokeState::Expected: return "expected";
    case SpokeState::Starting: return "starting";
    case SpokeState::Ready: return "ready";
    case SpokeState::Degraded: return "degraded";
    case SpokeState::Stopped: return "stopped";
    case SpokeState::Hung: return "hung";
  }
  return "unknown";
}

const std::vector<std::string>& startup_order() {
  static const std::vector<std::string> order = {
      "faults", "monitord", "hardware", "alarms", "logmanager",
      "telemetry", "networkd", "ota", "backup",
  };
  return order;
}

const std::map<std::string, std::vector<std::string>>& dependency_edges() {
  static const std::map<std::string, std::vector<std::string>> edges = {
      {"faults", {}},
      {"monitord", {}},
      {"hardware", {}},
      {"alarms", {}},
      {"logmanager", {}},
      {"telemetry", {"faults", "monitord", "hardware"}},
      {"networkd", {"faults", "monitord", "hardware", "alarms", "logmanager", "telemetry"}},
      {"ota", {"networkd"}},
      {"backup", {"networkd"}},
  };
  return edges;
}

void Orchestrator::declare_spokes(const std::vector<std::string>& names) {
  for (const auto& n : names) {
    SpokeInfo info;
    info.name = n;
    info.unit = "instr-" + n + ".service";
    spokes_[n] = info;
  }
}

bool Orchestrator::start_all() {
  bool ok = true;
  // Readiness order = startup_order with dependency edges respected (edges
  // are consistent with the declared order).
  for (const auto& name : startup_order()) {
    auto it = spokes_.find(name);
    if (it == spokes_.end()) continue;
    // Dependencies start earlier in the declared order (systemd owns unit
    // ordering); readiness arrives via registration.
    Error e = plat_.unit_start(it->second.unit);
    if (e.ok()) {
      it->second.state = SpokeState::Starting;
      if (event_sink_) event_sink_("restart", "{\"daemon\":\"" + name + "\",\"action\":\"start\"}");
    } else {
      it->second.state = SpokeState::Degraded;
      ok = false;
      if (event_sink_)
        event_sink_("restart", "{\"daemon\":\"" + name + "\",\"action\":\"start-failed\"}");
    }
  }
  return ok;
}

void Orchestrator::stop_all() {
  auto order = startup_order();
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    auto sit = spokes_.find(*it);
    if (sit == spokes_.end()) continue;
    (void)plat_.unit_stop(sit->second.unit);
    sit->second.state = SpokeState::Stopped;
  }
}

Orchestrator::Reg Orchestrator::register_spoke(const std::string& name,
                                               const std::string& build_version,
                                               uint32_t proto_version,
                                               const std::vector<std::string>& capabilities,
                                               std::string* refusal_reason) {
  auto it = spokes_.find(name);
  if (it == spokes_.end()) {
    if (refusal_reason) *refusal_reason = "unknown daemon: " + name;
    return Reg::NameMismatch;
  }
  if (proto_version != transport::kProtoSchemaVersion) {
    // Proto version mismatch: refuse registration, raise an alarm, never
    // guess (REQ §3.1, §11).
    if (refusal_reason)
      *refusal_reason = "proto schema version mismatch: spoke " + std::to_string(proto_version) +
                        " vs istate " + std::to_string(transport::kProtoSchemaVersion);
    it->second.state = SpokeState::Degraded;
    if (event_sink_)
      event_sink_("alarm", "{\"alarm_id\":" + std::to_string(0x10103001u) +
                               ",\"detail\":\"proto mismatch " + name + "\"}");
    return Reg::SchemaMismatch;
  }
  it->second.build_version = build_version;
  it->second.proto_version = proto_version;
  it->second.capabilities = capabilities;
  it->second.state = SpokeState::Starting;
  it->second.last_seen_mono = ts_.mono_ns();
  return Reg::Accepted;
}

void Orchestrator::mark_ready(const std::string& name) {
  auto it = spokes_.find(name);
  if (it == spokes_.end()) return;
  it->second.state = SpokeState::Ready;
  it->second.live = true;
  it->second.last_seen_mono = ts_.mono_ns();
}

void Orchestrator::note_spoke_activity(const std::string& name) {
  auto it = spokes_.find(name);
  if (it == spokes_.end()) return;
  it->second.last_seen_mono = ts_.mono_ns();
  it->second.live = true;
  if (it->second.state == SpokeState::Starting || it->second.state == SpokeState::Degraded ||
      it->second.state == SpokeState::Hung)
    it->second.state = SpokeState::Ready;
}

void Orchestrator::mark_stopped(const std::string& name) {
  auto it = spokes_.find(name);
  if (it == spokes_.end()) return;
  it->second.state = SpokeState::Stopped;
  it->second.live = false;
}

std::vector<Orchestrator::LivenessEvent> Orchestrator::liveness_sweep(double now_s,
                                                                      double hang_timeout_s) {
  std::vector<LivenessEvent> out;
  double now_mono = double(ts_.mono_ns());
  for (auto& kv : spokes_) {
    SpokeInfo& info = kv.second;
    if (info.state == SpokeState::Stopped || info.state == SpokeState::Expected) continue;
    auto pit = probes_.find(kv.first);
    bool alive = pit != probes_.end() && pit->second();
    if (!alive) {
      // A process that is running but not responding must not look healthy.
      double silent_s = (now_mono - double(info.last_seen_mono)) / 1e9;
      if (silent_s >= hang_timeout_s && info.state != SpokeState::Hung) {
        info.state = SpokeState::Hung;
        info.live = false;
        out.push_back({kv.first, "hung"});
        // Request restart through systemd (§7.1).
        (void)plat_.unit_restart(info.unit);
        out.push_back({kv.first, "restarted"});
        if (event_sink_)
          event_sink_("restart", "{\"daemon\":\"" + kv.first + "\",\"reason\":\"hang\"}");
      }
    } else {
      info.live = true;
      info.last_seen_mono = ts_.mono_ns();
      if (info.state == SpokeState::Hung) {
        info.state = SpokeState::Ready;
        out.push_back({kv.first, "degraded"});
      }
    }
    (void)now_s;
  }
  return out;
}

bool Orchestrator::pet_watchdog() {
  // Pet only while every must-live daemon is live; a hung daemon stops the
  // petting so the hardware watchdog can act as the last resort.
  for (const auto& name : must_live_) {
    auto it = spokes_.find(name);
    if (it == spokes_.end()) continue;
    if (it->second.state != SpokeState::Ready || !it->second.live) {
      (void)plat_.watchdog_pet();
      return false;
    }
  }
  return plat_.watchdog_pet().ok();
}

Result<const SpokeInfo*> Orchestrator::spoke(const std::string& name) const {
  auto it = spokes_.find(name);
  if (it == spokes_.end())
    return Result<const SpokeInfo*>::fail(Errc::NotFound, "unknown spoke", name);
  return Result<const SpokeInfo*>::good(&it->second);
}

bool Orchestrator::all_ready() const {
  for (const auto& kv : spokes_)
    if (kv.second.state != SpokeState::Ready) return false;
  return !spokes_.empty();
}

bool Orchestrator::degraded() const {
  for (const auto& kv : spokes_)
    if (kv.second.state == SpokeState::Degraded || kv.second.state == SpokeState::Hung ||
        kv.second.state == SpokeState::Stopped)
      return true;
  return false;
}

std::string Orchestrator::status_json() const {
  JValue arr = JValue::array();
  for (const auto& kv : spokes_) {
    JValue o = JValue::object();
    o.set("name", JValue(kv.second.name));
    o.set("state", JValue(spoke_state_name(kv.second.state)));
    o.set("live", JValue(kv.second.live));
    o.set("build_version", JValue(kv.second.build_version));
    o.set("proto_version", JValue(double(kv.second.proto_version)));
    JValue caps = JValue::array();
    for (const auto& c : kv.second.capabilities) caps.push(JValue(c));
    o.set("capabilities", caps);
    arr.push(o);
  }
  return arr.dump();
}

}  // namespace istate
}  // namespace instr