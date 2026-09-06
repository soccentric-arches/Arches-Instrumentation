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
 * @file catalog/include/instr/catalog/codegen.h
 * @brief alarm catalog code generator domain (REQ §8)
 *
 * Pure logic: parse the YAML catalog → validated entries → render the
 * shared C++ header. No filesystem access; the main wraps it with IO.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <string>
#include <vector>

#include "instr/common/error.h"
#include "instr/common/json.h"

namespace instr {
namespace catalog {

enum class Severity { Info, Warning, Minor, Major, Critical };
const char* severity_name(Severity);
Severity severity_from_name(const std::string&, Error* err);

struct AlarmEntry {
  uint32_t id = 0;
  std::string name;           // symbolic name → INSTR_ALARM_<UPPER_SNAKE>
  Severity severity = Severity::Info;
  std::string probable_cause;
  std::string specific_problem;
  std::string description;
  std::string escalation;     // none / promote-after-15m / promote-after-1h
  std::string auto_clear;     // manual / auto
  bool hq_reportable = true;
};

// Parsed catalog.
struct AlarmCatalog {
  uint32_t version = 1;
  std::vector<AlarmEntry> entries;
};

// Parse + validate. Duplicate IDs or names are loud errors.
Result<AlarmCatalog> parse_catalog(const std::string& yaml_text);

// Render the C++ header: INSTR_ALARM_* constants + entry table.
Result<std::string> render_header(const AlarmCatalog& cat, const std::string& version);

// Encode id text "0x1_04_1_0007" → uint32 (validated field widths).
Result<uint32_t> parse_alarm_id_text(const std::string& text);

}  // namespace catalog
}  // namespace instr