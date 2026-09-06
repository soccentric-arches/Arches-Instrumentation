// hardware daemon main (thin shell, REQ §7.4).
#include <cstdio>
#include <fstream>
#include <sstream>

#include "instr/common/daemon_skeleton.h"
#include "instr/common/config.h"
#include "instr/common/log.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/hardware/daemon.h"
#include "instr/platform/native.h"
#include "instr/common/filesystem.h"

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "hardware");
  if (args.help) {
    std::printf("hardware — board device control + quick health checks (Arches Instrumentation)\n"
                "usage: hardware [--socket PATH] [--hub PATH] [--root PATH] [--help]\n");
    return 0;
  }
  std::atomic<bool> stop{false};
  instr::install_signal_handlers(stop);
  instr::StderrSink sink;
  instr::SystemTimeSource ts;
  instr::Logger log(sink, ts);

  std::string root = args.root.empty() ? "/" : args.root;
  instr::platform::NativePlatform plat(root);
  // Board knowledge lives in devices.yaml (data, not code).
  instr::JValue schema = instr::JValue::object();
  {
    instr::NativeFileSystem fs;
    auto txt = fs.read(root + "/etc/instrumentation/devices.yaml");
    auto sch = fs.read(root + "/usr/share/instrumentation/schemas/devices.schema.json");
    if (txt.ok() && sch.ok()) {
      auto parsed_schema = instr::load_schema_text(*sch);
      if (parsed_schema.ok()) schema = *parsed_schema;
      auto desc = instr::platform::load_device_descriptor(*txt, schema, "devices.yaml");
      if (desc.ok()) {
        instr::hardware::HardwareCore core(plat, ts, *desc);
        auto table = instr::hardware::build_hardware_table(&core);
        std::string run_dir = root + "/run/instrumentation";
        std::string ctrl_path = args.socket_override.empty() ? run_dir + "/hardware-ctrl.sock"
                                                             : args.socket_override;
        instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
        auto e = ctrl.start();
        if (!e.ok()) {
          std::fprintf(stderr, "hardware: ctrl server failed: %s\n", e.str().c_str());
          return 1;
        }
        std::string hub = args.hub_socket.empty() ? run_dir + "/hardware.sock" : args.hub_socket;
        instr::hardware::HardwareSpoke spoke(&core, &log, &table,
                                             instr::transport::uds_target(hub), INSTR_VERSION);
        spoke.start();
        instr::sd_notify_ready();
        instr::run_until_stop(stop, {});
        spoke.stop();
        ctrl.stop();
        return 0;
      }
      std::fprintf(stderr, "hardware: devices.yaml rejected: %s\n", desc.error.str().c_str());
      return 1;
    }
  }
  std::fprintf(stderr, "hardware: devices.yaml not found under %s\n", root.c_str());
  return 1;
}