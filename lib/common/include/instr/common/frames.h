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
 * @file lib/common/include/instr/common/frames.h
 * @brief raw-UDS control framing (REQ §3.2)
 *
 * Length-prefixed frames: 4-byte little-endian length + UTF-8 JSON payload.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "instr/common/error.h"

namespace instr {
namespace frames {

inline void put_u32(std::string& s, uint32_t v) {
  for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xFF);
}

inline std::string encode_frame(const std::string& payload) {
  std::string out;
  put_u32(out, uint32_t(payload.size()));
  out += payload;
  return out;
}

constexpr size_t kMaxFrame = 16 * 1024 * 1024;  // 16 MiB safety cap

// Streaming decoder: feed bytes, extract complete frames.
class FrameDecoder {
 public:
  // Returns true when a frame was extracted; *out filled.
  bool feed(const char* data, size_t len, std::string* out) {
    buf_.append(data, len);
    if (buf_.size() < 4) return false;
    uint32_t n = uint32_t(uint8_t(buf_[0])) | (uint32_t(uint8_t(buf_[1])) << 8) |
                 (uint32_t(uint8_t(buf_[2])) << 16) | (uint32_t(uint8_t(buf_[3])) << 24);
    if (n > kMaxFrame) {
      buf_.clear();
      bad_length_ = true;
      return false;
    }
    if (buf_.size() < 4 + n) return false;
    *out = buf_.substr(4, n);
    buf_.erase(0, 4 + n);
    return true;
  }
  bool bad_length() const { return bad_length_; }

 private:
  std::string buf_;
  bool bad_length_ = false;
};

}  // namespace frames
}  // namespace instr