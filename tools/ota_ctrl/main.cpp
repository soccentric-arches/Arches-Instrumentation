// ota_ctrl — full-coverage control tool for ota (REQ §3.2).
// The daemon's command table is the single source of truth: compiled into
// this tool for --help, per-op help and shell completion, while every
// dispatch travels to the daemon over its raw-UDS ctrl socket (mode 0600,
// root only). Handlers are never invoked locally — the nullptr core is a
// dispatch-remote marker enforced by tool_main.
#include "instr/ota/daemon.h"
#include "instr/ctrl/ctrl.h"

int main(int argc, char** argv) {
  auto table = instr::ota::build_ota_table(nullptr);
  return instr::ctrl::tool_main(argc, argv, table, INSTR_CTRL_PROG, INSTR_CTRL_SOCKET);
}
