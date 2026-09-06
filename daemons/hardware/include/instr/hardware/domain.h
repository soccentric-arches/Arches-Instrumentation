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
 * @file daemons/hardware/include/instr/hardware/domain.h
 * @brief full control of board hardware (REQ §7.4, R7). Board knowledge
 *
 * lives entirely in devices.yaml; the same binary ships on every platform.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <map>
#include <string>
#include <vector>

#include "instr/common/error.h"
#include "instr/common/timestamps.h"
#include "instr/platform/descriptors.h"
#include "instr/platform/platform.h"

namespace instr
{
namespace hardware
{
namespace domain
{

/// @brief Outcome state of a device health check.
enum class CheckState
{
    Pass, ///< Check succeeded.
    Fail, ///< Check failed.
    Degraded, ///< Check passed but below expectations.
    NotPresent, ///< Device missing from the platform.
    Unknown ///< Check could not run.
};

/// @brief Canonical name of a check state ("pass", "fail", ...).
const char* check_state_name(CheckState);

/// @brief One executed device check.
struct CheckResult
{
    std::string device; ///< Device name from the descriptor.
    std::string check; ///< Check name.
    CheckState state = CheckState::Unknown; ///< Outcome.
    std::string detail; ///< Human-readable detail (failure reason).
    uint64_t duration_ms = 0; ///< How long the check took.
    uint64_t mono_ns = 0; ///< When it ran.
};

/// @brief Device controls and quick health checks over the platform primitives.
class DeviceManager
{
  public:
    /// @brief Bind the manager to the platform, a clock and the board descriptor.
    DeviceManager(platform::IPlatform& plat, TimeSource& ts, const platform::DeviceDescriptor& desc) : plat_(plat), ts_(ts), desc_(desc)
    {
    }

    /// @brief Devices listed in the board descriptor.
    const std::vector<platform::DeviceSpec>& devices() const
    {
        return desc_.devices;
    }
    /// @brief Look up a device spec by name.
    /// @return Pointer to the spec, or nullptr when unknown.
    const platform::DeviceSpec* find(const std::string& name) const;

    /// @brief Control operation: driving devices, not read-only.
    ///
    /// @param device Device name.
    /// @param control_op Control op from the device's controls list.
    /// @param args Operation arguments.
    /// @return ok(), or an error for unknown device/op or platform refusal.
    Error control(const std::string& device, const std::string& control_op, const std::map<std::string, std::string>& args);

    /// @brief Read a GPIO line via the platform (read side of the control op).
    Result<bool> gpio_read(const std::string& device);

    /// @brief Read side of the CAN control op.
    ///
    /// control() returns Error and so cannot hand back frames; receiving gets
    /// its own entry point for the same reason gpio_read does. Returns the
    /// frames buffered since the previous call.
    ///
    /// @param device Device name.
    /// @param max_frames Maximum frames returned.
    Result<std::vector<platform::IPlatform::CanFrame>> can_receive(const std::string& device, size_t max_frames);

    /// @brief Run one quick health check (presence + cheap behavioural sanity; not full qualification — that is the IV&V path).
    /// @param dev Device to check.
    /// @param chk Check to run.
    /// @return The check outcome.
    CheckResult run_check(const platform::DeviceSpec& dev, const platform::DeviceCheck& chk);
    /// @brief Run every check for one device.
    std::vector<CheckResult> check_device(const std::string& name);
    /// @brief Run every check for every device.
    std::vector<CheckResult> check_all();

    /// @brief Override the periodic scheduling interval.
    void set_periodic_interval_s(double s)
    {
        periodic_interval_s_ = s;
    }
    /// @brief Devices whose periodic check is due.
    /// @param now_mono_ns Current monotonic time.
    /// @return Results for due checks.
    std::vector<CheckResult> due_checks(uint64_t now_mono_ns);

  private:
    /// @brief Stamp and return a check result.
    CheckResult make_result(const std::string& dev, const std::string& chk, CheckState st, const std::string& detail, uint64_t started);
    platform::IPlatform& plat_; ///< Platform primitives (not owned).
    TimeSource& ts_; ///< Clock (not owned).
    platform::DeviceDescriptor desc_; ///< Board device inventory.
    std::map<std::string, uint64_t> last_check_mono_; ///< Last periodic check per device.
    double periodic_interval_s_ = 300; ///< Periodic scheduling interval.
};

} // namespace domain
} // namespace hardware
} // namespace instr