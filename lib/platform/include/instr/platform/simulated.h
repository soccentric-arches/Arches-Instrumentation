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
 * @file lib/platform/include/instr/platform/simulated.h
 * @brief simulated platform (REQ §5)
 *
 * Complete enough to run the entire suite on a build host with no hardware;
 * the integration harness and fault-injection tests depend on it. All state
 * is in-memory and scriptable, with injectors for every §11 failure mode.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "instr/platform/platform.h"

namespace instr
{
namespace platform
{

/// @brief Scripted in-memory platform: every board behaviour is test-controlled.
///
/// Backs the whole-suite simulated mode (REQ §5) and unit tests (§12 fault
/// injection).
class SimulatedPlatform : public IPlatform
{
  public:
    /// @brief Construct a fresh simulated board with sane defaults.
    SimulatedPlatform();

    /// @brief Script the factory identity returned to the daemons.
    void set_factory_identity(const FactoryIdentity& id);
    /// @brief Script entropy availability.
    void set_entropy_available(bool v)
    {
        entropy_ = v;
    }
    void inject_storage_device(const StorageDevice& d);
    void set_thermal(const std::string& id, const std::string& label, double c);
    void set_power(const std::string& id, const std::string& kind, double pct, bool charging);
    void inject_iface(const IfaceInfo& i);
    void set_time_locked(bool v)
    {
        time_locked_ = v;
    }
    void set_hq_reachable(bool v)
    {
        hq_reachable_ = v;
    }
    void set_mem(const MemInfo& m)
    {
        mem_ = m;
    }
    /// @brief Script the CPU snapshot.
    void set_cpu(const CpuSnapshot& c)
    {
        cpu_ = c;
    }
    /// @brief Script the load average.
    void set_load(double l)
    {
        load_ = l;
    }
    /// @brief Script the process count.
    void set_processes(uint32_t n)
    {
        processes_ = n;
    }
    void set_uptime_s(uint64_t s)
    {
        uptime_ = s;
    }
    /// @brief Script the boot count.
    void set_boot_count(uint32_t n)
    {
        boot_count_ = n;
    }
    /// @brief Script the last-shutdown-clean state.
    void set_last_shutdown_clean(bool v)
    {
        clean_shutdown_ = v;
    }

    /// @brief Fault injection (REQ §12): watchdog failures.
    void fail_watchdog(bool v)
    {
        fail_watchdog_ = v;
    }
    /// @brief Fault injection: GPIO failures.
    void fail_gpio(bool v)
    {
        fail_gpio_ = v;
    }
    /// @brief Fault injection: USB reset failures.
    void fail_usb(bool v)
    {
        fail_usb_ = v;
    }
    /// @brief Fault injection: CAN failures.
    void fail_can(bool v)
    {
        fail_can_ = v;
    }
    /// @brief Fault injection: display failures.
    void fail_display(bool v)
    {
        fail_display_ = v;
    }
    /// @brief Fault injection: unit start failures.
    void fail_unit_start(bool v)
    {
        fail_unit_start_ = v;
    }
    /// @brief Fault injection: boot-slot operation failures.
    void fail_boot_slot_ops(bool v)
    {
        fail_boot_slots_ = v;
    }
    /// @brief Replace the whole scripted kernel log.
    void set_kernel_log(const std::vector<std::string>& lines)
    {
        kernel_log_ = lines;
    }
    /// @brief Append one kernel log line (emits it to followers too).
    void append_kernel_line(const std::string& line);

    /// @brief Network plans applied (or recorded) so far — test observation.
    const std::vector<std::string>& applied_plans() const
    {
        return applied_plans_;
    }
    /// @brief Watchdog pets counted so far.
    uint64_t watchdog_pets() const
    {
        return watchdog_pets_;
    }
    /// @brief True between watchdog_set_timeout_s() and disarm.
    bool watchdog_running() const
    {
        return watchdog_running_;
    }
    /// @brief Current LED states.
    const std::map<std::string, bool>& led_state() const
    {
        return leds_;
    }
    /// @brief Current GPIO states.
    const std::map<int, bool>& gpio_state() const
    {
        return gpios_;
    }
    /// @brief Units started so far.
    const std::vector<std::string>& started_units() const
    {
        return started_units_;
    }
    /// @brief Current unit states.
    const std::map<std::string, bool>& unit_states() const
    {
        return units_;
    }
    /// @brief USB ports reset so far.
    const std::vector<std::string>& usb_resets() const
    {
        return usb_resets_;
    }
    /// @brief Display commands received so far.
    const std::vector<std::string>& display_commands() const
    {
        return display_cmds_;
    }
    /// @brief Units restarted so far.
    const std::vector<std::string>& unit_restarts() const
    {
        return restarts_;
    }

    /// @brief IPlatform implementation (per-method docs live on IPlatform).
    /// @{
    Result<FactoryIdentity> read_factory_identity() override;
    bool entropy_available() override;
    std::vector<StorageDevice> storage_devices() override;
    std::vector<Partition> partitions() override;
    MemInfo memory_info() override;
    CpuSnapshot cpu_snapshot() override;
    double load_average_1m() override;
    uint32_t process_count() override;
    uint64_t uptime_s() override;
    uint32_t boot_count() override;
    bool last_shutdown_clean() override;
    std::vector<ThermalZone> thermal_zones() override;
    std::vector<PowerSource> power_sources() override;
    Error watchdog_set_timeout_s(int seconds) override;
    Error watchdog_pet() override;
    bool time_locked() override;
    std::vector<TimeSourceInfo> time_sources() override;
    std::vector<IfaceInfo> interfaces() override;
    Result<BootSlotState> boot_slots() override;
    Error set_boot_slot(const std::string& slot, bool trial) override;
    Error confirm_boot_slot() override;
    Error gpio_set(int line, bool value) override;
    Result<bool> gpio_get(int line) override;
    Error led_set(const std::string& name, bool on) override;
    Error led_blink(const std::string& name, int on_ms, int off_ms, int repeats) override;
    Error usb_reset(const std::string& port) override;
    Result<bool> can_loopback_test(const std::string& iface) override;
    Error can_send(const std::string& iface, const CanFrame& frame) override;
    Result<std::vector<CanFrame>> can_recv(const std::string& iface, size_t max_frames) override;
    Error display_control(const std::string& cmd, const std::string& arg) override;
    Result<std::string> display_probe() override;
    Error unit_start(const std::string& unit) override;
    Error unit_stop(const std::string& unit) override;
    Error unit_restart(const std::string& unit) override;
    Result<bool> unit_active(const std::string& unit) override;
    Result<std::vector<std::string>> kernel_log_lines(bool from_start) override;
    Result<std::vector<std::string>> journal_lines(const std::string& unit, size_t max_lines) override;
    Error apply_network_plan(const std::string& plan_json) override;
    bool hq_reachable() override;
    MeasuredBootState measured_boot() override;
    /// @copydoc IPlatform::coredump_dir
    std::string coredump_dir() override
    {
        return "/var/lib/instrumentation/coredumps";
    }
    /// @copydoc IPlatform::platform_name
    std::string platform_name() override
    {
        return "simulated";
    }
    /// @}

    /// @brief Script per-unit journal contents for LogManager tests.
    void set_journal(const std::map<std::string, std::vector<std::string>>& j)
    {
        journal_ = j;
    }
    /// @brief Fault-injection / scenario knobs for the newer platform surfaces.
    /// @{
    /// @brief Script the partition topology.
    void set_partitions(std::vector<Partition> p)
    {
        partitions_ = std::move(p);
    }
    /// @brief Script the time-source inventory.
    void set_time_sources(std::vector<TimeSourceInfo> t)
    {
        time_sources_ = std::move(t);
    }
    /// @brief Script the measured-boot state.
    void set_measured_boot(MeasuredBootState m)
    {
        measured_boot_ = std::move(m);
    }
    /// @brief Queue a frame a can_recv on this interface will hand back
    /// (loopback by default: whatever was sent comes back, which is what a
    /// health check expects).
    void push_can_frame(const std::string& iface, const CanFrame& f)
    {
        can_rx_[iface].push_back(f);
    }
    /// @brief Frames sent on this interface so far (test observation).
    const std::vector<CanFrame>& can_sent(const std::string& iface)
    {
        return can_tx_[iface];
    }
    /// @brief Script CAN controller presence for an interface.
    void set_can_present(const std::string& iface, bool present)
    {
        can_present_[iface] = present;
    }
    /// @}

  private:
    std::mutex mu_; ///< Serialises scripted state across threads.
    // Follow cursor: index of the first kernel line not yet handed out in
    // follow mode (see kernel_log_lines).
    size_t follow_cursor_ = 0; ///< Kernel-log follow position.
    std::vector<Partition> partitions_; ///< Scripted partition topology.
    std::vector<TimeSourceInfo> time_sources_; ///< Scripted time sources.
    MeasuredBootState measured_boot_; ///< Scripted attestation state.
    std::map<std::string, std::vector<CanFrame>> can_rx_; ///< Queued receive frames per interface.
    std::map<std::string, std::vector<CanFrame>> can_tx_; ///< Recorded sent frames per interface.
    std::map<std::string, bool> can_present_; ///< CAN controller presence per interface.
    FactoryIdentity identity_; ///< Scripted factory identity.
    bool entropy_ = true; ///< Scripted entropy availability.
    std::vector<StorageDevice> storage_; ///< Scripted storage devices.
    std::vector<ThermalZone> thermals_; ///< Scripted thermal zones.
    std::vector<PowerSource> power_; ///< Scripted power sources.
    std::vector<IfaceInfo> ifaces_; ///< Scripted interfaces.
    MemInfo mem_; ///< Scripted memory snapshot.
    CpuSnapshot cpu_; ///< Scripted CPU snapshot.
    double load_ = 0.5; ///< Scripted load average.
    uint32_t processes_ = 80; ///< Scripted process count.
    uint64_t uptime_ = 0; ///< Scripted uptime.
    uint32_t boot_count_ = 1; ///< Scripted boot count.
    bool clean_shutdown_ = true; ///< Scripted last-shutdown state.
    bool time_locked_ = false; ///< Scripted time-lock state.
    bool hq_reachable_ = false; ///< Scripted HQ reachability.
    bool fail_watchdog_ = false, fail_gpio_ = false, fail_usb_ = false; ///< Fault-injection flags.
    bool fail_can_ = false, fail_display_ = false, fail_unit_start_ = false; ///< Fault-injection flags.
    bool fail_boot_slots_ = false; ///< Fault-injection flag for boot-slot ops.
    bool watchdog_running_ = false; ///< Simulated watchdog armed state.
    uint64_t watchdog_pets_ = 0; ///< Pets counted.
    std::map<std::string, bool> leds_; ///< LED states.
    std::map<int, bool> gpios_; ///< GPIO states.
    std::map<std::string, bool> units_; ///< Unit states.
    std::vector<std::string> started_units_; ///< Units started so far.
    std::vector<std::string> usb_resets_; ///< USB resets so far.
    std::vector<std::string> display_cmds_; ///< Display commands so far.
    std::vector<std::string> restarts_; ///< Unit restarts so far.
    std::vector<std::string> kernel_log_; ///< Scripted kernel log.
    std::map<std::string, std::vector<std::string>> journal_; ///< Scripted per-unit journal.
    std::vector<std::string> applied_plans_; ///< Recorded network plans.
    BootSlotState slots_; ///< Simulated A/B slot state.
};

} // namespace platform
} // namespace instr