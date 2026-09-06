// Arches Instrumentation — common result/error model (REQ §4, §14).
// One result/error model across the suite; no exceptions across ABI
// boundaries; every rejected input produces a diagnosable message.
#pragma once
#include <string>
#include <utility>

namespace instr {

enum class Errc {
  Ok = 0,
  InvalidArgument,
  NotFound,
  Io,
  Corrupt,
  BudgetExceeded,
  SignatureInvalid,
  VersionMismatch,
  Busy,
  Unavailable,
  NotSupported,
  Internal,
};

inline const char* errc_name(Errc c) {
  switch (c) {
    case Errc::Ok: return "ok";
    case Errc::InvalidArgument: return "invalid-argument";
    case Errc::NotFound: return "not-found";
    case Errc::Io: return "io";
    case Errc::Corrupt: return "corrupt";
    case Errc::BudgetExceeded: return "budget-exceeded";
    case Errc::SignatureInvalid: return "signature-invalid";
    case Errc::VersionMismatch: return "version-mismatch";
    case Errc::Busy: return "busy";
    case Errc::Unavailable: return "unavailable";
    case Errc::NotSupported: return "not-supported";
    case Errc::Internal: return "internal";
  }
  return "unknown";
}

struct Error {
  Errc code = Errc::Ok;
  std::string message;
  std::string context;  // file/line/operation that produced it

  bool ok() const { return code == Errc::Ok; }
  std::string str() const {
    std::string s = std::string(errc_name(code)) + ": " + message;
    if (!context.empty()) s += " (" + context + ")";
    return s;
  }
  static Error none() { return Error{}; }
  static Error make(Errc c, std::string msg, std::string ctx = "") {
    Error e; e.code = c; e.message = std::move(msg); e.context = std::move(ctx);
    return e;
  }
};

template <typename T>
struct Result {
  T value{};
  Error error;

  bool ok() const { return error.ok(); }
  const T& operator*() const { return value; }
  T& operator*() { return value; }
  const T* operator->() const { return &value; }
  T* operator->() { return &value; }

  static Result good(T v) { Result r; r.value = std::move(v); return r; }
  static Result fail(Error e) { Result r; r.error = std::move(e); return r; }
  static Result fail(Errc c, std::string msg, std::string ctx = "") {
    return fail(Error::make(c, std::move(msg), std::move(ctx)));
  }
};

// Specialisation for void-like success.
struct Unit {};
using Status = Result<Unit>;
inline Status good() { return Status::good(Unit{}); }
inline Status fail(Error e) { return Status::fail(std::move(e)); }
inline Status fail(Errc c, std::string msg, std::string ctx = "") {
  return Status::fail(Error::make(c, std::move(msg), std::move(ctx)));
}

}  // namespace instr