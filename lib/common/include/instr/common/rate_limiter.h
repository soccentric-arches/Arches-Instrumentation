// Arches Instrumentation — rate limiting primitives (REQ §4).
// Token bucket with a "suppressed N events" summary, used by Faults,
// LogManager and Telemetry so a stuck driver emitting 10k lines/s cannot
// drown the system.
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace instr {

class TokenBucket {
 public:
  TokenBucket(double rate_per_s, double burst,
              std::function<double()> now_s = nullptr)
      : rate_(rate_per_s), burst_(burst), now_(std::move(now_s)) {
    if (!now_) now_ = [] { return steady_now_s(); };
    tokens_ = burst_;
    last_ = now_();
  }

  bool try_acquire() {
    refill();
    if (tokens_ >= 1.0) {
      tokens_ -= 1.0;
      return true;
    }
    ++suppressed_;
    ++streak_;
    return false;
  }

  uint64_t suppressed_total() const { return suppressed_; }
  uint64_t current_streak() const { return streak_; }
  double rate() const { return rate_; }
  double burst() const { return burst_; }
  double tokens() const { return tokens_; }
  void reset() { tokens_ = burst_; streak_ = 0; }

  static double steady_now_s() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

 private:
  void refill() {
    double t = now_();
    tokens_ += (t - last_) * rate_;
    if (tokens_ > burst_) tokens_ = burst_;
    last_ = t;
  }
  double rate_, burst_, tokens_, last_;
  std::function<double()> now_;
  uint64_t suppressed_ = 0, streak_ = 0;
};

// Emits "suppressed N events" summaries: when a suppressed streak ends and N
// reached the threshold, exactly one summary is produced for the batch.
class SuppressionSummarizer {
 public:
  explicit SuppressionSummarizer(uint64_t threshold = 1) : threshold_(threshold) {}

  // Call after each try_acquire(). Returns a summary string exactly once per
  // streak when the streak met the threshold.
  std::optional<std::string> on_attempt(bool acquired, TokenBucket& bucket) {
    if (acquired) {
      if (streak_ >= threshold_) {
        std::string s = "suppressed " + std::to_string(streak_) + " events";
        streak_ = 0;
        return s;
      }
      streak_ = 0;
      return std::nullopt;
    }
    streak_ = bucket.current_streak();
    return std::nullopt;
  }

  uint64_t streak() const { return streak_; }

 private:
  uint64_t threshold_;
  uint64_t streak_ = 0;
};

}  // namespace instr