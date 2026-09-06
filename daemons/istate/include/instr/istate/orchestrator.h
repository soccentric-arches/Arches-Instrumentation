// ============================================================================
//  Arches Instrumentation
//
//  Copyright (C) 2026 Soccentric. All rights reserved.
//  Author: Sandesh Ghimire (sandesh@soccentric)
//
//  This file is part of a proprietary product of Soccentric.
//
//  NO WARRANTY:
//  This software is provided "AS IS", without warranty of any kind, either
//  express or implied, including but not limited to the warranties of
//  merchantability, fitness for a particular purpose, and non-infringement.
//  In no event shall Soccentric be liable for any claim, damages, or other
//  liability arising from the use of this software.
//
//  This software may not be used, copied, modified, distributed, or
//  disclosed, in whole or in part, without the express prior written
//  consent of Soccentric.
// ============================================================================
/**
 * @file daemons/istate/include/instr/istate/orchestrator.h
 * @brief orchestration, not supervision (REQ §7.1). Declares readiness
 *
 * order and dependency edges, requests start/stop through systemd, tracks
 * each spoke's operational state, exposes it northbound. systemd owns
 * restart policy; if istate restarts, spokes keep running and re-attach.
 * Liveness-checks each spoke so a hung (not crashed) daemon is detected.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "instr/common/backoff.h"
#include "instr/common/timestamps.h"
#include "instr/platform/platform.h"

namespace instr
{
namespace istate
{

/// @brief Lifecycle state of a managed spoke daemon.
enum class SpokeState
{
    Expected,    ///< Declared but not started.
    Starting,    ///< Start requested.
    Ready,       ///< Registered and responsive.
    Degraded,    ///< Registered but failing liveness.
    Stopped,     ///< Stopped or exited.
    Hung         ///< Running but not answering pings.
};

/// @brief Canonical name of a spoke state.
const char* spoke_state_name(SpokeState);

/// @brief Bookkeeping for one managed spoke.
struct SpokeInfo
{
    std::string name;               ///< Daemon name.
    std::string unit;               ///< systemd unit.
    SpokeState state = SpokeState::Expected;   ///< Lifecycle state.
    bool live = false;              ///< Liveness probe result.
    std::string build_version;      ///< Reported build.
    uint32_t proto_version = 0;     ///< Reported proto schema version.
    std::vector<std::string> capabilities;   ///< Reported capabilities.
    uint64_t last_seen_mono = 0;    ///< Last traffic of any kind.
    // Liveness ping/pong (§3.1, §7.1). last_seen_mono tracks any traffic, which
    // an idle-but-healthy spoke (backup, ota) does not produce — only the pong
    // distinguishes "quiet" from "hung".
    uint64_t last_ping_mono = 0;    ///< Last ping sent.
    uint64_t last_pong_mono = 0;    ///< Last pong received.
    uint64_t ping_nonce = 0;        ///< Outstanding ping nonce.
    uint64_t pongs_received = 0;    ///< Lifetime pong count.
    uint64_t pings_sent = 0;        ///< Lifetime ping count.
};

/// @brief Startup order (§7.1): istate → Faults, monitord, Hardware → Alarms,
/// LogManager, Telemetry → networkd → OTA, Backup → tunnel and HQ session.
///
/// The HQ session is last and never a precondition for local operation.
/// Observation daemons start early so boot-window anomalies are captured.
const std::vector<std::string>& startup_order();

/// @brief Dependency edges: daemon → set it must wait for.
const std::map<std::string, std::vector<std::string>>& dependency_edges();

/// @brief Spoke lifecycle orchestrator: ordering, registration, liveness, watchdog.
class Orchestrator
{
  public:
    /// @brief Bind the orchestrator to the platform and a clock.
    Orchestrator(platform::IPlatform& plat, TimeSource& ts) : plat_(plat), ts_(ts)
    {
    }

    /// @brief Register all spokes in startup order.
    void declare_spokes(const std::vector<std::string>& names);

    /// @brief Start sequence: requests start through systemd in dependency order.
    ///
    /// Returns false when a required unit fails to start (the spoke
    /// re-registers on its own retry; istate keeps operating).
    bool start_all();
    /// @brief Stop in reverse order.
    void stop_all();

    /// @brief Spoke registration handshake outcome (proto mismatch refuses + alarms).
    enum class Reg
    {
        Accepted,        ///< Registered.
        SchemaMismatch,  ///< Proto version refused.
        NameMismatch     ///< Wrong daemon name.
    };
    /// @brief Process a registration handshake.
    /// @param refusal_reason Optional out-text for refusals.
    Reg register_spoke(const std::string& name, const std::string& build_version, uint32_t proto_version, const std::vector<std::string>& capabilities, std::string* refusal_reason = nullptr);
    /// @brief Mark a spoke ready (channel established).
    void mark_ready(const std::string& name);
    /// @brief Mark a spoke stopped.
    void mark_stopped(const std::string& name);
    /// @brief A spoke sent data: it is attached and responsive.
    void note_spoke_activity(const std::string& name);

    /// @brief Install the ping sender (§3.1, §7.1 liveness).
    ///
    /// istate probes each registered spoke; a process that is running but not
    /// responding must not look healthy. Hung → request restart. The probe is
    /// a ping/pong over the spoke's own channel: a liveness-ping command
    /// carrying a nonce, answered by the spoke's transport. A daemon wedged
    /// inside its own event loop stops answering while its process is still
    /// very much alive — which is the case this exists to catch.
    void set_ping_fn(std::function<bool(const std::string& daemon, uint64_t nonce)> f)
    {
        send_ping_ = std::move(f);
    }
    /// @brief A spoke answered a ping.
    void note_pong(const std::string& daemon, uint64_t nonce);
    /// @brief Optional synchronous probe override (tests, and any spoke that offers a cheaper liveness signal).
    void set_probe_fn(const std::string& name, std::function<bool()> f)
    {
        probes_[name] = std::move(f);
    }
    /// @brief One liveness event.
    struct LivenessEvent
    {
        std::string daemon;    ///< Affected daemon.
        std::string event;     ///< hung / restarted / degraded.
    };
    /// @brief Sweep all spokes for hangs; returns events to emit.
    std::vector<LivenessEvent> liveness_sweep(double now_s, double hang_timeout_s);

    /// @brief Watchdog policy: pet only while every must-live daemon is live.
    void set_watchdog_required(const std::vector<std::string>& names)
    {
        must_live_ = names;
    }
    /// @brief Pet the watchdog when safe; false when a must-live daemon is down.
    bool pet_watchdog();

    /// @brief All managed spokes.
    const std::map<std::string, SpokeInfo>& spokes() const
    {
        return spokes_;
    }
    /// @brief Look up one spoke.
    Result<const SpokeInfo*> spoke(const std::string& name) const;
    /// @brief True when every declared spoke is Ready.
    bool all_ready() const;
    /// @brief True when any spoke is Degraded or Hung.
    bool degraded() const;
    /// @brief Render the fleet status as JSON (ctrl "status").
    std::string status_json() const;

    /// @brief Install the event sink (liveness and lifecycle events).
    void set_event_sink(std::function<void(const std::string& kind, const std::string& detail)> f)
    {
        event_sink_ = std::move(f);
    }

  private:
    platform::IPlatform& plat_;       ///< systemd operations (not owned).
    TimeSource& ts_;                   ///< Clock (not owned).
    std::map<std::string, SpokeInfo> spokes_;                 ///< Managed spokes.
    std::map<std::string, std::function<bool()>> probes_;      ///< Per-spoke probe overrides.
    std::function<bool(const std::string&, uint64_t)> send_ping_;   ///< Ping sender.
    uint64_t next_nonce_ = 1;         ///< Ping nonce generator.
    std::vector<std::string> must_live_;    ///< Watchdog-gating daemons.
    std::function<void(const std::string&, const std::string&)> event_sink_;   ///< Event sink.
};

} // namespace istate
} // namespace instr