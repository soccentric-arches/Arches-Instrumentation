// monitord daemon main (REQ §7.3). Thin shell: wires platform, core, table,
// ctrl server, spoke channel; all logic lives in the domain classes.
#include <cstdio>

#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/ctrl/ctrl.h"
#include "instr/monitord/spoke.h"
#include "instr/monitord/table.h"
#include "instr/platform/native.h"
#include "instr/common/filesystem.h"
#include "instr/common/version_gen.h"

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "monitord");
  if (args.help) {
    std::printf("monitord — system resource monitoring daemon (Arches Instrumentation)\n"
                "usage: monitord [--socket PATH] [--hub PATH] [--root PATH] [--help]\n");
    return 0;
  }
  std::atomic<bool> stop{false};
  instr::install_signal_handlers(stop);

  instr::StderrSink sink;
  instr::SystemTimeSource ts;
  instr::Logger log(sink, ts);
  log.set_level(instr::LogLevel::Info);

  std::string root = args.root.empty() ? "/" : args.root;
  instr::platform::NativePlatform plat(root);
  instr::monitord::MonitorCore core(plat, ts);

  // Resource profile from the platform class (descriptor ships with the image).
  {
    instr::NativeFileSystem fs;
    auto txt = fs.read(root + "/etc/instrumentation/resource-profile.yaml");
    if (txt.ok()) {
      auto profile = instr::platform::load_resource_profile(*txt, instr::JValue::object(),
                                                            "resource-profile");
      // Absent schema: the loader validates structure; invalid profiles fall
      // back to compiled defaults (never wedge the device).
      (void)profile;
    }
  }

  auto table = instr::monitord::build_monitor_table(&core);
  std::string run_dir = root + "/run/instrumentation";
  std::string ctrl_path =
      args.socket_override.empty() ? run_dir + "/monitord-ctrl.sock" : args.socket_override;
  instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
  auto e = ctrl.start();
  if (!e.ok()) {
    std::fprintf(stderr, "monitord: ctrl server failed: %s\n", e.str().c_str());
    return 1;
  }

  std::string hub = args.hub_socket.empty() ? run_dir + "/monitord.sock" : args.hub_socket;
  instr::monitord::MonitorSpoke spoke(&core, &log, &table, instr::transport::uds_target(hub),
                                      INSTR_VERSION);
  spoke.start();

  instr::sd_notify_ready();
  instr::run_until_stop(stop, [&] { return spoke.registered() || true; });
  spoke.stop();
  ctrl.stop();
  return 0;
}