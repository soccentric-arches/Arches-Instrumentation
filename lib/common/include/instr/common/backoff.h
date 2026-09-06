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
 * @file lib/common/include/instr/common/backoff.h
 * @brief jittered exponential backoff (REQ §3.4, §11)
 *
 * Flood control: a site of hundreds of devices powering up together must not
 * stampede HQ. HQ backpressure is honoured by callers via reset().
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <functional>
#include <random>

namespace instr {

class JitteredBackoff {
 public:
  JitteredBackoff(double base_s, double max_s, double jitter_frac = 0.2)
      : base_(base_s), max_(max_s), jitter_(jitter_frac) {}

  // Returns the next delay in seconds; grows exponentially, jittered ±frac.
  double next() {
    if (attempt_ == 0) {
      attempt_ = 1;
      return base_ * jitter_scale();
    }
    attempt_ <<= 1;
    double d = base_ * static_cast<double>(attempt_);
    if (d > max_) d = max_;
    return d * jitter_scale();
  }

  void reset() { attempt_ = 0; }
  uint64_t attempts() const { return attempt_; }

  void set_rng(std::function<double()> rng01) { rng01_ = std::move(rng01); }

 private:
  double jitter_scale() {
    double u = rng01_ ? double(rng01_()) : uniform_(rng_);
    return 1.0 + jitter_ * (2.0 * u - 1.0);  // 1 ± jitter
  }
  double base_, max_, jitter_;
  uint64_t attempt_ = 0;
  std::mt19937 rng_{std::random_device{}()};
  std::uniform_real_distribution<double> uniform_{0.0, 1.0};
  std::function<double()> rng01_;
};

}  // namespace instr