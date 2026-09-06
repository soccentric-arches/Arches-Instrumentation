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
 * @file daemons/logmanager/src/main.cpp
 * @brief logmanager daemon main (thin shell, REQ §7.7)
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#include <cstdio>

#include "instr/common/config_sink.h"
#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/common/periodic.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/logmanager/daemon.h"
#include "instr/platform/native.h"
#include "instr/common/filesystem.h"

int main(int argc, char** argv) {
  auto args = instr::parse_daemon_args(argc, argv, "logmanager");
  if (args.help) {
    std::printf("logmanager — collection, rotation, tail, bundles, crash forensics\n"
                "usage: logmanager [--socket PATH] [--hub PATH] [--root PATH]\n"
                "       [--platform native|simulated] [--help]\n");
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

  // The daemon's JSON Schema ships in the image; the configuration itself
  // arrives from istate over the channel (R4), never read from a file here.
  auto cfg_schema = instr::load_daemon_schema(fs, root, "logmanager");
  if (!cfg_schema.ok())
    log.warning("logmanager", "no JSON Schema installed: " + cfg_schema.error.str() +
                          " — deliveries will be applied unvalidated");

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
  if (cfg_schema.ok()) spoke.set_schema(*cfg_schema);
  spoke.start();

  // Rotation, retention and the storage budget are enforced on a timer, not
  // when someone happens to ask (§7.7): the budget exists to stop logs
  // wedging the device, which only works if it is applied continuously.
  instr::PeriodicWorker prune("logmanager-prune", core.config().prune_interval_s, [&] {
    auto st = core.store().enforce_retention(ts.mono_ns());
    if (st.files_removed)
      log.info("logmanager", "pruned " + std::to_string(st.files_removed) + " files, " +
                                 std::to_string(st.bytes_freed) + " bytes");
  });
  core.set_config_listener([&](const instr::logmanager::LogCore::Config& c) {
    prune.set_interval_s(c.enabled ? c.prune_interval_s : 0.0);
  });
  prune.start();

  instr::sd_notify_ready();
  instr::run_until_stop(stop, {});
  prune.stop();
  spoke.stop();
  ctrl.stop();
  return 0;
}