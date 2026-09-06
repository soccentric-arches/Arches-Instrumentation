// logmanager daemon main (thin shell, REQ §7.7).
#include <cstdio>

#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/logmanager/daemon.h"
#include "instr/platform/native.h"
#include "instr/common/filesystem.h"

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "logmanager");
  if (args.help) {
    std::printf("logmanager — collection, rotation, tail, bundles, crash forensics\n"
                "usage: logmanager [--socket PATH] [--hub PATH] [--root PATH] [--help]\n");
    return 0;
  }
  std::atomic<bool> stop{false};
  instr::install_signal_handlers(stop);
  instr::StderrSink sink;
  instr::SystemTimeSource ts;
  instr::Logger log(sink, ts);

  std::string root = args.root.empty() ? "/" : args.root;
  instr::NativeFileSystem fs;
  std::string base = root + "/pst/state/logs";
  fs.mkdirs(base);
  instr::logmanager::LogCore core(fs, base, ts);

  // Sources are data: config lists which logs to collect and send.
  std::vector<instr::logmanager::SourceSpec> sources = {
      {"journal-system", "journal", "kernel", "INFO", 8 << 20, 5, 7ull * 24 * 3600, true},
      {"journal-istate", "journal", "instr-istate", "INFO", 8 << 20, 5, 7ull * 24 * 3600, true},
  };
  core.store().configure(sources);
  core.store().set_budget_bytes(128ull << 20);

  auto table = instr::logmanager::build_log_table(&core);
  std::string run_dir = root + "/run/instrumentation";
  std::string ctrl_path =
      args.socket_override.empty() ? run_dir + "/logmanager-ctrl.sock" : args.socket_override;
  instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
  auto e = ctrl.start();
  if (!e.ok()) {
    std::fprintf(stderr, "logmanager: ctrl server failed: %s\n", e.str().c_str());
    return 1;
  }
  std::string hub = args.hub_socket.empty() ? run_dir + "/logmanager.sock" : args.hub_socket;
  instr::logmanager::LogSpoke spoke(&core, &log, &table, instr::transport::uds_target(hub),
                                    INSTR_VERSION);
  spoke.start();
  instr::sd_notify_ready();
  instr::run_until_stop(stop, [&] {
    static uint64_t last = 0;
    if (ts.mono_ns() - last > 60ull * 1'000'000'000ull) {
      last = ts.mono_ns();
      core.store().enforce_retention(ts.mono_ns());
    }
    return true;
  });
  spoke.stop();
  ctrl.stop();
  return 0;
}