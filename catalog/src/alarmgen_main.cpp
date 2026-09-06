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
 * @file catalog/src/alarmgen_main.cpp
 * @brief builds the shared alarm ID header from catalog/alarms.yaml
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#include <fstream>
#include <iostream>

#include "instr/catalog/codegen.h"
#include "instr/common/version_gen.h"

/// @brief alarmgen tool entry: parse the catalog YAML and emit the C++ header.
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: alarmgen <catalog.yaml> <output-header.h>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in)
    {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 2;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto cat = instr::catalog::parse_catalog(text);
    if (!cat.ok())
    {
        std::cerr << "catalog rejected: " << cat.error.str() << "\n";
        return 1;
    }
    auto hdr = instr::catalog::render_header(*cat, INSTR_VERSION);
    if (!hdr.ok())
    {
        std::cerr << "header render failed: " << hdr.error.str() << "\n";
        return 1;
    }
    std::ofstream out(argv[2]);
    if (!out)
    {
        std::cerr << "cannot write " << argv[2] << "\n";
        return 2;
    }
    out << *hdr;
    return 0;
}
