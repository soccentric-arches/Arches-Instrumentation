#include "instr/istate/northbound.h"

#include "instr/istate/pst.h"

namespace instr {
namespace istate {

// ---------------- EventTimeline ----------------

void EventTimeline::append(const std::string& kind, const std::string& actor,
                           const std::string& detail_json, TimeSource& ts) {
  TimelineRecord ev;
  ev.seq = next_seq_++;
  ev.ts.mono_ns = ts.mono_ns();
  ev.ts.boot_id = ts.boot_id();
  ev.ts.wall_ns = ts.wall_ns();
  ev.ts.seq = ts.next_seq();
  ev.ts.wall_valid = true;
  ev.kind = kind;
  ev.actor = actor;
  ev.detail_json = detail_json;
  events_.push_back(ev);
  while (events_.size() > max_events_) events_.pop_front();
}

std::vector<TimelineRecord> EventTimeline::since(uint64_t seq) const {
  std::vector<TimelineRecord> out;
  for (const auto& e : events_)
    if (e.seq > seq) out.push_back(e);
  return out;
}

// ---------------- TailFanout ----------------

int TailFanout::subscribe(Subscriber s) {
  std::lock_guard<std::mutex> lk(mu_);
  int id = next_id_++;
  Sub sub;
  sub.id = id;
  sub.fn = std::move(s);
  subs_.push_back(std::move(sub));
  return id;
}

void TailFanout::unsubscribe(int id) {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto it = subs_.begin(); it != subs_.end(); ++it)
    if (it->id == id) {
      subs_.erase(it);
      break;
    }
}

void TailFanout::publish(const std::string& json_line) {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& sub : subs_) {
    sub.pending.push_back(json_line);
    // Bounded per subscriber: drop oldest lines when the slow consumer lags.
    while (sub.pending.size() > 256) sub.pending.pop_front();
    if (sub.fn) sub.fn(json_line);
  }
}

// ---------------- NorthboundSpool ----------------

NorthboundSpool::NorthboundSpool(IFileSystem& fs, const SpoolConfig& cfg, BudgetManager* budgets)
    : fs_(fs), spool_(fs, cfg), budgets_(budgets) {}

Error NorthboundSpool::open() { return spool_.open(); }

Result<bool> NorthboundSpool::enqueue(SpoolPriority prio, const std::string& payload,
                                      uint64_t mono_ns) {
  // Budget enforcement before the write: alarm before the limit (§6).
  if (budgets_) {
    auto r = budgets_->reserve("spool", payload.size());
    if (!r.ok()) {
      // Budget exceeded → the spool drop policy still applies for telemetry;
      // protected records get refused loudly by the DiskSpool itself.
      return Result<bool>::fail(r.error);
    }
  }
  int dropped = 0;
  Error e = spool_.append(prio, payload, mono_ns, &dropped);
  if (!e.ok()) return Result<bool>::fail(e);
  if (budgets_) budgets_->record_usage("spool", spool_.bytes());
  return Result<bool>::good(true);
}

NorthboundSpool::DrainReport NorthboundSpool::drain(
    const std::function<bool(const DiskSpool::Entry&)>& send, size_t max_batch) {
  DrainReport rep;
  auto records = spool_.peek(max_batch);
  for (const auto& r : records) {
    // HQ backpressure honoured: a false return pauses the drain (§11).
    if (!send(r)) {
      rep.paused = true;
      break;
    }
    ++rep.sent;
    switch (r.priority) {
      case SpoolPriority::Telemetry: ++rep.dropped_telemetry; break;
      case SpoolPriority::LogRef: ++rep.dropped_logref; break;
      case SpoolPriority::Event: ++rep.dropped_event; break;
      default: break;
      case SpoolPriority::Fault:
      case SpoolPriority::Alarm: break;
    }
  }
  if (rep.sent > 0) spool_.pop_front(rep.sent);
  const auto& st = spool_.stats();
  rep.dropped_telemetry = st.dropped_telemetry;
  rep.dropped_logref = st.dropped_logref;
  rep.dropped_event = st.dropped_event;
  rep.refused_protected = st.refused_protected;
  if (budgets_) budgets_->record_usage("spool", spool_.bytes());
  return rep;
}

}  // namespace istate
}  // namespace instr