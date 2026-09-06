// alarms daemon main (thin shell, REQ §7.6). Active table persists across
// restart in state/alarms/active.db (via istate-owned state paths).
#include <cstdio>

#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/alarms/daemon.h"
#include "instr/platform/native.h"
#include "instr/common/filesystem.h"

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "alarms");
  if (args.help) {
    std::printf("alarms — telecom-style alarm manager (Arches Instrumentation)\n"
                "usage: alarms [--socket PATH] [--hub PATH] [--root PATH] [--help]\n");
    return 0;
  }
  std::atomic<bool> stop{false};
  instr::install_signal_handlers(stop);
  instr::StderrSink sink;
  instr::SystemTimeSource ts;
  instr::Logger log(sink, ts);

  std::string root = args.root.empty() ? "/" : args.root;
  instr::NativeFileSystem fs;
  std::string db_path = root + "/pst/state/alarms/active.db";
  instr::alarms::AlarmsCore core(fs, db_path, ts);
  // Load persisted active table + merged catalog, then regenerate from the
  // shipped catalog preserving operator edits.
  core.manager().load();
  core.manager().load_catalog(root + "/pst/state/alarms/catalog.yaml");
  auto shipped = fs.read(root + "/usr/share/instrumentation/catalog/alarms.yaml");
  if (shipped.ok()) {
    auto entries = instr::alarms::load_catalog_entries(*shipped);
    if (entries.ok()) {
      auto st = core.manager().merge_catalog(*entries);
      (void)st;
      core.manager().save_catalog(root + "/pst/state/alarms/catalog.yaml");
    }
  }
  core.manager().save();

  auto table = instr::alarms::build_alarms_table(&core);
  std::string run_dir = root + "/run/instrumentation";
  std::string ctrl_path =
      args.socket_override.empty() ? run_dir + "/alarms-ctrl.sock" : args.socket_override;
  instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
  auto e = ctrl.start();
  if (!e.ok()) {
    std::fprintf(stderr, "alarms: ctrl server failed: %s\n", e.str().c_str());
    return 1;
  }
  std::string hub = args.hub_socket.empty() ? run_dir + "/alarms.sock" : args.hub_socket;
  instr::alarms::AlarmsSpoke spoke(&core, &log, &table, instr::transport::uds_target(hub),
                                   INSTR_VERSION);
  spoke.start();
  instr::sd_notify_ready();
  // Periodic persistence + escalation sweep.
  instr::run_until_stop(stop, [&] {
    static uint64_t last = 0;
    uint64_t now = ts.mono_ns();
    if (now - last > 5'000'000'000ull) {
      last = now;
      core.manager().save();
      core.manager().escalation_sweep(now);
    }
    return true;
  });
  core.manager().save();
  spoke.stop();
  ctrl.stop();
  return 0;
}