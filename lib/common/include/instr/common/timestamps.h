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
 * @file lib/common/include/instr/common/timestamps.h
 * @brief timestamps and time reconciliation (REQ §4)
 *
 * Every record carries CLOCK_MONOTONIC plus boot id alongside wall clock.
 * A GNSS or NTP lock mid-run must never reorder history: wall times are
 * re-stamped and clamped to be non-decreasing in arrival order.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "instr/common/error.h"

namespace instr {

struct Stamp {
  uint64_t mono_ns = 0;
  std::string boot_id;
  int64_t wall_ns = 0;
  uint64_t seq = 0;
  bool wall_valid = false;
};

// Injected clock so domain logic has no OS dependency.
class TimeSource {
 public:
  virtual ~TimeSource() = default;
  virtual uint64_t mono_ns() = 0;
  virtual int64_t wall_ns() = 0;
  virtual std::string boot_id() = 0;
  virtual uint64_t next_seq() = 0;  // process-wide monotonic arrival order
};

// Real clock: CLOCK_MONOTONIC + realtime + /proc/sys/kernel/random/boot_id.
class SystemTimeSource : public TimeSource {
 public:
  uint64_t mono_ns() override;
  int64_t wall_ns() override;
  std::string boot_id() override;
  uint64_t next_seq() override;
};

// Manual clock for tests; seq auto-increments.
class ManualTimeSource : public TimeSource {
 public:
  uint64_t mono = 1'000'000'000ull;  // 1 s
  int64_t wall = 1'700'000'000'000'000'000ll;
  std::string bid = "boot-1";
  uint64_t seq_counter = 0;
  uint64_t mono_ns() override { return mono; }
  int64_t wall_ns() override { return wall; }
  std::string boot_id() override { return bid; }
  uint64_t next_seq() override { return ++seq_counter; }
};

// Reconciliation API invoked when a time source acquires lock.
class TimeReconciler {
 public:
  explicit TimeReconciler(TimeSource& ts) : ts_(ts) {}

  // Record a monotonic-only observation awaiting wall-clock stamping
  // (spooled data produced while the clock was unavailable).
  void record_pending(uint64_t mono_ns);

  struct Pending {
    uint64_t seq;
    uint64_t mono_ns;
  };
  const std::deque<Pending>& pending() const { return pending_; }
  size_t pending_count() const { return pending_.size(); }

  // On lock: re-stamp all pending records. offset_ns is the wall-clock
  // correction reported by the time source. Returns number re-stamped.
  // History is never reordered: stamps materialise in seq order with
  // non-decreasing wall time.
  size_t reconcile(int64_t offset_ns, const std::string& source);

  // Stamps produced by reconcile(), ordered by seq.
  std::vector<Stamp> take_stamped();
  const std::vector<Stamp>& stamped() const { return stamped_; }

  // Stamp an observation made *now*.
  Stamp stamp_now();

  // Wall time for a historical monotonic sample under the locked mapping;
  // negative/0 result when no lock has been acquired yet.
  int64_t wall_for_mono(uint64_t mono_ns) const {
    if (!locked_) return 0;
    return base_wall_ns_ + static_cast<int64_t>(mono_ns - base_mono_ns_);
  }

  bool wall_valid() const { return locked_; }
  int64_t offset_ns() const { return offset_ns_; }
  const std::string& source() const { return source_; }

 private:
  TimeSource& ts_;
  std::deque<Pending> pending_;
  std::vector<Stamp> stamped_;
  bool locked_ = false;
  int64_t offset_ns_ = 0;
  std::string source_;
  int64_t base_wall_ns_ = 0;
  uint64_t base_mono_ns_ = 0;
};

// Build a Stamp from any TimeSource (seq assigned by the source).
Stamp make_stamp(TimeSource& ts);

// Wall time derived from the reconciler's locked timeline, for re-stamping
// historical monotonic samples: wall(mono) = base_wall + (mono - base_mono).
int64_t project_wall_ns(const TimeReconciler& rec, uint64_t mono_ns);

}  // namespace instr