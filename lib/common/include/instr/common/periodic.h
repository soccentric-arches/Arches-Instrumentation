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
 * @file lib/common/include/instr/common/periodic.h
 * @brief One stoppable interval worker, shared by every daemon.
 *
 * The daemons own scheduled work — the fault follower, the alarm escalation
 * sweep, hardware's periodic checks, OTA's rollback deadline, backup's
 * schedule, telemetry's send interval, LogManager's prune, networkd's
 * connectivity probe and SSH expiry. Each of those is a timer, and a timer
 * written nine times is a timer with nine different shutdown bugs.
 *
 * Stopping is prompt: the worker waits on a condition variable, so stop()
 * returns without waiting out the remaining interval. The interval may be
 * changed at runtime (configuration arrives from istate while running) and
 * takes effect on the next wait.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace instr {

class PeriodicWorker {
 public:
  // interval_s <= 0 leaves the worker idle: it stays startable but does no
  // work, which is how a configuration disables scheduled checks without
  // tearing the thread down.
  PeriodicWorker(std::string name, double interval_s, std::function<void()> tick)
      : name_(std::move(name)), interval_s_(interval_s), tick_(std::move(tick)) {}
  ~PeriodicWorker() { stop(); }

  PeriodicWorker(const PeriodicWorker&) = delete;
  PeriodicWorker& operator=(const PeriodicWorker&) = delete;

  void start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { loop(); });
  }

  void stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
  }

  // Runtime interval change (configuration delivered while running). Takes
  // effect on the next wait; wakes the worker so a shortened interval is not
  // held up by the previous long one.
  void set_interval_s(double s) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      interval_s_ = s;
    }
    cv_.notify_all();
  }
  double interval_s() const {
    std::lock_guard<std::mutex> lk(mu_);
    return interval_s_;
  }

  bool running() const { return running_.load(); }
  uint64_t ticks() const { return ticks_.load(); }
  const std::string& name() const { return name_; }

  // Run one tick synchronously. Tests drive the work directly rather than
  // sleeping on wall-clock time.
  void tick_once() {
    if (tick_) tick_();
    ++ticks_;
  }

 private:
  void loop() {
    while (running_.load()) {
      double wait_s;
      {
        std::unique_lock<std::mutex> lk(mu_);
        wait_s = interval_s_;
        if (wait_s <= 0) wait_s = 0.5;  // idle poll; re-checks for a new interval
        cv_.wait_for(lk, std::chrono::duration<double>(wait_s),
                     [this] { return !running_.load(); });
      }
      if (!running_.load()) return;
      if (interval_s() <= 0) continue;  // disabled by configuration
      tick_once();
    }
  }

  std::string name_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  double interval_s_;
  std::function<void()> tick_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> ticks_{0};
};

}  // namespace instr
