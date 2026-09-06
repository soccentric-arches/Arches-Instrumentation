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
 * @file lib/common/include/instr/common/daemon_skeleton.h
 * @brief standard daemon lifecycle skeleton (REQ §4)
 *
 * Signal handling, graceful shutdown, readiness notification (sd_notify),
 * argument parsing shared by every daemon main.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <atomic>
#include <functional>
#include <csignal>
#include <string>

#include "instr/common/error.h"

namespace instr {

struct DaemonArgs {
  std::string name;
  std::string socket_override;  // --socket PATH (ctrl socket override)
  std::string hub_socket;  // --hub PATH: istate registration socket
  std::string root;  // --root PATH: relocate /pst and /run for tests
  // --platform native|simulated (or INSTR_PLATFORM). The simulated platform
  // must be able to run the entire suite on a build host with no hardware
  // (REQ §5), which means the daemons themselves have to be able to select
  // it — not only the unit tests.
  std::string platform = "native";
  bool help = false;
};

// True when the daemon was asked for the simulated platform.
inline bool use_simulated_platform(const DaemonArgs& a) { return a.platform == "simulated"; }

DaemonArgs parse_daemon_args(int argc, char** argv, const std::string& daemon_name);

// Installs SIGINT/SIGTERM handlers that set the stop flag (graceful
// shutdown); SIGPIPE is ignored (write errors are handled via Result).
void install_signal_handlers(std::atomic<bool>& stop_flag);

// systemd readiness notification via NOTIFY_SOCKET (datagram, no libsystemd
// dependency). Returns false when NOTIFY_SOCKET is unset (running unsupervised).
bool sd_notify(const std::string& status);
inline bool sd_notify_ready() { return sd_notify("READY=1"); }

// Block until stop_flag is set or `heartbeat` returns false.
void run_until_stop(const std::atomic<bool>& stop_flag, const std::function<bool()>& heartbeat,
                    int poll_ms = 200);

}  // namespace instr