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
 * @file daemons/istate/include/instr/istate/hubs.h
 * @brief gRPC server wiring: nine per-spoke hub services (one socket per
 *
 * daemon, mode 0660 owned instr:<daemon>), the Configuration API (§3.3),
 * and the HQ session client (istate dials HqPeer over the tunnel, §3.4).
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <grpcpp/grpcpp.h>

#include "alarms.grpc.pb.h"
#include "backup.grpc.pb.h"
#include "faults.grpc.pb.h"
#include "hardware.grpc.pb.h"
#include "istate.grpc.pb.h"
#include "logmanager.grpc.pb.h"
#include "monitord.grpc.pb.h"
#include "networkd.grpc.pb.h"
#include "ota.grpc.pb.h"
#include "telemetry.grpc.pb.h"

#include "instr/istate/core.h"

namespace instr
{
namespace istate
{

// Hub impls derive from the generated nested `<X>Hub::Service` classes.
// Builds and runs the full gRPC server for istate. Socket paths are
// /run/instrumentation/<daemon>.sock (created by istate, §3.1) plus
// configuration.sock. Returns the server (BuildAndStart) or nullptr.
std::unique_ptr<grpc::Server> build_hub_server(IstateCore& core, const std::string& run_dir, std::vector<std::unique_ptr<grpc::Service>>* services);

// HQ session client: dials HqPeer over the tunnel, pushes spooled data,
// receives config pushes / OTA commands / backpressure.
class HqClient
{
  public:
    HqClient(IstateCore& core, Logger& log, const std::string& hq_target);
    void start();
    void stop();
    bool connected() const
    {
        return connected_;
    }

  private:
    void run_loop();
    bool try_session();

    IstateCore& core_;
    Logger& log_;
    std::string target_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    JitteredBackoff backoff_{2.0, 60.0};
};

} // namespace istate
} // namespace instr