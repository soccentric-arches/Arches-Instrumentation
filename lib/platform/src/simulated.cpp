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
 * @file lib/platform/src/simulated.cpp
 * @brief Implementation of the simulated platform: in-memory, scriptable reality covering every failure mode for hosts, CI, and fault-injection tests (REQ §, §11).
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#include "instr/platform/simulated.h"

namespace instr
{
namespace platform
{

SimulatedPlatform::SimulatedPlatform()
{
    identity_.serial = "SIM-0001";
    identity_.hw_revision = "rev-a";
    identity_.board_variant = "sim";
    identity_.mfg_date = "2025-01-01";
    identity_.hostname_base = "sim-device";
    identity_.identity_secret = "sim-factory-secret";
    identity_.valid = true;
    slots_.active = "A";
    slots_.inactive = "B";
    slots_.golden = "A";
    units_["instr-istate.service"] = true;
}

void SimulatedPlatform::set_factory_identity(const FactoryIdentity& id)
{
    std::lock_guard<std::mutex> lk(mu_);
    identity_ = id;
}
void SimulatedPlatform::inject_storage_device(const StorageDevice& d)
{
    std::lock_guard<std::mutex> lk(mu_);
    storage_.push_back(d);
}
void SimulatedPlatform::set_thermal(const std::string& id, const std::string& label, double c)
{
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& t : thermals_)
        if (t.id == id)
        {
            t.temp_c = c;
            t.label = label;
            return;
        }
    thermals_.push_back(ThermalZone{id, label, c});
}
void SimulatedPlatform::set_power(const std::string& id, const std::string& kind, double pct, bool charging)
{
    std::lock_guard<std::mutex> lk(mu_);
    power_.push_back(PowerSource{id, kind, pct, charging});
}
void SimulatedPlatform::inject_iface(const IfaceInfo& i)
{
    std::lock_guard<std::mutex> lk(mu_);
    ifaces_.push_back(i);
}
void SimulatedPlatform::append_kernel_line(const std::string& line)
{
    std::lock_guard<std::mutex> lk(mu_);
    kernel_log_.push_back(line);
}

Result<FactoryIdentity> SimulatedPlatform::read_factory_identity()
{
    std::lock_guard<std::mutex> lk(mu_);
    if (!identity_.valid) return Result<FactoryIdentity>::fail(Errc::NotFound, "factory identity not provisioned", "simulated");
    return Result<FactoryIdentity>::good(identity_);
}
bool SimulatedPlatform::entropy_available()
{
    return entropy_;
}
std::vector<StorageDevice> SimulatedPlatform::storage_devices()
{
    std::lock_guard<std::mutex> lk(mu_);
    return storage_;
}
MemInfo SimulatedPlatform::memory_info()
{
    std::lock_guard<std::mutex> lk(mu_);
    return mem_;
}
CpuSnapshot SimulatedPlatform::cpu_snapshot()
{
    std::lock_guard<std::mutex> lk(mu_);
    return cpu_;
}
double SimulatedPlatform::load_average_1m()
{
    return load_;
}
uint32_t SimulatedPlatform::process_count()
{
    return processes_;
}
uint64_t SimulatedPlatform::uptime_s()
{
    return uptime_;
}
uint32_t SimulatedPlatform::boot_count()
{
    return boot_count_;
}
bool SimulatedPlatform::last_shutdown_clean()
{
    return clean_shutdown_;
}
std::vector<ThermalZone> SimulatedPlatform::thermal_zones()
{
    std::lock_guard<std::mutex> lk(mu_);
    return thermals_;
}
std::vector<PowerSource> SimulatedPlatform::power_sources()
{
    std::lock_guard<std::mutex> lk(mu_);
    return power_;
}
Error SimulatedPlatform::watchdog_set_timeout_s(int seconds)
{
    (void)seconds;
    if (fail_watchdog_) return Error::make(Errc::Io, "simulated watchdog open failure", "watchdog");
    watchdog_running_ = true;
    return Error::none();
}
Error SimulatedPlatform::watchdog_pet()
{
    if (fail_watchdog_) return Error::make(Errc::Io, "simulated watchdog pet failure", "watchdog");
    ++watchdog_pets_;
    return Error::none();
}
bool SimulatedPlatform::time_locked()
{
    return time_locked_;
}
std::vector<IfaceInfo> SimulatedPlatform::interfaces()
{
    std::lock_guard<std::mutex> lk(mu_);
    return ifaces_;
}
Result<BootSlotState> SimulatedPlatform::boot_slots()
{
    if (fail_boot_slots_) return Result<BootSlotState>::fail(Errc::Io, "simulated bootctl failure", "boot");
    std::lock_guard<std::mutex> lk(mu_);
    return Result<BootSlotState>::good(slots_);
}
Error SimulatedPlatform::set_boot_slot(const std::string& slot, bool trial)
{
    if (fail_boot_slots_) return Error::make(Errc::Io, "simulated set-slot failure", "boot");
    std::lock_guard<std::mutex> lk(mu_);
    if (slot != "A" && slot != "B") return Error::make(Errc::InvalidArgument, "bad slot", slot);
    if (trial) slots_.pending = slot;
    return Error::none();
}
Error SimulatedPlatform::confirm_boot_slot()
{
    if (fail_boot_slots_) return Error::make(Errc::Io, "simulated confirm failure", "boot");
    std::lock_guard<std::mutex> lk(mu_);
    if (!slots_.pending.empty())
    {
        slots_.inactive = slots_.active;
        slots_.active = slots_.pending;
        slots_.pending.clear();
    }
    return Error::none();
}
Error SimulatedPlatform::gpio_set(int line, bool value)
{
    if (fail_gpio_) return Error::make(Errc::Io, "simulated gpio failure", "gpio");
    std::lock_guard<std::mutex> lk(mu_);
    gpios_[line] = value;
    return Error::none();
}
Result<bool> SimulatedPlatform::gpio_get(int line)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto it = gpios_.find(line);
    if (it == gpios_.end()) return Result<bool>::fail(Errc::NotFound, "gpio not configured", "gpio");
    return Result<bool>::good(it->second);
}
Error SimulatedPlatform::led_set(const std::string& name, bool on)
{
    std::lock_guard<std::mutex> lk(mu_);
    leds_[name] = on;
    return Error::none();
}
Error SimulatedPlatform::led_blink(const std::string& name, int on_ms, int off_ms, int repeats)
{
    std::lock_guard<std::mutex> lk(mu_);
    (void)on_ms;
    (void)off_ms;
    // Behavioural sanity: acknowledge the blink request.
    leds_[name + ":blink-ack"] = repeats > 0;
    return Error::none();
}
Error SimulatedPlatform::usb_reset(const std::string& port)
{
    if (fail_usb_) return Error::make(Errc::Io, "simulated usb reset failure", port);
    std::lock_guard<std::mutex> lk(mu_);
    usb_resets_.push_back(port);
    return Error::none();
}
Result<bool> SimulatedPlatform::can_loopback_test(const std::string& iface)
{
    if (fail_can_) return Result<bool>::good(false);
    (void)iface;
    return Result<bool>::good(true);
}
Error SimulatedPlatform::display_control(const std::string& cmd, const std::string& arg)
{
    if (fail_display_) return Error::make(Errc::Io, "simulated display failure", cmd);
    std::lock_guard<std::mutex> lk(mu_);
    display_cmds_.push_back(cmd + ":" + arg);
    return Error::none();
}
Result<std::string> SimulatedPlatform::display_probe()
{
    if (fail_display_) return Result<std::string>::fail(Errc::Io, "simulated display probe failure", "display");
    return Result<std::string>::good("ok 1280x800");
}
Error SimulatedPlatform::unit_start(const std::string& unit)
{
    if (fail_unit_start_) return Error::make(Errc::Unavailable, "simulated systemctl failure", unit);
    std::lock_guard<std::mutex> lk(mu_);
    units_[unit] = true;
    started_units_.push_back(unit);
    return Error::none();
}
Error SimulatedPlatform::unit_stop(const std::string& unit)
{
    std::lock_guard<std::mutex> lk(mu_);
    units_[unit] = false;
    return Error::none();
}
Error SimulatedPlatform::unit_restart(const std::string& unit)
{
    std::lock_guard<std::mutex> lk(mu_);
    units_[unit] = true;
    restarts_.push_back(unit);
    return Error::none();
}
Result<bool> SimulatedPlatform::unit_active(const std::string& unit)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto it = units_.find(unit);
    return Result<bool>::good(it != units_.end() && it->second);
}
Result<std::vector<std::string>> SimulatedPlatform::kernel_log_lines(bool from_start)
{
    std::lock_guard<std::mutex> lk(mu_);
    if (from_start)
    {
        // Backfill hands back the whole ring and parks the cursor at the end, so
        // the follower that runs next does not re-report the boot window.
        follow_cursor_ = kernel_log_.size();
        return Result<std::vector<std::string>>::good(kernel_log_);
    }
    if (follow_cursor_ > kernel_log_.size()) follow_cursor_ = kernel_log_.size();
    std::vector<std::string> fresh(kernel_log_.begin() + long(follow_cursor_), kernel_log_.end());
    follow_cursor_ = kernel_log_.size();
    return Result<std::vector<std::string>>::good(std::move(fresh));
}
Result<std::vector<std::string>> SimulatedPlatform::journal_lines(const std::string& unit, size_t max_lines)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto it = journal_.find(unit);
    if (it == journal_.end()) return Result<std::vector<std::string>>::good({});
    std::vector<std::string> out = it->second;
    if (out.size() > max_lines) out.erase(out.begin(), out.end() - long(max_lines));
    return Result<std::vector<std::string>>::good(std::move(out));
}
Error SimulatedPlatform::apply_network_plan(const std::string& plan_json)
{
    std::lock_guard<std::mutex> lk(mu_);
    applied_plans_.push_back(plan_json);
    return Error::none();
}
bool SimulatedPlatform::hq_reachable()
{
    return hq_reachable_;
}

} // namespace platform
} // namespace instr
// ---- §5 additions: partition topology, time sources, CAN, measured boot ----
// The simulated platform must be complete enough to run the whole suite on a
// build host with no hardware, so each of these has a working default rather
// than an empty stub.

namespace instr
{
namespace platform
{

std::vector<Partition> SimulatedPlatform::partitions()
{
    std::lock_guard<std::mutex> lk(mu_);
    if (!partitions_.empty()) return partitions_;
    // A representative A/B layout with a separate /pst and a state partition —
    // the topology §6 recommends, so budget logic has something real to read.
    std::vector<Partition> out;
    auto mk = [](const char* name, const char* mount, uint64_t total, uint64_t used, bool ro) {
        Partition p;
        p.name = name;
        p.device_path = std::string("/dev/") + name;
        p.parent = "mmcblk0";
        p.mount_point = mount;
        p.fs_type = ro ? "squashfs" : "ext4";
        p.bytes = total;
        p.used_bytes = used;
        p.read_only = ro;
        return p;
    };
    out.push_back(mk("mmcblk0p1", "/boot", 256ull << 20, 64ull << 20, false));
    out.push_back(mk("mmcblk0p2", "/", 2ull << 30, 1ull << 30, true)); // slot A
    out.push_back(mk("mmcblk0p3", "", 2ull << 30, 0, true)); // slot B
    out.push_back(mk("mmcblk0p4", "/pst", 512ull << 20, 32ull << 20, false));
    out.push_back(mk("mmcblk0p5", "/pst/state", 1ull << 30, 64ull << 20, false));
    return out;
}

std::vector<TimeSourceInfo> SimulatedPlatform::time_sources()
{
    std::lock_guard<std::mutex> lk(mu_);
    if (!time_sources_.empty()) return time_sources_;
    std::vector<TimeSourceInfo> out;
    TimeSourceInfo ntp;
    ntp.kind = "ntp";
    ntp.present = true;
    ntp.locked = time_locked_;
    ntp.offset_ms = 0.5;
    ntp.quality = time_locked_ ? 0.8 : 0.0;
    out.push_back(ntp);
    TimeSourceInfo rtc;
    rtc.kind = "rtc";
    rtc.present = true;
    rtc.locked = true;
    rtc.quality = 0.2;
    out.push_back(rtc);
    // GNSS and PTP are absent unless a test declares them: reporting a source
    // the board does not have is exactly the dishonesty §5.1 forbids.
    return out;
}

Error SimulatedPlatform::can_send(const std::string& iface, const CanFrame& frame)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto it = can_present_.find(iface);
    if (it != can_present_.end() && !it->second) return Error::make(Errc::NotSupported, "no CAN controller", iface);
    can_tx_[iface].push_back(frame);
    // Loopback by default: a health check that sends and reads back sees its
    // own frame, which is the cheap behavioural check §7.4 asks for.
    can_rx_[iface].push_back(frame);
    return Error::none();
}

Result<std::vector<SimulatedPlatform::CanFrame>> SimulatedPlatform::can_recv(const std::string& iface, size_t max_frames)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto it = can_present_.find(iface);
    if (it != can_present_.end() && !it->second) return Result<std::vector<CanFrame>>::fail(Errc::NotSupported, "no CAN controller", iface);
    auto& q = can_rx_[iface];
    std::vector<CanFrame> out;
    while (!q.empty() && out.size() < max_frames)
    {
        out.push_back(q.front());
        q.erase(q.begin());
    }
    return Result<std::vector<CanFrame>>::good(std::move(out));
}

MeasuredBootState SimulatedPlatform::measured_boot()
{
    std::lock_guard<std::mutex> lk(mu_);
    return measured_boot_;
}

} // namespace platform
} // namespace instr
