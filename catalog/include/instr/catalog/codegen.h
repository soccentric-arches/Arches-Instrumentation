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

namespace instr
{
namespace catalog
{

/// @brief Catalog severity levels.
enum class Severity
{
    Info,       ///< Informational.
    Warning,    ///< Warning.
    Minor,      ///< Minor alarm.
    Major,      ///< Major alarm.
    Critical    ///< Critical alarm.
};

/// @brief Canonical lowercase name of a severity.
const char* severity_name(Severity);

/// @brief Parse a severity name (case-insensitive).
/// @param s Name to parse.
/// @param err Receives the failure when unknown.
Severity severity_from_name(const std::string&, Error* err);

/// @brief One alarm definition in the catalog.
struct AlarmEntry
{
    uint32_t id = 0;             ///< Packed alarm identity.
    std::string name;            ///< Symbolic name → INSTR_ALARM_<UPPER_SNAKE>.
    Severity severity = Severity::Info;   ///< Default severity.
    std::string probable_cause;  ///< Probable cause text.
    std::string specific_problem;   ///< Specific problem text.
    std::string description;     ///< Human description.
    std::string escalation;      ///< none / promote-after-15m / promote-after-1h.
    std::string auto_clear;      ///< manual / auto.
    bool hq_reportable = true;   ///< False to keep the alarm local-only.
};

/// @brief A parsed alarm catalog.
struct AlarmCatalog
{
    uint32_t version = 1;              ///< Catalog schema version.
    std::vector<AlarmEntry> entries;    ///< Alarm definitions.
};

/// @brief Parse + validate a catalog; duplicate IDs or names are loud errors.
/// @param yaml_text Catalog source text.
/// @return Parsed catalog, or a diagnosable error.
Result<AlarmCatalog> parse_catalog(const std::string& yaml_text);

/// @brief Render the C++ header: INSTR_ALARM_* constants + entry table.
/// @param cat Parsed catalog.
/// @param version Catalog version stamped into the output.
/// @return Generated header text, or an error.
Result<std::string> render_header(const AlarmCatalog& cat, const std::string& version);

/// @brief Encode id text "0x1_04_1_0007" → uint32 (validated field widths).
/// @param text Canonical id text.
/// @return The packed id, or an error for malformed fields.
Result<uint32_t> parse_alarm_id_text(const std::string& text);

} // namespace catalog
} // namespace instr