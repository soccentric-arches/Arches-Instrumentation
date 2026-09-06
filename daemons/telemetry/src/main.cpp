// telemetry daemon main (thin shell, REQ §7.8).
#include <cstdio>

#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/telemetry/daemon.h"

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "telemetry");
  if (args.help) {
    std::printf("telemetry — aggregation, batch shaping, health score\n"
                "usage: telemetry [--socket PATH] [--hub PATH] [--root PATH] [--help]\n");
    return 0;
  }
  std::atomic<bool> stop{false};
  instr::install_signal_handlers(stop);
  instr::StderrSink sink;
  instr::SystemTimeSource ts;
  instr::Logger log(sink, ts);
  instr::telemetry::TelemetryCore core(ts);
  core.set_repro({INSTR_VERSION, "1", "", ""});
  auto table = instr::telemetry::build_telemetry_table(&core);
  std::string run_dir = args.root.empty() ? "/run/instrumentation" : args.root + "/run/instrumentation";
  std::string ctrl_path =
      args.socket_override.empty() ? run_dir + "/telemetry-ctrl.sock" : args.socket_override;
  instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
  auto e = ctrl.start();
  if (!e.ok()) {
    std::fprintf(stderr, "telemetry: ctrl server failed: %s\n", e.str().c_str());
    return 1;
  }
  std::string hub = args.hub_socket.empty() ? run_dir + "/telemetry.sock" : args.hub_socket;
  instr::telemetry::TelemetrySpoke spoke(&core, &log, &table, instr::transport::uds_target(hub),
                                         INSTR_VERSION);
  spoke.start();
  instr::sd_notify_ready();
  instr::run_until_stop(stop, {});
  spoke.stop();
  ctrl.stop();
  return 0;
}
