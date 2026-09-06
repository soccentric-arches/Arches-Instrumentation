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
 * @file lib/platform/include/instr/platform/native.h
 * @brief native platform implementation
 *
 * Reads standard Linux interfaces (sysfs/procfs, /dev/watchdog, boot
 * counters) that are common across the nine targets; board-specific device
 * topology comes from descriptors, never from code (R9). Network/systemd
 * mutations are dry-run recorded unless INSTR_APPLY_NETWORK=1 so the same
 * binary is safe in CI and on a laptop.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <mutex>
#include "instr/platform/platform.h"

namespace instr
{
namespace platform
{

/// @brief Native (real-board) platform implementation reading /proc, /sys, SMART and U-Boot env.
class NativePlatform : public IPlatform
{
  public:
    /// @brief Tear down open descriptors (watchdog, kmsg follow).
    ~NativePlatform() override;
    /// @brief Construct the platform rooted at @p sysroot ("/" on a device).
    explicit NativePlatform(std::string sysroot = "/");

    /// @brief Test seam: redirect file reads to a fixture directory.
    void set_sysroot(const std::string& root)
    {
        sysroot_ = root;
    }

    /// @copydoc IPlatform::read_factory_identity
    Result<FactoryIdentity> read_factory_identity() override;
    /// @copydoc IPlatform::entropy_available
    bool entropy_available() override;
    /// @copydoc IPlatform::storage_devices
    std::vector<StorageDevice> storage_devices() override;
    /// @copydoc IPlatform::partitions
    std::vector<Partition> partitions() override;
    /// @copydoc IPlatform::memory_info
    MemInfo memory_info() override;
    /// @copydoc IPlatform::cpu_snapshot
    CpuSnapshot cpu_snapshot() override;
    /// @copydoc IPlatform::load_average_1m
    double load_average_1m() override;
    /// @copydoc IPlatform::process_count
    uint32_t process_count() override;
    /// @copydoc IPlatform::uptime_s
    uint64_t uptime_s() override;
    /// @copydoc IPlatform::boot_count
    uint32_t boot_count() override;
    /// @copydoc IPlatform::last_shutdown_clean
    bool last_shutdown_clean() override;
    /// @copydoc IPlatform::thermal_zones
    std::vector<ThermalZone> thermal_zones() override;
    /// @copydoc IPlatform::power_sources
    std::vector<PowerSource> power_sources() override;
    /// @copydoc IPlatform::watchdog_set_timeout_s
    Error watchdog_set_timeout_s(int seconds) override;
    /// @copydoc IPlatform::watchdog_pet
    Error watchdog_pet() override;
    /// @copydoc IPlatform::time_locked
    bool time_locked() override;
    /// @copydoc IPlatform::time_sources
    std::vector<TimeSourceInfo> time_sources() override;
    /// @copydoc IPlatform::interfaces
    std::vector<IfaceInfo> interfaces() override;
    /// @copydoc IPlatform::boot_slots
    Result<BootSlotState> boot_slots() override;
    /// @copydoc IPlatform::set_boot_slot
    Error set_boot_slot(const std::string& slot, bool trial) override;
    /// @copydoc IPlatform::confirm_boot_slot
    Error confirm_boot_slot() override;
    /// @copydoc IPlatform::gpio_set
    Error gpio_set(int line, bool value) override;
    /// @copydoc IPlatform::gpio_get
    Result<bool> gpio_get(int line) override;
    /// @copydoc IPlatform::led_set
    Error led_set(const std::string& name, bool on) override;
    /// @copydoc IPlatform::led_blink
    Error led_blink(const std::string& name, int on_ms, int off_ms, int repeats) override;
    /// @copydoc IPlatform::usb_reset
    Error usb_reset(const std::string& port) override;
    /// @copydoc IPlatform::can_loopback_test
    Result<bool> can_loopback_test(const std::string& iface) override;
    /// @copydoc IPlatform::can_send
    Error can_send(const std::string& iface, const CanFrame& frame) override;
    /// @copydoc IPlatform::can_recv
    Result<std::vector<CanFrame>> can_recv(const std::string& iface, size_t max_frames) override;
    /// @copydoc IPlatform::display_control
    Error display_control(const std::string& cmd, const std::string& arg) override;
    /// @copydoc IPlatform::display_probe
    Result<std::string> display_probe() override;
    /// @copydoc IPlatform::unit_start
    Error unit_start(const std::string& unit) override;
    /// @copydoc IPlatform::unit_stop
    Error unit_stop(const std::string& unit) override;
    /// @copydoc IPlatform::unit_restart
    Error unit_restart(const std::string& unit) override;
    /// @copydoc IPlatform::unit_active
    Result<bool> unit_active(const std::string& unit) override;
    /// @copydoc IPlatform::kernel_log_lines
    Result<std::vector<std::string>> kernel_log_lines(bool from_start) override;
    /// @copydoc IPlatform::journal_lines
    Result<std::vector<std::string>> journal_lines(const std::string& unit, size_t max_lines) override;
    /// @copydoc IPlatform::apply_network_plan
    Error apply_network_plan(const std::string& plan_json) override;
    /// @copydoc IPlatform::hq_reachable
    bool hq_reachable() override;
    /// @copydoc IPlatform::measured_boot
    MeasuredBootState measured_boot() override;
    /// @brief Write the rendered supplicant / hostapd configuration for a plan.
    ///
    /// The PSK is resolved by networkd from /pst immediately before this call.
    ///
    /// @param mode "sta" or "ap".
    /// @param iface Interface the configuration applies to.
    /// @param text Rendered configuration text.
    Error write_wireless_conf(const std::string& mode, const std::string& iface, const std::string& text);
    /// @brief What the last apply_network_plan did (or would have done on a host).
    const std::vector<std::string>& applied_commands() const
    {
        return applied_commands_;
    }
    /// @copydoc IPlatform::coredump_dir
    std::string coredump_dir() override
    {
        return "/var/lib/instrumentation/coredumps";
    }
    /// @copydoc IPlatform::platform_name
    std::string platform_name() override
    {
        return "native";
    }

  private:
    // Kernel-log follow state: one long-lived /dev/kmsg descriptor so that
    // polling returns only new records (see kernel_log_lines).
    /// @brief Open the long-lived /dev/kmsg follow descriptor.
    bool open_follow_fd();
    /// @brief Read up to @p max_lines kernel messages from @p fd into @p out.
    static void read_kmsg_into(int fd, std::vector<std::string>* out, int max_lines);
    std::mutex kmsg_mu_; ///< Serialises /dev/kmsg follow access.
    int kmsg_follow_fd_ = -1; ///< Long-lived /dev/kmsg descriptor (-1 closed).

    // Bootloader environment access (U-Boot fw_printenv/fw_setenv), with a
    // flag-file fallback for boards that have no U-Boot environment.
    /// @brief Read a U-Boot environment variable.
    Result<std::string> fw_getenv(const std::string& var);
    /// @brief Write a U-Boot environment variable.
    Error fw_setenv(const std::string& var, const std::string& value);
    /// @brief Probe (once) whether the board has a U-Boot environment.
    bool have_fw_env();
    /// @brief Flag-file fallback for boards without U-Boot env.
    Error write_boot_file(const std::string& rel, const std::string& value);
    bool fw_env_checked_ = false; ///< True after the first U-Boot env probe.
    bool fw_env_present_ = false; ///< Cached U-Boot env availability.

    // Network application helpers. Every mutation goes through run_cmd /
    // write_conf, which record what they would do and only execute when
    // INSTR_APPLY_NETWORK=1 — so a build host is never reconfigured.
    /// @brief Record (and conditionally execute) a network command.
    Error run_cmd(const std::string& cmd);
    /// @brief Record (and conditionally execute) a config-file write.
    Error write_conf(const std::string& path, const std::string& text);

    /// @brief Enrich a device with SMART / NVMe health where it is exposed.
    void read_smart(StorageDevice* d);

    /// @brief Read a file under the sysroot.
    Result<std::string> read_file(const std::string& path);
    /// @brief List a directory under the sysroot.
    std::vector<std::string> list_dir(const std::string& path);
    /// @brief True when INSTR_APPLY_NETWORK=1 (mutations allowed).
    bool apply_enabled() const;

    std::vector<std::string> applied_commands_; ///< Log of executed/intended network commands.
    std::string sysroot_; ///< File-access root ("/" on device).
    int watchdog_fd_ = -1; ///< Open watchdog device descriptor.
    bool watchdog_opened_ = false; ///< True once the watchdog has been configured.
};

} // namespace platform
} // namespace instr