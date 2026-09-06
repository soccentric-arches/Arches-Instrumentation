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
 * @file daemons/hardware/include/instr/hardware/daemon.h
 * @brief hardware daemon facade: core + command table + spoke
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <memory>
#include <mutex>

#include "instr/common/command_table.h"
#include "instr/common/config_sink.h"
#include "instr/common/periodic.h"
#include "instr/hardware/domain.h"
#include "instr/platform/platform.h"
#include "instr/transport/transport.h"
#include "hardware.grpc.pb.h"

namespace instr
{
namespace hardware
{
using namespace domain;
using namespace instr::transport;

class HardwareCore
{
  public:
    HardwareCore(platform::IPlatform& plat, TimeSource& ts, const platform::DeviceDescriptor& desc) : mgr_(plat, ts, desc)
    {
    }

    // Configuration delivered by istate (§7.4, §9).
    struct Config
    {
        bool enabled = true;
        double periodic_check_interval_s = 300.0; // 0 disables scheduled checks
        double check_timeout_s = 5.0;
    };
    Config config() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return cfg_;
    }
    Error apply_config(const ConfigDocument& doc)
    {
        Config c;
        c.enabled = doc.get_bool("enabled", true);
        c.periodic_check_interval_s = doc.get_double("periodic_check_interval_s", 300.0);
        c.check_timeout_s = doc.get_double("check_timeout_s", 5.0);
        if (c.periodic_check_interval_s < 0) return Error::make(Errc::InvalidArgument, "periodic_check_interval_s must be >= 0", "hardware config");
        std::function<void(const Config&)> listener;
        {
            std::lock_guard<std::mutex> lk(mu_);
            cfg_ = c;
            listener = on_config_;
        }
        if (listener) listener(c);
        return Error::none();
    }
    void set_config_listener(std::function<void(const Config&)> f)
    {
        std::lock_guard<std::mutex> lk(mu_);
        on_config_ = std::move(f);
    }

    DeviceManager& manager()
    {
        return mgr_;
    }
    void set_result_sink(std::function<void(const CheckResult&)> f)
    {
        std::lock_guard<std::mutex> lk(mu_);
        sink_ = std::move(f);
    }
    void emit(const CheckResult& r)
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (sink_) sink_(r);
    }
    std::string descriptor_summary_json() const;

  private:
    DeviceManager mgr_;
    std::function<void(const CheckResult&)> sink_;
    mutable std::mutex mu_;
    Config cfg_;
    std::function<void(const Config&)> on_config_;
};

CommandTable build_hardware_table(HardwareCore* core);

class HardwareSpoke
{
  public:
    HardwareSpoke(HardwareCore* core, Logger* log, const CommandTable* table, const std::string& hub_target, const std::string& build_version);
    void start();
    void stop();
    bool registered() const
    {
        return channel_->registered();
    }
    // Configuration is delivered by istate over the channel and validated
    // against this schema before it can change behaviour (R4, §9).
    void set_schema(JValue schema);
    uint64_t config_revision() const
    {
        return channel_->config_revision();
    }
    void send_result(const CheckResult& r);

  private:
    HardwareCore* core_;
    Logger* log_;
    const CommandTable* table_;
    std::unique_ptr<ConfigSink> config_sink_;
    std::unique_ptr<SpokeChannel<hardware::HardwareHub, hardware::HardwareSpokeFrame, hardware::HardwareHubFrame>> channel_;
};

} // namespace hardware
} // namespace instr