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
 * @file lib/platform/include/instr/platform/descriptors.h
 * @brief declarative descriptors (REQ §5.1)
 *
 * Board-specific content is data, never code:
 * devices.yaml        — board devices + their checks and controls
 * resource profile    — sampling intervals, retention, budgets by class
 * capability manifest — which features exist, so istate reports honestly
 * Adding a board means adding descriptors, not editing daemons.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <map>
#include <string>
#include <vector>

#include "instr/common/error.h"
#include "instr/common/json.h"

namespace instr {
namespace platform {

// ---- device descriptor ----
struct DeviceCheck {
  std::string name;  // e.g. "loopback"
  std::string op;  // e.g. "gpio_toggle_read"
  std::map<std::string, std::string> params;
};
struct DeviceSpec {
  std::string name;  // "status-led", "gpio-1", "can0", "usb1", "display"
  std::string kind;  // gpio / led / can / usb / display / sensor / watchdog
  std::vector<std::string> controls;  // control op names
  std::vector<DeviceCheck> checks;
  std::map<std::string, std::string> params;  // e.g. line=1, port=1-2
};
struct DeviceDescriptor {
  std::string board;  // board variant name
  std::vector<DeviceSpec> devices;
};

// ---- resource profile ----
struct ResourceProfile {
  std::string platform_class;  // small / medium / large
  double sample_interval_default_s = 5;
  std::map<std::string, double> sample_intervals_per_class_s;  // cpu/memory/storage/thermal/load
  double highres_window_s = 60;  // fine-grained local retention
  double highres_interval_s = 1;
  double telemetry_interval_s = 30;
  uint64_t telemetry_batch_max_records = 200;
  uint64_t spool_max_bytes = 64ull * 1024 * 1024;
  uint64_t logs_max_bytes = 128ull * 1024 * 1024;
  uint64_t ota_staging_bytes = 512ull * 1024 * 1024;
  uint64_t backup_max_bytes = 256ull * 1024 * 1024;
  uint64_t timeline_max_events = 5000;
};

// ---- capability manifest ----
struct CapabilityManifest {
  std::string board;
  std::vector<std::string> capabilities;  // wifi, wifi_ap, can, display, gnss, ptp, watchdog, ...
  bool has(const std::string& c) const {
    for (const auto& x : capabilities)
      if (x == c) return true;
    return false;
  }
};

// Loaders validate against the JSON Schemas in schemas/.
Result<DeviceDescriptor> load_device_descriptor(const std::string& yaml_text, const JValue& schema,
                                                const std::string& source);
Result<ResourceProfile> load_resource_profile(const std::string& yaml_text, const JValue& schema,
                                              const std::string& source);
Result<CapabilityManifest> load_capability_manifest(const std::string& yaml_text,
                                                    const JValue& schema,
                                                    const std::string& source);

}  // namespace platform
}  // namespace instr