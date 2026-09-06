#include "instr/common/log.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace instr {

const char* log_level_name(LogLevel l) {
  switch (l) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARNING";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Critical: return "CRITICAL";
  }
  return "?";
}

LogLevel log_level_from_name(const std::string& n) {
  std::string u;
  for (char c : n) u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (u == "TRACE") return LogLevel::Trace;
  if (u == "DEBUG") return LogLevel::Debug;
  if (u == "WARNING" || u == "WARN") return LogLevel::Warning;
  if (u == "ERROR") return LogLevel::Error;
  if (u == "CRITICAL" || u == "FATAL") return LogLevel::Critical;
  return LogLevel::Info;
}

// ---------------- sinks ----------------

void StderrSink::write(const LogRecord& r) {
  char tbuf[32];
  time_t secs = static_cast<time_t>(r.wall_ns / 1'000'000'000ll);
  struct tm tm;
  gmtime_r(&secs, &tm);
  strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  std::fprintf(stderr, "%s %s %s: %s", tbuf, log_level_name(r.level), r.source.c_str(),
               r.message.c_str());
  for (const auto& kv : r.fields) std::fprintf(stderr, " %s=%s", kv.first.c_str(), kv.second.c_str());
  std::fprintf(stderr, "\n");
}

void MemorySink::write(const LogRecord& r) {
  buf_.push_back(r);
  while (buf_.size() > cap_) buf_.pop_front();
}

// ---------------- redaction ----------------

const std::vector<std::string>& Redactor::secret_key_names() {
  static const std::vector<std::string> names = {
      "password", "passwd", "secret", "token", "credential", "private_key",
      "privatekey", "api_key", "apikey", "wg-private", "psk", "session_key",
  };
  return names;
}

static std::string lower(const std::string& s) {
  std::string o;
  for (char c : s) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}

bool Redactor::is_secret_key(const std::string& key) {
  const std::string k = lower(key);
  for (const auto& n : secret_key_names())
    if (k.find(n) != std::string::npos) return true;
  return false;
}

std::string Redactor::redact_value(const std::string& key, const std::string& value) {
  if (is_secret_key(key)) return "[REDACTED]";
  return redact_text(value);
}

std::string Redactor::redact_text(const std::string& text) {
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    // Redact "key=value" / "key: value" pairs where key looks like a secret.
    bool matched = false;
    for (const auto& n : secret_key_names()) {
      if (text.size() - i >= n.size() && lower(text.substr(i, n.size())) == n) {
        size_t j = i + n.size();
        if (j < text.size() && (text[j] == '=' || text[j] == ':' || text[j] == ' ')) {
          while (j < text.size() && (text[j] == ' ' || text[j] == '=' || text[j] == ':')) ++j;
          size_t end = j;
          while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
                 text[end] != ',' && text[end] != '"' && text[end] != ')')
            ++end;
          out += text.substr(i, j - i) + "[REDACTED]";
          i = end;
          matched = true;
          break;
        }
      }
    }
    if (!matched) {
      out += text[i];
      ++i;
    }
  }
  return out;
}

// ---------------- logger ----------------

double Logger::now_s() {
  return static_cast<double>(ts_->mono_ns()) / 1e9;
}

void Logger::set_rate_limit(const std::string& source, double max_per_s) {
  rate_limits_[source] = RateBucket{max_per_s, now_s(), max_per_s};
}

void Logger::log(LogLevel lvl, const std::string& source, const std::string& msg,
                 const std::map<std::string, std::string>& fields) {
  // Effective level: per-source override, else global.
  auto it = source_levels_.find(source);
  LogLevel eff = (it != source_levels_.end()) ? it->second : level_;
  if (lvl < eff) return;

  // Rate limiting per source (token bucket refill).
  auto rit = rate_limits_.find(source);
  if (rit != rate_limits_.end()) {
    double t = now_s();
    RateBucket& b = rit->second;
    b.tokens += (t - b.last_s) * b.rate;
    if (b.tokens > b.rate) b.tokens = b.rate;  // burst = 1 s worth
    b.last_s = t;
    if (b.tokens < 1.0) {
      ++dropped_;
      ++suppressed_[source];
      return;
    }
    b.tokens -= 1.0;
    auto sit = suppressed_.find(source);
    if (sit != suppressed_.end() && sit->second > 0) {
      LogRecord summary;
      summary.level = LogLevel::Warning;
      summary.source = source;
      summary.message = "suppressed " + std::to_string(sit->second) + " events";
      summary.mono_ns = ts_->mono_ns();
      summary.wall_ns = ts_->wall_ns();
      summary.wall_valid = true;
      sink_->write(summary);
      suppressed_.erase(source);
    }
  }

  LogRecord r;
  r.level = lvl;
  r.source = source;
  r.message = Redactor::redact_text(msg);
  r.mono_ns = ts_->mono_ns();
  r.wall_ns = ts_->wall_ns();
  r.wall_valid = true;
  for (const auto& kv : fields) r.fields[kv.first] = Redactor::redact_value(kv.first, kv.second);
  sink_->write(r);
}

}  // namespace instr