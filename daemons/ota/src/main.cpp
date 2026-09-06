// ota daemon main (thin shell, REQ §7.9).
#include <cstdio>

#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/ota/daemon.h"
#include "instr/platform/native.h"
#include "instr/common/filesystem.h"

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "ota");
  if (args.help) {
    std::printf("ota — SWUpdate-based update manager (Arches Instrumentation)\n"
                "usage: ota [--socket PATH] [--hub PATH] [--root PATH] [--help]\n");
    return 0;
  }
  std::atomic<bool> stop{false};
  instr::install_signal_handlers(stop);
  instr::StderrSink sink;
  instr::SystemTimeSource ts;
  instr::Logger log(sink, ts);
  std::string root = args.root.empty() ? "/" : args.root;
  instr::NativeFileSystem fs;
  instr::platform::NativePlatform plat(root);
  instr::ota::OtaConfig cfg;
  cfg.min_version = "0.0.0";
  cfg.confirm_timeout_s = 600;
  instr::ota::OtaCore core(fs, plat, ts, cfg);
  core.set_secret_provider([] { return std::string(); });  // istate injects via relay
  core.set_running_version_provider([] { return std::string(INSTR_VERSION); });
  core.set_clock_provider([&ts] { return ts.mono_ns(); });
  core.set_wall_provider([&ts] { return ts.wall_ns() / 1'000'000'000ll; });
  auto table = instr::ota::build_ota_table(&core);
  std::string run_dir = root + "/run/instrumentation";
  std::string ctrl_path =
      args.socket_override.empty() ? run_dir + "/ota-ctrl.sock" : args.socket_override;
  instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
  auto e = ctrl.start();
  if (!e.ok()) {
    std::fprintf(stderr, "ota: ctrl server failed: %s\n", e.str().c_str());
    return 1;
  }
  std::string hub = args.hub_socket.empty() ? run_dir + "/ota.sock" : args.hub_socket;
  instr::ota::OtaSpoke spoke(&core, &log, &table, instr::transport::uds_target(hub), INSTR_VERSION);
  spoke.start();
  instr::sd_notify_ready();
  instr::run_until_stop(stop, [&] {
    auto ev = core.engine().tick(ts.mono_ns());
    (void)ev;
    return true;
  });
  spoke.stop();
  ctrl.stop();
  return 0;
}
