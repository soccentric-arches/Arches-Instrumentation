#include "instr/networkd/daemon.h"

#include <sstream>

namespace instr {
namespace networkd {

Error NetworkdCore::load_plan(const NetworkPlan& plan) {
  plan_ = plan;
  return Error::none();
}

Error NetworkdCore::apply() {
  JValue j = render_plan_json(plan_);
  Error e = plat_.apply_network_plan(j.dump());
  if (!e.ok()) return e;
  if (event_sink_) event_sink_("plan_applied", "{}");
  return Error::none();
}

TunnelGuard::Result NetworkdCore::guarded_apply(const NetworkPlan& next, double timeout_s) {
  NetworkPlan previous = plan_;
  auto reach = reachability_ ? reachability_ : [this] { return plat_.hq_reachable(); };
  auto clock = clock_fn_ ? clock_fn_ : [this] { return double(ts_.mono_ns()) / 1e9; };
  auto result = TunnelGuard::guarded_change(
      previous, next,
      [this](const NetworkPlan& p) {
        plan_ = p;
        return apply();
      },
      reach, timeout_s, clock);
  if (result.reverted) {
    if (event_sink_) event_sink_("guard_revert", "{\"reason\":\"hq-unreachable\"}");
  }
  return result;
}

Error NetworkdCore::apply_time_policy(const TimePolicy& policy) {
  plan_.time = policy;
  time_daemons_ = render_time_daemons(policy);
  JValue j = JValue::object();
  j.set("kind", JValue("time_policy"));
  j.set("chrony_conf", JValue(time_daemons_.chrony_conf));
  j.set("ptp4l_args", JValue(time_daemons_.ptp4l_args));
  j.set("phc2sys_args", JValue(time_daemons_.phc2sys_args));
  j.set("gpsd_args", JValue(time_daemons_.gpsd_args));
  Error e = plat_.apply_network_plan(j.dump());
  if (!e.ok()) return e;
  if (event_sink_) event_sink_("time_policy_applied", "{}");
  return Error::none();
}

std::string NetworkdCore::status_json() const {
  int64_t now = ts_.wall_ns() / 1'000'000'000ll;
  std::vector<SshWindow> active = ssh_.active(now);
  return plan_status_json(plan_, active, tunnel_up_);
}

// ---------------- command table (full coverage; runtime-only) ----------------

CommandTable build_networkd_table(NetworkdCore* core) {
  CommandTable t;
  t.add({"status", "full network status (plan, tunnel, ssh windows)", {},
         [core](const std::string&) { return CommandResult::good(core->status_json()); }});
  t.add({"ifaces", "list configured interfaces",
         {{"verbose", false, "include mode details (true/false)"}},
         [core](const std::string& args) {
           auto a = args_object(args);
           bool verbose = a.ok() && (*a).get_bool("verbose");
           JValue arr = JValue::array();
           for (const auto& i : core->plan().ifaces) {
             JValue o = JValue::object();
             o.set("name", JValue(i.name));
             o.set("mode", JValue(i.mode));
             o.set("priority", JValue(double(i.priority)));
             if (verbose) {
               o.set("address", JValue(i.address));
               o.set("vlan_id", JValue(double(i.vlan_id)));
             }
             arr.push(o);
           }
           return CommandResult::good(arr.dump());
         }});
  t.add({"apply-plan", "apply the current plan through the platform", {},
         [core](const std::string&) {
           Error e = core->apply();
           return e.ok() ? CommandResult::good(std::string("{\"applied\":true}"))
                         : CommandResult::from_error(e);
         }});
  t.add({"firewall-get", "list firewall rules", {},
         [core](const std::string&) {
           JValue arr = JValue::array();
           for (const auto& r : core->plan().firewall) {
             JValue o = JValue::object();
             o.set("action", JValue(r.action));
             o.set("proto", JValue(r.proto));
             o.set("src", JValue(r.src));
             o.set("dst_port", JValue(double(r.dst_port)));
             o.set("comment", JValue(r.comment));
             arr.push(o);
           }
           return CommandResult::good(arr.dump());
         }});
  t.add({"firewall-add", "add a runtime firewall rule (persistent config via istate)",
         {{"action", true, "allow|deny"}, {"proto", true, "tcp|udp|icmp|any"},
          {"src", false, "source cidr (any)"},
          {"dst_port", false, "destination port (0=any)"},
          {"comment", false, "rule comment"}},
         [core](const std::string& args) {
           auto a = args_object(args);
           if (!a.ok()) return CommandResult::from_error(a.error);
           FirewallRule r;
           r.action = (*a).get_string("action");
           r.proto = (*a).get_string("proto", "any");
           r.src = (*a).get_string("src", "any");
           r.dst_port = uint32_t((*a).get_number("dst_port", 0.0));
           r.comment = (*a).get_string("comment");
           if (r.action != "allow" && r.action != "deny")
             return CommandResult::err(Errc::InvalidArgument, "action must be allow|deny");
           core->mutable_plan().firewall.push_back(r);
           return CommandResult::good(std::string("{\"added\":true}"));
         }});
  t.add({"ssh-open", "open a time-boxed, attributed inbound SSH window (alarm-visible)",
         {{"operator", true, "named operator for attribution"}, {"ttl_s", true, "window TTL 1s..24h"}},
         [core](const std::string& args) {
           auto a = args_object(args);
           if (!a.ok()) return CommandResult::from_error(a.error);
           std::string op = (*a).get_string("operator");
           uint64_t ttl = uint64_t((*a).get_number("ttl_s", 0));
           auto w = core->ssh().open(op, ttl, core->wall_now_s());
           if (!w.ok()) return CommandResult::from_error(w.error);
           JValue o = JValue::object();
           o.set("operator", JValue(w->operator_name));
           o.set("expires_unix_s", JValue(double(w->expires_unix_s)));
           return CommandResult::good(o.dump());
         }});
  t.add({"ssh-close", "close all inbound SSH windows", {},
         [core](const std::string&) {
           core->ssh().close_all();
           return CommandResult::good(std::string("{\"closed\":true}"));
         }});
  t.add({"ssh-list", "list SSH windows", {},
         [core](const std::string&) {
           JValue arr = JValue::array();
           for (const auto& w : core->ssh().active(core->wall_now_s())) {
             JValue o = JValue::object();
             o.set("operator", JValue(w.operator_name));
             o.set("expires_unix_s", JValue(double(w.expires_unix_s)));
             arr.push(o);
           }
           return CommandResult::good(arr.dump());
         }});
  t.add({"tunnel-status", "WireGuard tunnel state", {},
         [core](const std::string&) {
           JValue o = JValue::object();
           o.set("up", JValue(core->tunnel_up()));
           o.set("endpoint", JValue(core->plan().wg.hq_endpoint));
           return CommandResult::good(o.dump());
         }});
  t.add({"time-policy-render", "render time daemon configs from the current policy", {},
         [core](const std::string&) {
           JValue o = JValue::object();
           o.set("chrony_conf", JValue(core->time_daemons().chrony_conf));
           o.set("ptp4l_args", JValue(core->time_daemons().ptp4l_args));
           o.set("phc2sys_args", JValue(core->time_daemons().phc2sys_args));
           o.set("gpsd_args", JValue(core->time_daemons().gpsd_args));
           return CommandResult::good(o.dump());
         }});
  t.add({"failover-rank", "rank interfaces by priority and reachability", {},
         [core](const std::string&) {
           std::vector<Reachability> probes;
           for (const auto& i : core->platform().interfaces()) {
             Reachability r;
             r.iface = i.name;
             r.reachable = i.link;
             probes.push_back(r);
           }
           auto ranked = ConnectivityRanker::rank(core->plan().ifaces, probes);
           JValue arr = JValue::array();
           for (const auto& name : ranked) arr.push(JValue(name));
           return CommandResult::good(arr.dump());
         }});
  return t;
}

// ---------------- spoke ----------------

NetworkdSpoke::NetworkdSpoke(NetworkdCore* core, Logger* log, const CommandTable* table,
                             const std::string& hub_target, const std::string& build_version)
    : core_(core), log_(log), table_(table) {
  SpokeChannel<networkd::NetworkHub, networkd::NetworkSpokeFrame,
               networkd::NetworkHubFrame>::Options o;
  o.target = hub_target;
  o.daemon = "networkd";
  o.build_version = build_version;
  o.capabilities = {"wired", "wireless", "firewall", "tunnel", "time-daemons"};
  channel_ = std::make_unique<
      SpokeChannel<networkd::NetworkHub, networkd::NetworkSpokeFrame,
                   networkd::NetworkHubFrame>>(std::move(o), *log);
  channel_->set_hub_handler([this](const networkd::NetworkHubFrame& hf) {
    if (hf.has_command()) {
      common::Ack ack = dispatch_to_ack(table_, hf.command());
      channel_->send_ack(ack);
    }
  });
  core_->set_event_sink([this](const std::string& kind, const std::string& detail) {
    networkd::NetworkEvent ev;
    ev.set_kind(kind);
    ev.set_detail_json(detail);
    send_event(ev);
  });
}

void NetworkdSpoke::start() { channel_->start(); }
void NetworkdSpoke::stop() { channel_->stop(); }

void NetworkdSpoke::send_event(const networkd::NetworkEvent& ev) {
  channel_->enqueue([ev](networkd::NetworkSpokeFrame& f) { *f.mutable_event() = ev; });
}

}  // namespace networkd
}  // namespace instr