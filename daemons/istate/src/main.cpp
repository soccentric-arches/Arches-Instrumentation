// ============================================================================
//  Arches Instrumentation
//
//  Copyright (C) 2026 Soccentric. All rights reserved.
//  Author: Sandesh Ghimire (sandesh@soccentric)
//
//  This file is part of a proprietary product of Soccentric.
//
//  NO WARRANTY:
//  This software is provided "AS IS", without warranty of any kind, either
//  express or implied, including but not limited to the warranties of
//  merchantability, fitness for a particular purpose, and non-infringement.
//  In no event shall Soccentric be liable for any claim, damages, or other
//  liability arising from the use of this software.
//
//  This software may not be used, copied, modified, distributed, or
//  disclosed, in whole or in part, without the express prior written
//  consent of Soccentric.
// ============================================================================
/**
 * @file daemons/istate/src/main.cpp
 * @brief istate daemon main (REQ §7.1). The hub: /pst, provisioning, orchestration,
 *
 * config service, spool, HQ session, hub gRPC server, ctrl server.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#include <cstdio>

#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/istate/core.h"
#include "instr/istate/table.h"
#include "instr/istate/hubs.h"
#include "instr/platform/factory.h"
#include "instr/common/filesystem.h"
#include <grpcpp/grpcpp.h>

namespace {
// Default YAML set shipped in the image (§6: seeds /pst on first boot).
std::map<std::string, std::string> load_defaults(instr::IFileSystem& fs,
                                                 const std::string& dir) {
  std::map<std::string, std::string> out;
  auto daemons = fs.list_dir(dir);
  if (!daemons.ok()) return out;
  for (const auto& d : *daemons) {
    auto txt = fs.read(dir + "/" + d + "/config.yaml");
    if (txt.ok()) out[d] = *txt;
  }
  return out;
}

// gRPC servers are truthy via operator bool.
inline bool server_up(const std::unique_ptr<grpc::Server>& s) { return s != nullptr; }
}  // namespace

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "istate");
  if (args.help) {
    std::printf("istate — the hub: state, provisioning, config, orchestration, external face\n"
                "usage: istate [--socket PATH] [--root PATH] [--help]\n");
    return 0;
  }
  std::atomic<bool> stop{false};
  instr::install_signal_handlers(stop);
  instr::StderrSink sink;
  instr::SystemTimeSource ts;
  instr::Logger log(sink, ts);

  std::string root = args.root.empty() ? "/" : args.root;
  std::string run_dir = root + "/run/instrumentation";
  instr::NativeFileSystem fs;
  fs.mkdirs(run_dir);

  instr::istate::IstateOptions opts;
  opts.pst_root = root + "/pst";
  opts.run_dir = run_dir;
  opts.build_version = INSTR_VERSION;
  // §5: the same binary runs against the simulated platform on a build host.
  bool platform_known = true;
  auto plat_owned = instr::platform::make_platform(args.platform, root, &platform_known);
  if (!platform_known)
    log.warning(args.name, "unknown --platform '" + args.platform + "', using native");
  instr::platform::IPlatform& plat = *plat_owned;
  instr::istate::IstateCore core(opts, fs, plat, ts, log);

  auto defaults = load_defaults(fs, root + "/usr/share/instrumentation/defaults");
  core.set_default_provider([defaults] { return defaults; });

  auto table = build_istate_table(&core);
  std::string ctrl_path =
      args.socket_override.empty() ? run_dir + "/istate-ctrl.sock" : args.socket_override;
  instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
  auto e = ctrl.start();
  if (!e.ok()) {
    std::fprintf(stderr, "istate: ctrl server failed: %s\n", e.str().c_str());
    return 1;
  }

  std::vector<std::unique_ptr<grpc::Service>> services;
  auto server = instr::istate::build_hub_server(core, run_dir, &services);
  if (!server_up(server)) {
    std::fprintf(stderr, "istate: gRPC server failed to start\n");
    return 1;
  }

  // HQ session: last in the startup order, never a precondition (§7.1).
  std::string hq_target = "unix:" + run_dir + "/hq.sock";
  if (const char* env = ::getenv("INSTR_HQ_TARGET")) hq_target = env;
  instr::istate::HqClient hq(core, log, hq_target);
  hq.start();

  core.boot(defaults);
  instr::sd_notify_ready();

  // Liveness sweeps + watchdog petting on the main loop.
  instr::run_until_stop(stop, [&] {
    auto evs = core.orchestrator().liveness_sweep(double(ts.mono_ns()) / 1e9,
                                                  core.options().liveness_hang_timeout_s);
    for (const auto& ev : evs) log.warning("istate", "liveness: " + ev.daemon + " " + ev.event);
    core.orchestrator().pet_watchdog();
    // Budget alarms before the limit (§6).
    for (const auto& c : core.budgets().over_warning())
      log.warning("istate", "storage budget warning: " + c);
    return true;
  }, 1000);

  core.shutdown();
  hq.stop();
  server->Shutdown();
  ctrl.stop();
  return 0;
}