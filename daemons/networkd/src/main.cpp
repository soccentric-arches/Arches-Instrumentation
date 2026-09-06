// networkd daemon main (thin shell, REQ §7.2).
#include <cstdio>

#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/networkd/daemon.h"
#include "instr/platform/native.h"

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "networkd");
  if (args.help) {
    std::printf("networkd — network/firewall/tunnel/time daemon (Arches Instrumentation)\n"
                "usage: networkd [--socket PATH] [--hub PATH] [--root PATH] [--help]\n");
    return 0;
  }
  std::atomic<bool> stop{false};
  instr::install_signal_handlers(stop);
  instr::StderrSink sink;
  instr::SystemTimeSource ts;
  instr::Logger log(sink, ts);

  std::string root = args.root.empty() ? "/" : args.root;
  instr::platform::NativePlatform plat(root);
  instr::networkd::NetworkdCore core(plat, ts);
  auto table = instr::networkd::build_networkd_table(&core);

  std::string run_dir = root + "/run/instrumentation";
  std::string ctrl_path =
      args.socket_override.empty() ? run_dir + "/networkd-ctrl.sock" : args.socket_override;
  instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
  auto e = ctrl.start();
  if (!e.ok()) {
    std::fprintf(stderr, "networkd: ctrl server failed: %s\n", e.str().c_str());
    return 1;
  }
  std::string hub = args.hub_socket.empty() ? run_dir + "/networkd.sock" : args.hub_socket;
  instr::networkd::NetworkdSpoke spoke(&core, &log, &table, instr::transport::uds_target(hub),
                                       INSTR_VERSION);
  spoke.start();
  instr::sd_notify_ready();
  instr::run_until_stop(stop, {});
  spoke.stop();
  ctrl.stop();
  return 0;
}