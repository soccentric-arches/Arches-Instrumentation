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
 * @file lib/common/include/instr/common/crypto.h
 * @brief cryptographic primitives for device identity,
 *
 * signing, and encrypted backups (REQ §6.2, §9, §10).
 * SHA-256 / HMAC-SHA256 (FIPS 180-4, RFC 2104), X25519 (RFC 7748),
 * ChaCha20 (RFC 8439), HKDF-SHA256 (RFC 5869). Self-contained, no external
 * crypto dependency; every primitive is tested against published vectors.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "instr/common/error.h"

namespace instr {
namespace crypto {

struct Digest32 {
  std::array<uint8_t, 32> b{};
  std::string hex() const;
};

Digest32 sha256(const uint8_t* data, size_t len);
inline Digest32 sha256(const std::string& s) {
  return sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}
Digest32 hmac_sha256(const std::string& key, const std::string& msg);

// ---- X25519 (RFC 7748) ----
struct X25519Key {
  std::array<uint8_t, 32> priv{};
  std::array<uint8_t, 32> pub{};
};
using Rng32 = std::function<void(uint8_t*, size_t)>;
X25519Key x25519_generate(const Rng32& rng);
std::array<uint8_t, 32> x25519(const uint8_t priv[32], const uint8_t pub_in[32]);

// ---- ChaCha20 stream cipher (RFC 8439) ----
void chacha20(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, const uint8_t* in,
              uint8_t* out, size_t len);
// Encrypt-then-MAC convenience: ChaCha20 stream + HMAC-SHA256 over
// (nonce || ciphertext). Output layout: nonce(12) || ciphertext || tag(32).
std::string aead_seal(const std::string& key32, const std::string& plaintext, const Rng32& rng);
Result<std::string> aead_open(const std::string& key32, const std::string& sealed);

// ---- HKDF-SHA256 (RFC 5869) ----
std::string hkdf_sha256(const std::string& ikm, const std::string& salt, const std::string& info,
                        size_t len);

// ---- encodings ----
std::string base64_encode(const std::string& raw);
Result<std::string> base64_decode(const std::string& b64);
std::string hex_encode(const std::string& raw);
Result<std::string> hex_decode(const std::string& hex);

// ---- randomness ----
// Fills n bytes from the OS CSPRNG (getrandom(2)); fails loudly rather than
// silently degrading — key generation must not run without entropy (§6.2).
Error random_bytes(uint8_t* out, size_t n);

}  // namespace crypto
}  // namespace instr