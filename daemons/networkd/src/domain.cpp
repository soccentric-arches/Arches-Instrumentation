#include "instr/networkd/domain.h"

#include <algorithm>
#include <sstream>

namespace instr {
namespace networkd {
namespace domain {

JValue render_plan_json(const NetworkPlan& plan) {
  JValue j = JValue::object();
  JValue ifaces = JValue::array();
  for (const auto& i : plan.ifaces) {
    JValue o = JValue::object();
    o.set("name", JValue(i.name));
    o.set("mode", JValue(i.mode));
    if (i.mode == "static") {
      o.set("address", JValue(i.address));
      o.set("prefix", JValue(double(i.prefix)));
    }
    if (i.mode == "vlan") o.set("vlan_id", JValue(double(i.vlan_id)));
    if (i.mode == "bond") o.set("bond_member", JValue(i.bond_member));
    o.set("priority", JValue(double(i.priority)));
    ifaces.push(o);
  }
  j.set("interfaces", ifaces);
  JValue fw = JValue::array();
  for (const auto& r : plan.firewall) {
    JValue o = JValue::object();
    o.set("action", JValue(r.action));
    o.set("proto", JValue(r.proto));
    o.set("src", JValue(r.src));
    if (r.dst_port) o.set("dst_port", JValue(double(r.dst_port)));
    o.set("comment", JValue(r.comment));
    fw.push(o);
  }
  j.set("firewall", fw);
  JValue pf = JValue::array();
  for (const auto& f : plan.forwards) {
    JValue o = JValue::object();
    o.set("proto", JValue(f.proto));
    o.set("listen_port", JValue(double(f.listen_port)));
    o.set("dest_ip", JValue(f.dest_ip));
    o.set("dest_port", JValue(double(f.dest_port)));
    pf.push(o);
  }
  j.set("port_forwarding", pf);
  j.set("ssh_enabled", JValue(plan.ssh_enabled));
  JValue wg = JValue::object();
  wg.set("endpoint", JValue(plan.wg.hq_endpoint));
  wg.set("device_addr", JValue(plan.wg.device_addr));
  wg.set("hq_addr", JValue(plan.wg.hq_addr));
  wg.set("peer_pubkey", JValue(plan.wg.peer_pubkey));  // HQ's key: not a device secret
  wg.set("listen_port", JValue(double(plan.wg.listen_port)));
  wg.set("up", JValue(plan.wg_up));
  j.set("wireguard", wg);
  JValue tp = JValue::object();
  JValue prio = JValue::array();
  for (const auto& s : plan.time.source_priority) prio.push(JValue(s));
  tp.set("source_priority", prio);
  JValue servers = JValue::array();
  for (const auto& s : plan.time.ntp_servers) servers.push(JValue(s));
  tp.set("ntp_servers", servers);
  tp.set("ptp_domain", JValue(double(plan.time.ptp_domain)));
  tp.set("ptp_profile", JValue(plan.time.ptp_profile));
  JValue consts = JValue::array();
  for (const auto& s : plan.time.gnss_constellations) consts.push(JValue(s));
  tp.set("gnss_constellations", consts);
  j.set("time_policy", tp);
  return j;
}

TunnelGuard::Result TunnelGuard::guarded_change(
    const NetworkPlan& previous, const NetworkPlan& next,
    const std::function<Error(const NetworkPlan&)>& apply,
    const std::function<bool()>& reachability, double timeout_s,
    const std::function<double()>& clock_s) {
  Result r;
  Error e = apply(next);
  if (!e.ok()) {
    r.detail = "apply failed: " + e.str();
    // Re-apply the previous plan so the device keeps its working config.
    Error pe = apply(previous);
    r.reverted = pe.ok();
    return r;
  }
  r.applied = true;
  double deadline = clock_s() + timeout_s;
  // Probe HQ reachability: immediately and until the deadline.
  while (!reachability()) {
    if (clock_s() >= deadline) {
      Error pe = apply(previous);
      r.reverted = pe.ok();
      r.detail = "HQ unreachable within " + std::to_string(timeout_s) +
                 "s; change auto-reverted to the previous plan";
      return r;
    }
    struct timespec ts {0, 10 * 1000 * 1000};
    nanosleep(&ts, nullptr);
  }
  r.detail = "applied; HQ reachable";
  return r;
}

Result<SshWindow> RemoteAccessManager::open(const std::string& operator_name, uint64_t ttl_s,
                                            int64_t now_unix_s) {
  if (operator_name.empty())
    return Result<SshWindow>::fail(Errc::InvalidArgument,
                                   "remote access must be attributed to a named operator", "");
  if (ttl_s == 0 || ttl_s > 24ull * 3600)
    return Result<SshWindow>::fail(Errc::InvalidArgument, "ttl must be 1s..24h", "");
  SshWindow w;
  w.operator_name = operator_name;
  w.opened_unix_s = now_unix_s;
  w.expires_unix_s = now_unix_s + int64_t(ttl_s);
  w.active = true;
  windows_.push_back(w);
  return Result<SshWindow>::good(w);
}

std::vector<SshWindow> RemoteAccessManager::sweep(int64_t now_unix_s) {
  std::vector<SshWindow> expired;
  for (auto& w : windows_) {
    if (w.active && now_unix_s >= w.expires_unix_s) {
      w.active = false;
      expired.push_back(w);
    }
  }
  return expired;
}

std::vector<SshWindow> RemoteAccessManager::active(int64_t now_unix_s) const {
  std::vector<SshWindow> out;
  for (const auto& w : windows_)
    if (w.active && now_unix_s < w.expires_unix_s) out.push_back(w);
  return out;
}

bool RemoteAccessManager::any_active(int64_t now_unix_s) const {
  return !active(now_unix_s).empty();
}

void RemoteAccessManager::close_all() {
  for (auto& w : windows_) w.active = false;
}

TimeDaemonConfig render_time_daemons(const TimePolicy& policy) {
  TimeDaemonConfig c;
  // chrony: NTP servers (fallback source).
  std::ostringstream ch;
  ch << "# generated by networkd from istate time policy\n";
  for (const auto& s : policy.ntp_servers) ch << "server " << s << " iburst\n";
  ch << "rtcsync\nmakestep 1.0 3\n";
  // GNSS takes priority when present: gpsd feeds chrony via SHM.
  bool has_gnss = false;
  for (const auto& s : policy.source_priority)
    if (s == "gnss") has_gnss = true;
  if (has_gnss) ch << "refclock SHM 0 offset 0.5 delay 0.2 refid GNSS\n";
  c.chrony_conf = ch.str();
  // ptp4l: domain and profile from policy.
  std::ostringstream ptp;
  ptp << "-f /etc/instrumentation/ptp4l.conf -i $IFACE -d " << policy.ptp_domain;
  if (!policy.ptp_profile.empty()) ptp << " -S --profile=" << policy.ptp_profile;
  c.ptp4l_args = ptp.str();
  c.phc2sys_args = "-a -r";
  std::ostringstream gps;
  bool gnss = false;
  for (const auto& s : policy.source_priority)
    if (s == "gnss") gnss = true;
  if (gnss) {
    gps << "-n -S /var/run/gpsd.sock";
    for (const auto& cst : policy.gnss_constellations) (void)cst;  // gpsd auto-configures
  }
  c.gpsd_args = gps.str();
  return c;
}

std::vector<std::string> ConnectivityRanker::rank(const std::vector<IfaceConfig>& cfgs,
                                                  const std::vector<Reachability>& probes) {
  std::map<std::string, uint32_t> prio;
  for (const auto& c : cfgs) prio[c.name] = c.priority;
  std::vector<std::string> out;
  std::map<std::string, bool> reach;
  for (const auto& p : probes) reach[p.iface] = p.reachable;
  std::vector<std::pair<uint32_t, std::string>> ranked;
  for (const auto& kv : prio) {
    auto rit = reach.find(kv.first);
    bool r = (rit != reach.end()) ? rit->second : false;
    // Unreachable interfaces rank last: add a large penalty.
    uint32_t key = kv.second + (r ? 0 : 100000);
    ranked.emplace_back(key, kv.first);
  }
  std::sort(ranked.begin(), ranked.end());
  for (const auto& kv : ranked) out.push_back(kv.second);
  return out;
}

std::string plan_status_json(const NetworkPlan& plan, const std::vector<SshWindow>& windows,
                             bool tunnel_up) {
  JValue j = render_plan_json(plan);
  j.set("tunnel_up", JValue(tunnel_up));
  JValue arr = JValue::array();
  for (const auto& w : windows) {
    JValue o = JValue::object();
    o.set("operator", JValue(w.operator_name));
    o.set("opened_unix_s", JValue(double(w.opened_unix_s)));
    o.set("expires_unix_s", JValue(double(w.expires_unix_s)));
    o.set("active", JValue(w.active));
    arr.push(o);
  }
  j.set("ssh_windows", arr);
  return j.dump();
}

}  // namespace domain
}  // namespace networkd
}  // namespace instr