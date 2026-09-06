// networkd — owns everything network-adjacent (REQ §7.2, R8). Domain logic:
// plans, firewall, remote-access windows, tunnel guard, time daemons, and
// failover ranking. Pure classes; the platform applies rendered plans.
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "instr/common/error.h"
#include "instr/common/json.h"
#include "instr/common/timestamps.h"

namespace instr {
namespace networkd {
namespace domain {

// ---- configuration model (persisted by istate in /pst/system/networkd) ----
struct IfaceConfig {
  std::string name;
  std::string mode;        // dhcp | static | vlan | bond | unused
  std::string address;     // static
  uint32_t prefix = 24;
  uint32_t vlan_id = 0;
  std::string bond_member; // bond parent iface
  uint32_t priority = 100; // failover ranking (lower = preferred)
};
struct FirewallRule {
  std::string action;      // allow | deny
  std::string proto;       // tcp | udp | icmp | any
  std::string src;         // cidr or any
  uint32_t dst_port = 0;   // 0 = any
  std::string comment;
};
struct PortForward {
  std::string proto;
  uint32_t listen_port = 0;
  std::string dest_ip;
  uint32_t dest_port = 0;
};
struct SshWindow {
  std::string operator_name;
  int64_t opened_unix_s = 0;
  int64_t expires_unix_s = 0;
  bool active = true;
};
struct TimePolicy {
  std::vector<std::string> source_priority = {"gnss", "ptp", "ntp"};  // locked order
  std::vector<std::string> ntp_servers;
  uint32_t ptp_domain = 0;
  std::string ptp_profile;             // e.g. "g8275-1"
  std::vector<std::string> gnss_constellations = {"gps", "galileo", "glonass", "beidou"};
};
struct WgConfig {
  std::string hq_endpoint;
  std::string device_addr;
  std::string hq_addr;
  std::string peer_pubkey;   // HQ's public key; ours never leaves the device
  uint32_t listen_port = 51820;
};

struct NetworkPlan {
  std::vector<IfaceConfig> ifaces;
  std::vector<FirewallRule> firewall;
  std::vector<PortForward> forwards;
  bool ssh_enabled = false;   // inbound SSH off by default (§7.2)
  WgConfig wg;
  bool wg_up = false;
  TimePolicy time;
};

// Renders a plan to the JSON shape handed to platform.apply_network_plan.
JValue render_plan_json(const NetworkPlan& plan);

// ---- tunnel-loss guard (mandatory, §7.2): any change that kills the tunnel
// auto-reverts if HQ is unreachable within the timeout. ----
class TunnelGuard {
 public:
  struct Result {
    bool applied = false;
    bool reverted = false;
    std::string detail;
  };
  // apply: applies the new plan; reachability: can HQ be reached now?
  // clock_s: monotonic seconds. If HQ unreachable after timeout_s, re-apply
  // the previous plan (revert).
  static Result guarded_change(const NetworkPlan& previous, const NetworkPlan& next,
                               const std::function<Error(const NetworkPlan&)>& apply,
                               const std::function<bool()>& reachability, double timeout_s,
                               const std::function<double()>& clock_s);
};

// ---- remote access: off by default, time-boxed, attributed, audited ----
class RemoteAccessManager {
 public:
  // Opens a window; expires automatically. Logged as an alarm the customer
  // can see (netd_remote_access_opened).
  Result<SshWindow> open(const std::string& operator_name, uint64_t ttl_s, int64_t now_unix_s);
  // Expires windows whose TTL elapsed; returns the expired ones.
  std::vector<SshWindow> sweep(int64_t now_unix_s);
  std::vector<SshWindow> active(int64_t now_unix_s) const;
  bool any_active(int64_t now_unix_s) const;
  void close_all();

 private:
  std::vector<SshWindow> windows_;
};

// ---- time daemon configuration rendering (networkd owns chrony, ptp4l,
// phc2sys, gpsd; istate owns the policy, §R8) ----
struct TimeDaemonConfig {
  std::string chrony_conf;
  std::string ptp4l_args;
  std::string phc2sys_args;
  std::string gpsd_args;
};
TimeDaemonConfig render_time_daemons(const TimePolicy& policy);

// ---- connectivity / failover ranking ----
struct Reachability {
  std::string iface;
  bool reachable = false;
  uint64_t rtt_ms = 0;
};
class ConnectivityRanker {
 public:
  // Rank reachable interfaces by configured priority, unreachable last.
  static std::vector<std::string> rank(const std::vector<IfaceConfig>& cfgs,
                                       const std::vector<Reachability>& probes);
};

// Effective-state snapshot for status reporting.
std::string plan_status_json(const NetworkPlan& plan, const std::vector<SshWindow>& windows,
                             bool tunnel_up);

}  // namespace domain
}  // namespace networkd
}  // namespace instr