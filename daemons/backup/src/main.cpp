// backup daemon main (thin shell, REQ §7.10). All /pst writes happen under
// istate's direction (R4): the commit function is wired by istate's relay.
#include <cstdio>

#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/backup/daemon.h"
#include "instr/common/filesystem.h"

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "backup");
  if (args.help) {
    std::printf("backup — snapshot /pst/system, archive, restore (Arches Instrumentation)\n"
                "usage: backup [--socket PATH] [--hub PATH] [--root PATH] [--help]\n");
    return 0;
  }
  std::atomic<bool> stop{false};
  instr::install_signal_handlers(stop);
  instr::StderrSink sink;
  instr::SystemTimeSource ts;
  instr::Logger log(sink, ts);
  std::string root = args.root.empty() ? "/" : args.root;
  instr::NativeFileSystem fs;
  instr::backup::BackupOptions opts;
  // Identity comes from istate via the enrollment state; the daemon never
  // reads factory/ itself at runtime.
  auto ident = fs.read(root + "/run/instrumentation/device-identity.json");
  if (ident.ok()) {
    auto j = instr::json_parse(*ident);
    if (j.ok() && (*j).is_object()) {
      if (const instr::JValue* v = (*j).find("serial")) opts.device_serial = v->as_string();
      if (const instr::JValue* v = (*j).find("hw_revision")) opts.hardware_revision = v->as_string();
    }
  }
  opts.schema_version = "1";
  opts.firmware_version = INSTR_VERSION;
  auto secret = fs.read(root + "/run/instrumentation/enrollment-secret");
  opts.identity_secret = secret.ok() ? *secret : "";
  instr::backup::BackupCore core(fs, opts, ts);
  core.set_archive_dir(root + "/pst/backup");
  auto table = instr::backup::build_backup_table(&core);
  std::string run_dir = root + "/run/instrumentation";
  std::string ctrl_path =
      args.socket_override.empty() ? run_dir + "/backup-ctrl.sock" : args.socket_override;
  instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
  auto e = ctrl.start();
  if (!e.ok()) {
    std::fprintf(stderr, "backup: ctrl server failed: %s\n", e.str().c_str());
    return 1;
  }
  std::string hub = args.hub_socket.empty() ? run_dir + "/backup.sock" : args.hub_socket;
  instr::backup::BackupSpoke spoke(&core, &log, &table, instr::transport::uds_target(hub),
                                   INSTR_VERSION);
  spoke.start();
  instr::sd_notify_ready();
  instr::run_until_stop(stop, {});
  spoke.stop();
  ctrl.stop();
  return 0;
}
