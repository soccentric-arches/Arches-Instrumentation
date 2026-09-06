// istate command table (§3.2) — full coverage of the hub's operations.
#pragma once
#include "instr/common/command_table.h"
#include "instr/istate/core.h"

namespace instr {
namespace istate {
CommandTable build_istate_table(IstateCore* core);
}  // namespace istate
}  // namespace instr
