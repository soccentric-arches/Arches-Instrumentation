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
 * @file lib/platform/include/instr/platform/platform.h
 * @brief platform abstraction layer (REQ §5, R9)
 *
 * The only place target variation exists. One interface; implementations:
 * SimulatedPlatform — complete simulation for hosts/CI/fault injection.
 * NativePlatform    — standard Linux sysfs/procfs paths shared by the
 * nine targets; board specifics come from descriptors.
 * No #ifdef for a target anywhere in daemon code: the same binaries ship on
 * every target.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <functional>
#include <string>
#include <vector>

#include "instr/common/error.h"

namespace instr
{
namespace platform
{

struct FactoryIdentity
{
    std::string serial;
    std::string hw_revision;
    std::string board_variant;
    std::string mfg_date;
    std::string hostname_base; // hostname derived on first boot
    std::string identity_secret; // factory-provisioned HMAC key (never logged)
    bool valid = false;
};

struct StorageDevice
{
    std::string name;
    std::string device_path;
    std::string kind; // emmc / sd / nvme / usb
    uint64_t bytes = 0;
    double wear_pct = -1; // -1 when unknown
    uint32_t lifetime_days_estimate = 0; // 0 when unknown
    // SMART / NVMe health, where the device exposes it (§5 wear indicators).
    int32_t temperature_c = -1; // -1 when unknown
    uint64_t power_on_hours = 0;
    uint64_t reallocated_sectors = 0;
    uint64_t media_errors = 0;
    bool smart_ok = true; // false once the device reports failing
    bool smart_available = false;
};

// Partition topology (§5): which partitions exist, where they are mounted,
// and how full they are. The budgets in §6 are meaningless without knowing
// which partition /pst and the spool actually live on.
struct Partition
{
    std::string name; // e.g. mmcblk0p3
    std::string device_path;
    std::string parent; // parent block device
    std::string mount_point; // empty when not mounted
    std::string fs_type;
    uint64_t bytes = 0;
    uint64_t used_bytes = 0;
    bool read_only = false;
};

// Which time sources this board actually has (§5, §7.1). istate reports the
// source it is using; without this it cannot tell "no GNSS lock yet" from
// "this board has no GNSS receiver".
struct TimeSourceInfo
{
    std::string kind; // gnss / ptp / ntp / rtc
    bool present = false; // hardware/daemon exists on this board
    bool locked = false; // currently disciplined
    double offset_ms = 0;
    double quality = 0; // 0..1, source-specific confidence
};

// Measured boot (§10): report the platform's attestation state upward where
// it exists. Absent TPM/PCR support is reported honestly, never faked.
struct MeasuredBootState
{
    bool available = false;
    std::string mechanism; // tpm2 / optee / none
    std::map<uint32_t, std::string> pcrs; // PCR index → hex digest
    bool secure_boot_enabled = false;
};

struct ThermalZone
{
    std::string id;
    std::string label;
    double temp_c = 0;
};

struct PowerSource
{
    std::string id;
    std::string kind; // ac / battery
    double percent = 0;
    bool charging = false;
};

struct IfaceInfo
{
    std::string name;
    bool up = false;
    bool link = false;
    std::string mac;
    std::vector<std::string> addrs;
};

struct BootSlotState
{
    std::string active; // "A" / "B"
    std::string inactive;
    std::string pending; // staged slot awaiting confirmation; empty if none
    std::string golden; // fallback slot
    uint32_t boot_attempts = 0;
};

struct MemInfo
{
    uint64_t total_bytes = 0;
    uint64_t available_bytes = 0;
    uint64_t swap_total_bytes = 0;
    uint64_t swap_free_bytes = 0;
};

struct CpuSnapshot
{
    uint32_t cores = 1;
    std::vector<uint64_t> busy_ticks; // per core
    std::vector<uint64_t> total_ticks; // per core
    std::vector<double> freq_mhz;
    std::string governor;
};

class IPlatform
{
  public:
    virtual ~IPlatform() = default;

    // ---- identity & entropy (first-boot key generation, REQ §6.2) ----
    virtual Result<FactoryIdentity> read_factory_identity() = 0;
    virtual bool entropy_available() = 0;

    // ---- storage / wear ----
    virtual std::vector<StorageDevice> storage_devices() = 0;
    // Partition topology, including mount points and usage (§5).
    virtual std::vector<Partition> partitions() = 0;
    virtual MemInfo memory_info() = 0;
    virtual CpuSnapshot cpu_snapshot() = 0;
    virtual double load_average_1m() = 0;
    virtual uint32_t process_count() = 0;
    virtual uint64_t uptime_s() = 0;
    virtual uint32_t boot_count() = 0;
    virtual bool last_shutdown_clean() = 0;

    // ---- thermal / power ----
    virtual std::vector<ThermalZone> thermal_zones() = 0;
    virtual std::vector<PowerSource> power_sources() = 0;

    // ---- hardware watchdog ----
    virtual Error watchdog_set_timeout_s(int seconds) = 0;
    virtual Error watchdog_pet() = 0;

    // ---- time ----
    virtual bool time_locked() = 0;
    // Which sources this board has, and their current state (§5, §7.1).
    virtual std::vector<TimeSourceInfo> time_sources() = 0;

    // ---- network enumeration ----
    virtual std::vector<IfaceInfo> interfaces() = 0;

    // ---- boot slots / rollback / golden boot (REQ §5) ----
    virtual Result<BootSlotState> boot_slots() = 0;
    virtual Error set_boot_slot(const std::string& slot, bool trial) = 0;
    virtual Error confirm_boot_slot() = 0;

    // ---- hardware primitives (Hardware daemon only, R7) ----
    virtual Error gpio_set(int line, bool value) = 0;
    virtual Result<bool> gpio_get(int line) = 0;
    virtual Error led_set(const std::string& name, bool on) = 0;
    virtual Error led_blink(const std::string& name, int on_ms, int off_ms, int repeats) = 0;
    virtual Error usb_reset(const std::string& port) = 0;
    virtual Result<bool> can_loopback_test(const std::string& iface) = 0;
    // CAN send/receive primitives (§5). send returns Unavailable where the
    // board has no CAN controller; recv returns the frames buffered since the
    // previous call, so a health check can prove the bus actually carries data.
    struct CanFrame
    {
        uint32_t id = 0;
        bool extended = false;
        std::vector<uint8_t> data;
    };
    virtual Error can_send(const std::string& iface, const CanFrame& frame) = 0;
    virtual Result<std::vector<CanFrame>> can_recv(const std::string& iface, size_t max_frames) = 0;
    virtual Error display_control(const std::string& cmd, const std::string& arg) = 0;
    virtual Result<std::string> display_probe() = 0;

    // ---- systemd orchestration (istate requests start/stop, REQ §7.1) ----
    virtual Error unit_start(const std::string& unit) = 0;
    virtual Error unit_stop(const std::string& unit) = 0;
    virtual Error unit_restart(const std::string& unit) = 0;
    virtual Result<bool> unit_active(const std::string& unit) = 0;

    // ---- kernel log source (Faults daemon, R6) ----
    // from_start = true:  read the full ring from the beginning (boot backfill)
    //                     and position the follow cursor at the end.
    // from_start = false: follow — return only lines not yet returned, so a
    //                     poller never re-reports a fault it already saw.
    virtual Result<std::vector<std::string>> kernel_log_lines(bool from_start) = 0;
    // Application log sinks / journal access for LogManager.
    virtual Result<std::vector<std::string>> journal_lines(const std::string& unit, size_t max_lines) = 0;

    // ---- network apply hooks (networkd) ----
    // Renders/applies a plan (interfaces, firewall, ssh windows, wg, time).
    // The simulated platform records the plan; native shells out only when
    // INSTR_APPLY_NETWORK=1 (never silently mutates a host).
    virtual Error apply_network_plan(const std::string& plan_json) = 0;
    virtual bool hq_reachable() = 0;

    // ---- attestation (§10) ----
    virtual MeasuredBootState measured_boot() = 0;

    // ---- misc ----
    virtual std::string coredump_dir() = 0;
    virtual std::string platform_name() = 0;
};

} // namespace platform
} // namespace instr