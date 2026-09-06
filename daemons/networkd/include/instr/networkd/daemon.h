// networkd daemon facade: core state + command table + spoke channel.
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "instr/common/command_table.h"
#include "instr/networkd/domain.h"
#include "instr/platform/platform.h"
#include "instr/transport/transport.h"
#include "networkd.grpc.pb.h"

namespace instr {
namespace networkd {
using namespace domain;
using namespace instr::transport;

class NetworkdCore {
 public:
  NetworkdCore(platform::IPlatform& plat, TimeSource& ts) : plat_(plat), ts_(ts) {}

  // Runtime (non-persistent) plan state — persistent config lives in /pst
  // via istate; _ctrl changes are runtime-only (§3.2).
  Error load_plan(const NetworkPlan& plan);
  const NetworkPlan& plan() const { return plan_; }
  NetworkPlan& mutable_plan() { return plan_; }

  // Applies the current plan through the platform (recorded/dry-run on host).
  Error apply();
  // Tunnel-loss-guarded apply of a new plan (mandatory §7.2).
  TunnelGuard::Result guarded_apply(const NetworkPlan& next, double timeout_s);
  // Simulated reachability hook (tests); production uses tunnel handshake age.
  void set_reachability_fn(std::function<bool()> f) { reachability_ = std::move(f); }
  // Test seam: override the guard clock (monotonic seconds by default).
  void set_clock_fn(std::function<double()> f) { clock_fn_ = std::move(f); }

  RemoteAccessManager& ssh() { return ssh_; }
  void set_tunnel_up(bool up) { tunnel_up_ = up; }
  bool tunnel_up() const { return tunnel_up_; }

  // Time policy passed down from istate; renders daemon configs.
  Error apply_time_policy(const TimePolicy& policy);
  const TimeDaemonConfig& time_daemons() const { return time_daemons_; }

  std::string status_json() const;

  // Wall seconds (best-effort when the clock is unlocked: monotonic fallback).
  int64_t wall_now_s() const {
    int64_t w = ts_.wall_ns() / 1'000'000'000ll;
    return w > 1'500'000'000ll ? w : int64_t(1'700'000'000ll);
  }

  // Events emitted upward to istate (kind, iface, detail).
  void set_event_sink(std::function<void(const std::string& kind, const std::string& detail)> f) {
    event_sink_ = std::move(f);
  }

  platform::IPlatform& platform() { return plat_; }

 private:
  platform::IPlatform& plat_;
  TimeSource& ts_;
  NetworkPlan plan_;
  RemoteAccessManager ssh_;
  TimeDaemonConfig time_daemons_;
  bool tunnel_up_ = false;
  std::function<bool()> reachability_;
  std::function<double()> clock_fn_;
  std::function<void(const std::string&, const std::string&)> event_sink_;
};

CommandTable build_networkd_table(NetworkdCore* core);

// Spoke runner (same shape for all spokes; typed to networkd frames).
class NetworkdSpoke {
 public:
  NetworkdSpoke(NetworkdCore* core, Logger* log, const CommandTable* table,
                const std::string& hub_target, const std::string& build_version);
  void start();
  void stop();
  bool registered() const { return channel_->registered(); }
  void send_event(const networkd::NetworkEvent& ev);

 private:
  NetworkdCore* core_;
  Logger* log_;
  const CommandTable* table_;
  std::unique_ptr<SpokeChannel<networkd::NetworkHub, networkd::NetworkSpokeFrame,
                               networkd::NetworkHubFrame>>
      channel_;
};

}  // namespace networkd
}  // namespace instr