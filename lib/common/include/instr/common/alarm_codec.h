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
 * @file lib/common/include/instr/common/alarm_codec.h
 * @brief alarm/fault 32-bit identity (REQ §8, locked)
 *
 * [31:28] domain | [27:20] module | [19:16] X.733 type | [15:0] index.
 * An ID is decodable without the catalog:
 * 0x1_04_1_0007 = hardware / Hardware daemon / equipment / alarm 7.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <cstdint>
#include <map>
#include <string>

#include "instr/common/error.h"

namespace instr
{

enum class AlarmDomain : uint8_t
{
    Hardware = 1,
    Software = 2,
    Network = 3,
    Application = 4,
    Fault = 5,
};

enum class X733Type : uint8_t
{
    Equipment = 1,
    Communications = 2,
    ProcessingError = 3,
    QualityOfService = 4,
    Environmental = 5,
};

const char* alarm_domain_name(AlarmDomain);
const char* x733_type_name(X733Type);
AlarmDomain alarm_domain_from_name(const std::string&); // "hardware" etc.
X733Type x733_type_from_name(const std::string&);

// Module numbering: 0x01..0x0A are the ten daemons, 0x10.. are application
// modules; HQ renders via the shared catalog without hardcoding.
const std::map<uint8_t, std::string>& module_names();
uint8_t module_from_name(const std::string&);

struct AlarmId
{
    AlarmDomain domain;
    uint8_t module;
    X733Type type;
    uint16_t index;
    bool operator==(const AlarmId& o) const
    {
        return domain == o.domain && module == o.module && type == o.type && index == o.index;
    }
};

constexpr uint32_t encode_alarm_id(AlarmDomain d, uint8_t module, X733Type t, uint16_t index)
{
    return (static_cast<uint32_t>(d) << 28) | (static_cast<uint32_t>(module) << 20) | (static_cast<uint32_t>(t) << 16) | index;
}

Result<AlarmId> decode_alarm_id(uint32_t id);

// Canonical text form: 0x<domain>_<module>_<type>_<index:04x>
std::string alarm_id_string(uint32_t id);

} // namespace instr