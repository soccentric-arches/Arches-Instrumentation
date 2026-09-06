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

class NativePlatform : public IPlatform
{
  public:
    ~NativePlatform() override;
    explicit NativePlatform(std::string sysroot = "/");

    // Test seam: redirect file reads to a fixture directory.
    void set_sysroot(const std::string& root)
    {
        sysroot_ = root;
    }

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
    // Writes the rendered supplicant / hostapd configuration for a plan. The
    // PSK is resolved by networkd from /pst immediately before this call.
    Error write_wireless_conf(const std::string& mode, const std::string& iface, const std::string& text);
    // What the last apply_network_plan did (or would have done on a host).
    const std::vector<std::string>& applied_commands() const
    {
        return applied_commands_;
    }
    std::string coredump_dir() override
    {
        return "/var/lib/instrumentation/coredumps";
    }
    std::string platform_name() override
    {
        return "native";
    }

  private:
    // Kernel-log follow state: one long-lived /dev/kmsg descriptor so that
    // polling returns only new records (see kernel_log_lines).
    bool open_follow_fd();
    static void read_kmsg_into(int fd, std::vector<std::string>* out, int max_lines);
    std::mutex kmsg_mu_;
    int kmsg_follow_fd_ = -1;

    // Bootloader environment access (U-Boot fw_printenv/fw_setenv), with a
    // flag-file fallback for boards that have no U-Boot environment.
    Result<std::string> fw_getenv(const std::string& var);
    Error fw_setenv(const std::string& var, const std::string& value);
    bool have_fw_env();
    Error write_boot_file(const std::string& rel, const std::string& value);
    bool fw_env_checked_ = false;
    bool fw_env_present_ = false;

    // Network application helpers. Every mutation goes through run_cmd /
    // write_conf, which record what they would do and only execute when
    // INSTR_APPLY_NETWORK=1 — so a build host is never reconfigured.
    Error run_cmd(const std::string& cmd);
    Error write_conf(const std::string& path, const std::string& text);

    // Enriches a device with SMART / NVMe health where it is exposed.
    void read_smart(StorageDevice* d);

    Result<std::string> read_file(const std::string& path);
    std::vector<std::string> list_dir(const std::string& path);
    bool apply_enabled() const;

    std::vector<std::string> applied_commands_;
    std::string sysroot_;
    int watchdog_fd_ = -1;
    bool watchdog_opened_ = false;
};

} // namespace platform
} // namespace instr