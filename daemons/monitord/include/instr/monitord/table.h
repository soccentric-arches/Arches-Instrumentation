// monitord command table — single source of truth for the gRPC surface and
// the _ctrl tool (REQ §3.2).
#pragma once
#include "instr/common/command_table.h"

namespace instr {
namespace monitord {

class MonitorCore;  // domain+state facade, defined in transport.h

CommandTable build_monitor_table(MonitorCore* core);

}  // namespace monitord
}  // namespace instr