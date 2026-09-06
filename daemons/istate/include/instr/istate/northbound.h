// istate — northbound: persistent spool, HQ session with jittered backoff
// and backpressure, event timeline, live log tail fanout (REQ §7.1, §3.4).
// Nothing northbound depends on the tunnel being available at the moment it
// is produced (§3.4). Drop policy: oldest telemetry first, never alarms or
// faults; drops are reported (§11).
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "instr/common/backoff.h"
#include "instr/common/spool.h"
#include "instr/common/timestamps.h"

namespace instr {
namespace istate {

class BudgetManager;  // forward (defined in pst.h)

// ---- event timeline: single ordered per-device stream (§7.1) ----
struct TimelineRecord {
  uint64_t seq = 0;
  Stamp ts;
  std::string kind;   // boot / config_change / alarm / fault / update / restart / audit
  std::string actor;
  std::string detail_json;
};

class EventTimeline {
 public:
  EventTimeline(size_t max_events) : max_events_(max_events) {}
  void append(const std::string& kind, const std::string& actor, const std::string& detail_json,
              TimeSource& ts);
  const std::deque<TimelineRecord>& events() const { return events_; }
  std::vector<TimelineRecord> since(uint64_t seq) const;
  size_t size() const { return events_.size(); }

 private:
  std::deque<TimelineRecord> events_;
  size_t max_events_;
  uint64_t next_seq_ = 1;
};

// ---- tail fanout: live log tail to HQ and Configuration (§7.1) ----
class TailFanout {
 public:
  using Subscriber = std::function<void(const std::string& json_line)>;
  int subscribe(Subscriber s);
  void unsubscribe(int id);
  void publish(const std::string& json_line);

 private:
  struct Sub {
    int id;
    Subscriber fn;
    std::deque<std::string> pending;  // bounded per subscriber
  };
  std::vector<Sub> subs_;
  int next_id_ = 1;
  std::mutex mu_;
};

// ---- northbound spool ----
class NorthboundSpool {
 public:
  NorthboundSpool(IFileSystem& fs, const SpoolConfig& cfg, BudgetManager* budgets);

  Error open();
  // Enqueue with the spool class; protected classes never drop.
  Result<bool> enqueue(SpoolPriority prio, const std::string& payload, uint64_t mono_ns);
  // Drain callback gets each record; return false to pause (backpressure).
  // Honours HQ backpressure and jittered drain pacing (§3.4, §11).
  struct DrainReport {
    size_t sent = 0;
    size_t dropped_telemetry = 0;
    size_t dropped_logref = 0;
    size_t dropped_event = 0;
    size_t refused_protected = 0;
    bool paused = false;
  };
  DrainReport drain(const std::function<bool(const DiskSpool::Entry&)>& send, size_t max_batch);
  const DiskSpool::Stats& stats() const { return spool_.stats(); }
  size_t size() const { return spool_.size(); }

 private:
  IFileSystem& fs_;
  DiskSpool spool_;
  BudgetManager* budgets_;
};

// ---- HQ session state machine ----
class HqSession {
 public:
  enum class State { Disconnected, Backoff, Connected, Draining, Backpressured };
  static const char* state_name(State s) {
    switch (s) {
      case State::Disconnected: return "disconnected";
      case State::Backoff: return "backoff";
      case State::Connected: return "connected";
      case State::Draining: return "draining";
      case State::Backpressured: return "backpressured";
    }
    return "?";
  }

  HqSession(double backoff_base_s = 1.0, double backoff_max_s = 60.0)
      : backoff_(backoff_base_s, backoff_max_s) {}

  State state() const { return state_; }
  double next_delay_s() { return backoff_.next(); }
  void on_connected() {
    state_ = State::Connected;
    backoff_.reset();
  }
  void on_disconnected() {
    state_ = State::Backoff;
  }
  void on_backpressure(bool pause) {
    state_ = pause ? State::Backpressured : State::Draining;
  }
  void set_draining() { state_ = State::Draining; }
  void set_disconnected() { state_ = State::Disconnected; }
  uint64_t attempts() const { return backoff_.attempts(); }

 private:
  State state_ = State::Disconnected;
  JitteredBackoff backoff_;
};

}  // namespace istate
}  // namespace instr