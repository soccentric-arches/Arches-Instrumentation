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
 * @file test/unit/test_ctrl_surface.cpp
 * @brief Every daemon's _ctrl surface is complete and self-describing.
 *
 * REQ §3.2: "Each _ctrl must be able to exercise every option and operation
 * its daemon supports — full coverage, not a convenience subset", and each
 * supports --help, subcommand help, --json, --socket and shell completion.
 *
 * The single command table is what makes that structural rather than
 * hand-maintained: the tool and the gRPC service dispatch from the same
 * table, so an operation cannot exist on one surface and not the other.
 * These tests check the property that guarantee rests on — that the table is
 * populated, described, and renders help and completion for every op.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#include <gtest/gtest.h>

#include "instr/alarms/daemon.h"
#include "instr/backup/daemon.h"
#include "instr/faults/daemon.h"
#include "instr/hardware/daemon.h"
#include "instr/istate/table.h"
#include "instr/logmanager/daemon.h"
#include "instr/monitord/table.h"
#include "instr/networkd/daemon.h"
#include "instr/ota/daemon.h"
#include "instr/telemetry/daemon.h"

using namespace instr;

namespace {

// Each tool builds its table with a null core — the same "dispatch remote"
// construction tools/<x>_ctrl/main.cpp uses. If a table cannot be built that
// way, the tool cannot render help without a running daemon.
struct Surface {
  const char* daemon;
  const char* prog;
  CommandTable table;
};

std::vector<Surface> all_surfaces() {
  std::vector<Surface> v;
  v.push_back({"istate", "istate_ctrl", istate::build_istate_table(nullptr)});
  v.push_back({"networkd", "network_ctrl", networkd::build_networkd_table(nullptr)});
  v.push_back({"monitord", "monitor_ctrl", monitord::build_monitor_table(nullptr)});
  v.push_back({"hardware", "hardware_ctrl", hardware::build_hardware_table(nullptr)});
  v.push_back({"faults", "fault_ctrl", faults::build_faults_table(nullptr)});
  v.push_back({"alarms", "alarm_ctrl", alarms::build_alarms_table(nullptr)});
  v.push_back({"logmanager", "log_ctrl", logmanager::build_log_table(nullptr)});
  v.push_back({"telemetry", "telemetry_ctrl", telemetry::build_telemetry_table(nullptr)});
  v.push_back({"ota", "ota_ctrl", ota::build_ota_table(nullptr)});
  v.push_back({"backup", "backup_ctrl", backup::build_backup_table(nullptr)});
  return v;
}

}  // namespace

TEST(CtrlSurface, EveryDaemonHasATableBuildableWithoutACore) {
  for (auto& s : all_surfaces()) {
    EXPECT_GT(s.table.ops().size(), 0u)
        << s.daemon << " exposes no operations: its _ctrl would be useless";
  }
}

TEST(CtrlSurface, EveryOperationIsDescribed) {
  // Help is generated from the table, so an undescribed op is an op an
  // operator cannot discover.
  for (auto& s : all_surfaces()) {
    for (const auto& op : s.table.ops()) {
      EXPECT_FALSE(op.name.empty()) << s.daemon << " has an unnamed operation";
      EXPECT_FALSE(op.description.empty())
          << s.daemon << "." << op.name << " has no description";
    }
  }
}

TEST(CtrlSurface, EveryArgumentIsDescribed) {
  for (auto& s : all_surfaces()) {
    for (const auto& op : s.table.ops()) {
      for (const auto& a : op.args) {
        EXPECT_FALSE(a.name.empty()) << s.daemon << "." << op.name << " has an unnamed argument";
        EXPECT_FALSE(a.description.empty())
            << s.daemon << "." << op.name << " argument '" << a.name << "' has no description";
      }
    }
  }
}

TEST(CtrlSurface, HelpTextListsEveryOperation) {
  for (auto& s : all_surfaces()) {
    const std::string help = s.table.help_text(s.prog);
    EXPECT_NE(help.find(s.prog), std::string::npos) << s.daemon;
    for (const auto& op : s.table.ops())
      EXPECT_NE(help.find(op.name), std::string::npos)
          << s.daemon << ": " << op.name << " is missing from --help";
  }
}

TEST(CtrlSurface, PerOperationHelpExistsForEveryOperation) {
  // §3.2 requires subcommand help, not just a top-level listing.
  for (auto& s : all_surfaces()) {
    for (const auto& op : s.table.ops()) {
      const std::string h = s.table.help_for(op.name);
      EXPECT_FALSE(h.empty()) << s.daemon << ": no help for " << op.name;
      EXPECT_NE(h.find(op.name), std::string::npos) << s.daemon << ": " << op.name;
    }
  }
}

TEST(CtrlSurface, ShellCompletionCoversEveryOperation) {
  for (auto& s : all_surfaces()) {
    const std::string bash = s.table.completion_bash(s.prog);
    const std::string zsh = s.table.completion_zsh(s.prog);
    EXPECT_FALSE(bash.empty()) << s.daemon << ": no bash completion";
    EXPECT_FALSE(zsh.empty()) << s.daemon << ": no zsh completion";
    for (const auto& op : s.table.ops()) {
      EXPECT_NE(bash.find(op.name), std::string::npos)
          << s.daemon << ": " << op.name << " missing from bash completion";
      EXPECT_NE(zsh.find(op.name), std::string::npos)
          << s.daemon << ": " << op.name << " missing from zsh completion";
    }
  }
}

TEST(CtrlSurface, OperationNamesAreUniqueWithinADaemon) {
  for (auto& s : all_surfaces()) {
    std::set<std::string> seen;
    for (const auto& op : s.table.ops())
      EXPECT_TRUE(seen.insert(op.name).second)
          << s.daemon << ": duplicate operation name '" << op.name << "'";
  }
}

TEST(CtrlSurface, UnknownOperationsAreRejectedDiagnosably) {
  for (auto& s : all_surfaces()) {
    CommandResult r = s.table.dispatch("no-such-op", "{}");
    EXPECT_FALSE(r.ok) << s.daemon << " accepted an operation it does not have";
    EXPECT_FALSE(r.error_message.empty()) << s.daemon << ": silent rejection";
  }
}

TEST(CtrlSurface, RequiredArgumentsAreEnforced) {
  // A missing required argument must be a clear error, not a silent default.
  for (auto& s : all_surfaces()) {
    for (const auto& op : s.table.ops()) {
      bool has_required = false;
      for (const auto& a : op.args) has_required = has_required || a.required;
      if (!has_required) continue;
      CommandResult r = s.table.dispatch(op.name, "{}");
      EXPECT_FALSE(r.ok) << s.daemon << "." << op.name
                         << " ran with no arguments despite requiring some";
    }
  }
}

TEST(CtrlSurface, EveryDaemonExposesItsCoreOperations) {
  // Spot-check that the tables carry the operations REQ §7 names for each
  // daemon, so "full coverage" is not satisfied by a table of one op.
  const std::map<std::string, std::vector<std::string>> expected = {
      {"istate", {"status", "spokes", "config-get", "config-set", "timeline", "reset",
                  "time-policy", "system-info", "audit", "budgets"}},
      {"networkd", {"status", "apply-plan", "firewall-add", "ssh-open", "ssh-close",
                    "tunnel-status", "time-policy-render"}},
      {"monitord", {"status", "sample-once", "set-interval", "wear"}},
      {"hardware", {"list-devices", "check", "check-all", "gpio-set", "led-set", "usb-reset"}},
      {"faults", {"stats", "rules-list", "rules-reload", "test-line"}},
      {"alarms", {"list", "raise", "ack", "clear", "escalation-sweep", "catalog-merge"}},
      {"logmanager", {"sources", "level-set", "bundle-create", "crash-report", "prune"}},
      {"telemetry", {"list-metrics", "register-metric", "ingest", "flush", "health-score"}},
      {"ota", {"status", "stage", "apply", "confirm", "rollback", "history", "canary"}},
      {"backup", {"create", "list", "verify", "preview", "restore", "schedule-set"}},
  };
  for (auto& s : all_surfaces()) {
    auto it = expected.find(s.daemon);
    if (it == expected.end()) continue;
    std::set<std::string> have;
    for (const auto& op : s.table.ops()) have.insert(op.name);
    for (const auto& want : it->second)
      EXPECT_TRUE(have.count(want) > 0) << s.daemon << " is missing operation '" << want << "'";
  }
}
