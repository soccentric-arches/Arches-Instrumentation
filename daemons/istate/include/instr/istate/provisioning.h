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
 * @file daemons/istate/include/instr/istate/provisioning.h
 * @brief provisioning state machine (REQ §6.2)
 *
 * Virgin detection → default generation → entropy check → WireGuard keypair
 * generated on device (private key never leaves) → factory-signed enrollment
 * → HQ returns tunnel configuration → operational. Key rotation without a
 * site visit: regenerate, re-enroll, no factory involvement.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <functional>
#include <string>
#include <vector>

#include "instr/common/crypto.h"
#include "instr/common/filesystem.h"
#include "instr/istate/pst.h"
#include "instr/platform/platform.h"

namespace instr {
namespace istate {

struct DeviceIdentity {
  std::string hostname;
  std::string serial;
  std::string hw_revision;
  std::string board_variant;
  std::string mfg_date;
  std::string wg_public_key;  // base64
  std::string factory_sig;  // HMAC(factory secret, pubkey) — base64
  std::string pubkey_b64() const { return wg_public_key; }
};

struct TunnelConfig {
  std::string hq_endpoint;
  std::string device_addr;
  std::string hq_addr;
  std::string peer_pubkey_b64;
  bool valid = false;
};

class Provisioner {
 public:
  enum class State {
    Virgin,  // no system/ yet
    Seeding,  // defaults written
    Keygen,  // generating tunnel keypair
    Enrolling,  // enrolling with HQ
    Operational,
  };
  const char* state_name(State s) const;

  Provisioner(PstStore& pst, platform::IPlatform& plat, IFileSystem& fs)
      : pst_(pst), plat_(plat), fs_(fs) {}

  State state() const { return state_; }
  const DeviceIdentity& identity() const { return identity_; }
  const TunnelConfig& tunnel() const { return tunnel_; }

  // First-boot sequence (§6.2). enroll_fn performs the HQ round-trip:
  // given the signed enrollment request, returns the tunnel configuration.
  using EnrollFn = std::function<Result<TunnelConfig>(const DeviceIdentity&)>;
  Result<DeviceIdentity> run_first_boot(const EnrollFn& enroll_fn);

  // Key rotation without a site visit: regenerate, re-enroll, no factory
  // involvement.
  Result<DeviceIdentity> rotate_key(const EnrollFn& enroll_fn);

  //hostname derivation from factory identity.
  static std::string derive_hostname(const platform::FactoryIdentity& id);

 private:
  Result<DeviceIdentity> generate_and_enroll(bool rotation, const EnrollFn& enroll_fn);

  PstStore& pst_;
  platform::IPlatform& plat_;
  IFileSystem& fs_;
  State state_ = State::Virgin;
  DeviceIdentity identity_;
  TunnelConfig tunnel_;
};

}  // namespace istate
}  // namespace instr