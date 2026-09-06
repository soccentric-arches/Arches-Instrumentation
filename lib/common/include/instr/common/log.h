// Arches Instrumentation — structured, levelled, journal-aware, rate-limited
// logging with per-source runtime level control and secret redaction
// (REQ §4 Logging, §10 Secret hygiene: enforced with a test, not a convention).
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "instr/common/timestamps.h"

namespace instr {

enum class LogLevel { Trace = 0, Debug = 1, Info = 2, Warning = 3, Error = 4, Critical = 5 };

const char* log_level_name(LogLevel);
LogLevel log_level_from_name(const std::string&);  // "INFO" -> Info; unknown -> Info

struct LogRecord {
  LogLevel level = LogLevel::Info;
  std::string source;
  std::string message;
  std::map<std::string, std::string> fields;
  uint64_t mono_ns = 0;
  int64_t wall_ns = 0;
  bool wall_valid = false;
};

class ISink {
 public:
  virtual ~ISink() = default;
  virtual void write(const LogRecord&) = 0;
};

// Structured key=value line on stderr; journald picks it up verbatim on
// targets (journal-aware).
class StderrSink : public ISink {
 public:
  void write(const LogRecord&) override;
};

// Bounded in-memory ring; used by tests and crash forensics.
class MemorySink : public ISink {
 public:
  explicit MemorySink(size_t capacity = 1024) : cap_(capacity) {}
  void write(const LogRecord&) override;
  const std::deque<LogRecord>& records() const { return buf_; }
  std::vector<LogRecord> snapshot() const { return {buf_.begin(), buf_.end()}; }
  void clear() { buf_.clear(); }

 private:
  size_t cap_;
  std::deque<LogRecord> buf_;
};

// Secret hygiene guard. Field values whose key looks like a credential are
// redacted; raw credential material in message bodies is masked. Enforced by
// the secret-leak test (REQ §12).
class Redactor {
 public:
  static const std::vector<std::string>& secret_key_names();
  static bool is_secret_key(const std::string& key);
  static std::string redact_value(const std::string& key, const std::string& value);
  static std::string redact_text(const std::string& text);
};

class Logger {
 public:
  Logger(ISink& sink, TimeSource& ts) : sink_(&sink), ts_(&ts) {}

  void set_level(LogLevel l) { level_ = l; }
  LogLevel level() const { return level_; }
  // Per-source runtime level control (REQ §4).
  void set_source_level(const std::string& source, LogLevel l) { source_levels_[source] = l; }
  void clear_source_levels() { source_levels_.clear(); }

  // Rate limit per source: max N messages/s, surplus dropped and summarised.
  void set_rate_limit(const std::string& source, double max_per_s);
  void clear_rate_limits() { rate_limits_.clear(); }

  void log(LogLevel lvl, const std::string& source, const std::string& msg,
           const std::map<std::string, std::string>& fields = {});

  void trace(const std::string& s, const std::string& msg, const std::map<std::string, std::string>& f = {}) { log(LogLevel::Trace, s, msg, f); }
  void debug(const std::string& s, const std::string& msg, const std::map<std::string, std::string>& f = {}) { log(LogLevel::Debug, s, msg, f); }
  void info(const std::string& s, const std::string& msg, const std::map<std::string, std::string>& f = {}) { log(LogLevel::Info, s, msg, f); }
  void warning(const std::string& s, const std::string& msg, const std::map<std::string, std::string>& f = {}) { log(LogLevel::Warning, s, msg, f); }
  void error(const std::string& s, const std::string& msg, const std::map<std::string, std::string>& f = {}) { log(LogLevel::Error, s, msg, f); }
  void critical(const std::string& s, const std::string& msg, const std::map<std::string, std::string>& f = {}) { log(LogLevel::Critical, s, msg, f); }

  // Exposed for tests: how many messages were dropped by rate limiting.
  uint64_t dropped_by_rate_limit() const { return dropped_; }

 private:
  ISink* sink_;
  TimeSource* ts_;
  LogLevel level_ = LogLevel::Info;
  std::map<std::string, LogLevel> source_levels_;
  struct RateBucket { double tokens; double last_s; double rate; };
  std::map<std::string, RateBucket> rate_limits_;
  std::map<std::string, uint64_t> suppressed_;
  uint64_t dropped_ = 0;
  double now_s();
};

}  // namespace instr