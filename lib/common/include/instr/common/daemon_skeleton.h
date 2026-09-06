// Arches Instrumentation — standard daemon lifecycle skeleton (REQ §4).
// Signal handling, graceful shutdown, readiness notification (sd_notify),
// argument parsing shared by every daemon main.
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
  std::string hub_socket;       // --hub PATH: istate registration socket
  std::string root;             // --root PATH: relocate /pst and /run for tests
  bool help = false;
};

DaemonArgs parse_daemon_args(int argc, char** argv, const std::string& daemon_name);

// Installs SIGINT/SIGTERM handlers that set the stop flag (graceful
// shutdown); SIGPIPE is ignored (write errors are handled via Result).
void install_signal_handlers(std::atomic<bool>& stop_flag);

// systemd readiness notification via NOTIFY_SOCKET (datagram, no libsystemd
// dependency). Returns false when NOTIFY_SOCKET is unset (running unsupervised).
bool sd_notify(const std::string& status);
inline bool sd_notify_ready() { return sd_notify("READY=1"); }

// Block until stop_flag is set or `heartbeat` returns false.
void run_until_stop(const std::atomic<bool>& stop_flag,
                    const std::function<bool()>& heartbeat, int poll_ms = 200);

}  // namespace instr