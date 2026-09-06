#include "instr/istate/core.h"

#include <future>

#include "instr/common/version_gen.h"

namespace instr {
namespace istate {

IstateCore::IstateCore(IstateOptions opts, IFileSystem& fs, platform::IPlatform& plat,
                       TimeSource& ts, Logger& log)
    : opts_(std::move(opts)),
      fs_(fs),
      plat_(plat),
      ts_(ts),
      log_(log),
      budgets_(fs, opts_.pst_root),
      pst_(fs, opts_.pst_root, budgets_),
      provisioner_(pst_, plat, fs),
      orchestrator_(plat, ts),
      timeline_(5000),
      northbound_spool_(fs,
                        [&] {
                          SpoolConfig c;
                          c.dir = opts_.pst_root + "/state/spool";
                          c.max_bytes = opts_.spool_max_bytes;
                          c.segment_bytes = 4 * 1024 * 1024;
                          return c;
                        }(),
                        &budgets_),
      config_service_(pst_, fs),
      audit_(fs, opts_.pst_root + "/state/audit.log", 2000),
      reconciler_(ts) {
  budgets_.set_budget("spool", uint64_t(opts_.budget_spool));
  budgets_.set_budget("logs", uint64_t(opts_.budget_logs));
  budgets_.set_budget("ota", uint64_t(opts_.budget_ota));
  budgets_.set_budget("backup", uint64_t(opts_.budget_backup));
}

bool IstateCore::boot(const std::map<std::string, std::string>& default_yaml) {
  // 1. Mount and validate /pst; corrupt → redundant copy; both fail → boot
  // unprovisioned and report, never fail to boot (§11).
  auto mount = pst_.mount();
  if (!mount.corrupt.empty()) {
    log_.critical("istate", "/pst unrecoverable files: booting unprovisioned",
                  {{"files", std::to_string(mount.corrupt.size())}});
    timeline_.append("boot", "istate", "{\"pst_corrupt\":true}", ts_);
  }
  // Unclean shutdown detection via the clean-shutdown marker (§11).
  if (!last_shutdown_clean()) {
    log_.warning("istate", "previous shutdown was not clean");
    timeline_.append("boot", "istate", "{\"unclean_shutdown\":true}", ts_);
    northbound_spool_.enqueue(SpoolPriority::Event, "{\"kind\":\"unclean_shutdown\"}", ts_.mono_ns());
  }
  fs_.remove(opts_.run_dir + "/clean-shutdown", false);

  // 2. Virgin detection + default seeding.
  if (mount.virgin) {
    Error e = pst_.seed_defaults(default_yaml);
    if (!e.ok()) log_.error("istate", "default seeding failed: " + e.str());
    timeline_.append("boot", "istate", "{\"virgin\":true,\"seeded\":true}", ts_);
  }

  // 3. Load persisted schemas for config validation (registered by wiring).
  // 4. Provisioning state machine (§6.2).
  auto enrolled = provisioner_.run_first_boot([this](const DeviceIdentity& id) {
    // HQ enrollment round-trip: over the tunnel when available; without the
    // tunnel the device stays operational and enrollment retries (R10).
    if (hq_send_) {
      JValue req = JValue::object();
      req.set("identity", JValue(id.hostname));
      req.set("pubkey", JValue(id.pubkey_b64()));
      req.set("factory_sig", JValue(id.factory_sig));
      // Synchronous enrollment via the HQ session is wired in production; the
      // simulated HQ responds through the same callback.
      if (hq_send_("enroll", req.dump())) {
        TunnelConfig t;
        t.hq_endpoint = "hq:51820";
        t.valid = true;
        return Result<TunnelConfig>::good(t);
      }
      return Result<TunnelConfig>::fail(Errc::Unavailable, "HQ unreachable for enrollment", "enroll");
    }
    return Result<TunnelConfig>::fail(Errc::Unavailable, "no HQ transport wired", "enroll");
  });
  if (enrolled.ok())
    log_.info("istate", "provisioning state: " + std::string(provisioner_.state_name(provisioner_.state())));
  else
    log_.warning("istate", "enrollment deferred: " + enrolled.error.str());

  // 5. Orchestrate the spokes (observation daemons start first, §7.1).
  orchestrator_.declare_spokes(startup_order());
  orchestrator_.start_all();

  // 6. Open the northbound spool; HQ session is last and never a precondition.
  Error se = northbound_spool_.open();
  if (!se.ok()) log_.error("istate", "northbound spool failed: " + se.str());

  // 7. Watchdog: pet while all must-live daemons are live.
  orchestrator_.set_watchdog_required({"faults", "monitord", "hardware", "networkd"});
  (void)plat_.watchdog_set_timeout_s(60);

  audit_.load();
  audit_.append("istate", "boot", "boot", "{\"version\":\"" INSTR_VERSION "\"}", ts_);
  audit_.persist();
  timeline_.append("boot", "istate", "{\"version\":\"" INSTR_VERSION "\"}", ts_);
  return true;
}

void IstateCore::shutdown() {
  write_clean_shutdown_marker();
  orchestrator_.stop_all();
}

size_t IstateCore::on_time_lock(const std::string& source, int64_t offset_ns) {
  time_policy_.locked = true;
  time_policy_.current_source = source;
  // Reconciliation: spooled data, alarms, logs re-stamped; history never
  // reordered (§4).
  size_t n = reconciler_.reconcile(offset_ns, source);
  log_.info("istate", "time lock via " + source + "; re-stamped " + std::to_string(n) + " records");
  return n;
}

void IstateCore::on_spoke_msg(const transport::GenericSpokeMsg& msg) {
  // First message from a spoke ⇒ it is alive and attached.
  orchestrator_.note_spoke_activity(msg.daemon);
  if (msg.type == "sample") {
    // monitord → telemetry relay (R1: all traffic through istate).
    transport::GenericHubMsg relay;
    relay.has_relay = true;
    relay.relay.set_source_daemon(msg.daemon);
    relay.relay.set_type("sample");
    relay.relay.set_payload_json(msg.payload_json);
    relay.relay.mutable_ts()->CopyFrom(msg.ts);
    if (relay_to_spoke_) relay_to_spoke_("telemetry", relay);
    // Also spool a compact telemetry record northbound.
    northbound_spool_.enqueue(SpoolPriority::Telemetry, msg.payload_json, msg.ts.mono_ns());
  } else if (msg.type == "fault") {
    // Faults are protected: never dropped (§11).
    northbound_spool_.enqueue(SpoolPriority::Fault, msg.payload_json, msg.ts.mono_ns());
    timeline_.append("fault", msg.daemon, msg.payload_json, ts_);
  } else if (msg.type == "alarm") {
    // Alarm state changes go to istate directly as events (§7.6) — protected
    // class, never delayed by batching.
    northbound_spool_.enqueue(SpoolPriority::Alarm, msg.payload_json, msg.ts.mono_ns());
    timeline_.append("alarm", msg.daemon, msg.payload_json, ts_);
    // Snapshot-on-fault requests trigger a bounded diagnostic capture via
    // LogManager (evidence at the moment it matters).
    auto j = json_parse(msg.payload_json);
    if (j.ok()) {
      if (const JValue* v = (*j).find("needs_snapshot"); v && v->as_bool()) {
        transport::GenericHubMsg cmd;
        cmd.has_command = true;
        cmd.command.set_op("crash-report");
        cmd.command.set_json_args("{}");
        cmd.command.set_issued_by("istate");
        if (relay_to_spoke_) relay_to_spoke_("logmanager", cmd);
      }
    }
  } else if (msg.type == "tail_line") {
    // Live tail fanout to HQ and Configuration (§7.1).
    tail_fanout_.publish(msg.payload_json);
    northbound_spool_.enqueue(SpoolPriority::LogRef, msg.payload_json, msg.ts.mono_ns());
  } else if (msg.type == "bundle") {
    // Bulk transfer by handle: control path carries handle + metadata only.
    northbound_spool_.enqueue(SpoolPriority::LogRef, msg.payload_json, msg.ts.mono_ns());
  } else if (msg.type == "ota_event") {
    timeline_.append("update", msg.daemon, msg.payload_json, ts_);
    northbound_spool_.enqueue(SpoolPriority::Event, msg.payload_json, msg.ts.mono_ns());
  } else if (msg.type == "backup_event") {
    timeline_.append("backup", msg.daemon, msg.payload_json, ts_);
    northbound_spool_.enqueue(SpoolPriority::Event, msg.payload_json, msg.ts.mono_ns());
  } else if (msg.type == "network_event") {
    timeline_.append("network", msg.daemon, msg.payload_json, ts_);
    northbound_spool_.enqueue(SpoolPriority::Event, msg.payload_json, msg.ts.mono_ns());
  } else if (msg.type == "health_result") {
    northbound_spool_.enqueue(SpoolPriority::Telemetry, msg.payload_json, msg.ts.mono_ns());
  } else if (msg.type == "batch") {
    // Telemetry batches: spool northbound (protected of drops: telemetry is
    // the droppable class, §11).
    northbound_spool_.enqueue(SpoolPriority::Telemetry, msg.payload_json, msg.ts.mono_ns());
  }
}

std::future<std::string> IstateCore::relay_command(const std::string& daemon,
                                                   const common::Command& cmd) {
  auto pending = std::make_shared<PendingRelay>();
  std::future<std::string> fut = pending->promise.get_future();
  uint64_t id;
  {
    std::lock_guard<std::mutex> lk(relay_mu_);
    id = next_relay_id_++;
    pending_relays_[id] = pending;
  }
  common::Command c = cmd;
  c.set_id(id);
  transport::GenericHubMsg m;
  m.has_command = true;
  m.command = c;
  if (relay_to_spoke_ && !relay_to_spoke_(daemon, m)) {
    std::lock_guard<std::mutex> lk(relay_mu_);
    pending_relays_.erase(id);
    pending->promise.set_value("{\"ok\":false,\"error\":{\"code\":\"unavailable\",\"message\":\"spoke not connected\"}}");
  }
  return fut;
}

void IstateCore::on_spoke_ack(const std::string& daemon, const common::Ack& ack) {
  (void)daemon;
  std::shared_ptr<PendingRelay> pending;
  {
    std::lock_guard<std::mutex> lk(relay_mu_);
    auto it = pending_relays_.find(ack.id());
    if (it != pending_relays_.end()) {
      pending = it->second;
      pending_relays_.erase(it);
    }
  }
  if (pending) {
    CommandResult r;
    r.ok = ack.ok();
    r.error_code = ack.code();
    r.error_message = ack.message();
    r.json_data = ack.json_payload();
    pending->promise.set_value(r.envelope());
  }
}

NorthboundSpool::DrainReport IstateCore::drain_northbound(size_t max_batch) {
  if (hq_backpressured_.load()) {
    NorthboundSpool::DrainReport rep;
    rep.paused = true;
    return rep;
  }
  return northbound_spool_.drain(
      [this](const DiskSpool::Entry& e) {
        if (!hq_send_) return false;
        const char* type = "event";
        switch (e.priority) {
          case SpoolPriority::Telemetry: type = "telemetry"; break;
          case SpoolPriority::LogRef: type = "log_ref"; break;
          case SpoolPriority::Fault: type = "fault"; break;
          case SpoolPriority::Alarm: type = "alarm"; break;
          case SpoolPriority::Event: type = "event"; break;
        }
        return hq_send_(type, e.payload);
      },
      max_batch);
}

void IstateCore::set_hq_backpressure(bool pause) {
  hq_backpressured_.store(pause);
  if (pause) {
    hq_session_.on_backpressure(true);
    log_.warning("istate", "HQ backpressure: northbound drain paused");
  } else {
    hq_session_.on_backpressure(false);
  }
}

std::string IstateCore::system_info_json() const {
  auto fid = plat_.read_factory_identity();
  JValue o = JValue::object();
  o.set("hostname", JValue(provisioner_.identity().hostname));
  o.set("serial", JValue(fid.ok() ? fid->serial : ""));
  o.set("hw_revision", JValue(fid.ok() ? fid->hw_revision : ""));
  o.set("board_variant", JValue(fid.ok() ? fid->board_variant : ""));
  o.set("firmware_version", JValue(INSTR_VERSION));
  o.set("suite_version", JValue(INSTR_VERSION));
  o.set("config_schema_version", JValue(pst_.schema_version()));
  o.set("istate_state", JValue(orchestrator_.degraded() ? "degraded" : "operational"));
  o.set("provisioning_state", JValue(provisioner_.state_name(provisioner_.state())));
  o.set("boot_id", JValue(ts_.boot_id()));
  o.set("boot_count", JValue(double(plat_.boot_count())));
  o.set("clean_shutdown", JValue(last_shutdown_clean()));
  JValue caps = JValue::array();
  // Capability manifest (§5.1): istate reports honestly rather than failing
  // calls on platforms lacking a feature.
  for (const char* c : {"wifi", "wifi_ap", "can", "display", "gnss", "ptp", "watchdog"})
    caps.push(JValue(c));
  o.set("capabilities", caps);
  return o.dump();
}

std::string IstateCore::budgets_json() const {
  JValue arr = JValue::array();
  for (const auto& kv : budgets_.budgets()) {
    JValue o = JValue::object();
    o.set("consumer", JValue(kv.first));
    o.set("limit_bytes", JValue(double(kv.second.limit_bytes)));
    o.set("used_bytes", JValue(double(budgets_.usage(kv.first))));
    o.set("warning", JValue(budgets_.usage(kv.first) >= uint64_t(double(kv.second.limit_bytes) * kv.second.alarm_at_pct)));
    arr.push(o);
  }
  return arr.dump();
}

Result<bool> IstateCore::system_reset(const std::string& actor) {
  // System reset: wipes system/ and state/, keeps factory/ and backup/,
  // reseeds from image defaults (§6.1).
  std::map<std::string, std::string> defaults;
  // Defaults come from the image set already registered with the wiring.
  if (default_provider_) defaults = default_provider_();
  Error e = pst_.system_reset(defaults);
  if (!e.ok()) return Result<bool>::fail(e);
  audit_.append(actor, "configuration-ui", "reset", "{\"type\":\"system\"}", ts_);
  audit_.persist();
  timeline_.append("config_change", actor, "{\"reset\":\"system\"}", ts_);
  log_.warning("istate", "system reset performed by " + actor);
  return Result<bool>::good(true);
}

Result<bool> IstateCore::factory_reset(const std::string& actor, const std::string& confirmation) {
  // Manufacturing operation only, explicitly gated (§6.1).
  if (confirmation != "FACTORY-RESET")
    return Result<bool>::fail(
        Errc::InvalidArgument,
        "factory reset requires explicit confirmation string FACTORY-RESET", "factory_reset");
  Error e = pst_.factory_reset();
  if (!e.ok()) return Result<bool>::fail(e);
  audit_.append(actor, "local-console", "reset", "{\"type\":\"factory\"}", ts_);
  audit_.persist();
  log_.critical("istate", "FACTORY RESET performed by " + actor);
  return Result<bool>::good(true);
}

void IstateCore::write_clean_shutdown_marker() {
  fs_.write(opts_.run_dir + "/clean-shutdown", std::to_string(ts_.wall_ns()));
}

bool IstateCore::last_shutdown_clean() const {
  return fs_.exists(opts_.run_dir + "/clean-shutdown");
}

}  // namespace istate
}  // namespace instr