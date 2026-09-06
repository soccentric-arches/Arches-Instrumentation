// faults daemon main (thin shell, REQ §7.5). Purely passive: reads the
// kernel ring from the start on launch (backfill) and follows it.
#include <cstdio>

#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/faults/daemon.h"
#include "instr/platform/native.h"
#include "instr/common/filesystem.h"

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "faults");
  if (args.help) {
    std::printf("faults — passive kernel-log fault detection (Arches Instrumentation)\n"
                "usage: faults [--socket PATH] [--hub PATH] [--root PATH] [--help]\n");
    return 0;
  }
  std::atomic<bool> stop{false};
  instr::install_signal_handlers(stop);
  instr::StderrSink sink;
  instr::SystemTimeSource ts;
  instr::Logger log(sink, ts);

  std::string root = args.root.empty() ? "/" : args.root;
  instr::platform::NativePlatform plat(root);
  instr::faults::FaultsCore core;

  // Rules are data: default set ships with the image; HQ may push new
  // signatures as configuration (written by istate).
  instr::NativeFileSystem fs;
  auto rules_txt = fs.read(root + "/etc/instrumentation/fault-rules.yaml");
  if (rules_txt.ok()) {
    instr::Error e = core.load_rules(*rules_txt);
    if (!e.ok()) std::fprintf(stderr, "faults: %s\n", e.str().c_str());
  }

  auto table = instr::faults::build_faults_table(&core);
  std::string run_dir = root + "/run/instrumentation";
  std::string ctrl_path =
      args.socket_override.empty() ? run_dir + "/faults-ctrl.sock" : args.socket_override;
  instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
  auto e = ctrl.start();
  if (!e.ok()) {
    std::fprintf(stderr, "faults: ctrl server failed: %s\n", e.str().c_str());
    return 1;
  }
  std::string hub = args.hub_socket.empty() ? run_dir + "/faults.sock" : args.hub_socket;
  instr::faults::FaultsSpoke spoke(&core, &log, &table, instr::transport::uds_target(hub),
                                   INSTR_VERSION);
  spoke.start();

  // Backfill: the kernel boot log is the most diagnostic window there is.
  auto boot_log = plat.kernel_log_lines(true);
  if (boot_log.ok() && !boot_log->empty()) core.feed(*boot_log, ts.mono_ns(), true);

  instr::sd_notify_ready();
  instr::run_until_stop(stop, {});
  spoke.stop();
  ctrl.stop();
  return 0;
}