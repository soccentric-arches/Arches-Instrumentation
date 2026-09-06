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
 * @file lib/common/include/instr/common/signature.h
 * @brief Public-key signature verification for config pushes and updates.
 *
 * REQ §9: "config pushes from HQ are signed and verified on device, so tunnel
 * compromise alone does not let an attacker reconfigure a fleet." §7.9 wants
 * the same before an update is staged.
 *
 * A symmetric HMAC cannot carry that property on its own: whatever can verify
 * can also forge, so the verifying key on the device is a signing key for the
 * whole fleet if a single device is opened. Verification is therefore
 * asymmetric — Ed25519, device holds only the public key.
 *
 * The implementation delegates to libcrypto rather than adding another
 * hand-written primitive: signature verification is the one place in this
 * codebase where a subtle bug is silently exploitable, and a reviewed
 * implementation is worth the dependency.
 *
 * Fail closed: if verification cannot be performed, it fails. There is no
 * path where an unverifiable signature is treated as valid.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <string>

#include "instr/common/error.h"

namespace instr {
namespace crypto {

// Verifies an Ed25519 signature.
//   public_key: 32 raw bytes (the HQ/release signing key, provisioned in the
//               image or at enrollment — never a device secret).
//   message:    the exact bytes that were signed.
//   signature:  64 raw bytes.
// Returns ok() only on a good signature. Every other outcome — malformed key,
// wrong length, unavailable backend — is an error, never a pass.
Error verify_ed25519(const std::string& public_key, const std::string& message,
                     const std::string& signature);

// True when a signature backend is compiled in. Used to report the device's
// security posture northbound, not to decide whether to enforce: with no
// backend, verification fails and signed content is refused.
bool signature_verification_available();

// Convenience: accepts a base64 key and signature (the wire form).
Error verify_ed25519_b64(const std::string& public_key_b64, const std::string& message,
                         const std::string& signature_b64);

}  // namespace crypto
}  // namespace instr
