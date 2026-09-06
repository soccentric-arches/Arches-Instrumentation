// Arches Instrumentation — filesystem abstraction (REQ §4, §12).
// Domain logic that must touch storage goes through this interface so every
// component is unit-testable without a real filesystem and fault injection
// (corrupt /pst, full disk, storage errors) is structural, not bolted on.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "instr/common/error.h"

namespace instr {

class IFileSystem {
 public:
  virtual ~IFileSystem() = default;

  virtual bool exists(const std::string& path) = 0;
  virtual Result<std::string> read(const std::string& path) = 0;
  // Plain write (not atomic); callers needing atomicity use AtomicFile.
  virtual Error write(const std::string& path, const std::string& data,
                      bool append = false) = 0;
  virtual Error mkdirs(const std::string& path) = 0;
  virtual Error remove(const std::string& path, bool recursive = false) = 0;
  virtual Error rename(const std::string& from, const std::string& to) = 0;
  virtual Result<std::vector<std::string>> list_dir(const std::string& path) = 0;
  virtual Result<uint64_t> file_size(const std::string& path) = 0;
  virtual Result<uint64_t> free_space(const std::string& path) = 0;
  virtual Error fsync_path(const std::string& path) = 0;
  virtual std::string join(const std::string& a, const std::string& b) const {
    return a.empty() ? b : (a.back() == '/' ? a + b : a + "/" + b);
  }
};

// POSIX implementation: temp+fsync+rename discipline lives in atomic_file.h;
// this type provides the primitives.
class NativeFileSystem : public IFileSystem {
 public:
  bool exists(const std::string& path) override;
  Result<std::string> read(const std::string& path) override;
  Error write(const std::string& path, const std::string& data, bool append = false) override;
  Error mkdirs(const std::string& path) override;
  Error remove(const std::string& path, bool recursive) override;
  Error rename(const std::string& from, const std::string& to) override;
  Result<std::vector<std::string>> list_dir(const std::string& path) override;
  Result<uint64_t> file_size(const std::string& path) override;
  Result<uint64_t> free_space(const std::string& path) override;
  Error fsync_path(const std::string& path) override;
};

// In-memory implementation with fault injectors (REQ §12 fault injection:
// storage errors, corrupt reads, full disk, failed renames).
class SimulatedFileSystem : public IFileSystem {
 public:
  bool exists(const std::string& path) override;
  Result<std::string> read(const std::string& path) override;
  Error write(const std::string& path, const std::string& data, bool append = false) override;
  Error mkdirs(const std::string& path) override;
  Error remove(const std::string& path, bool recursive) override;
  Error rename(const std::string& from, const std::string& to) override;
  Result<std::vector<std::string>> list_dir(const std::string& path) override;
  Result<uint64_t> file_size(const std::string& path) override;
  Result<uint64_t> free_space(const std::string& path) override;
  Error fsync_path(const std::string& path) override;

  // ---- fault injection knobs ----
  // Fail writes from the n-th write onwards (1-based; 0 disables).
  void fail_writes_after(int n) { fail_writes_after_ = n; }
  // Corrupt data returned by reads of `path` from the n-th read onwards
  // (simulates bit rot / torn sector).
  void corrupt_reads_of(const std::string& path, int from_read) {
    corrupt_[path] = from_read;
  }
  // Clamp reported free space (simulates a filling / full partition).
  void set_free_space(uint64_t bytes) { free_space_ = bytes; }
  void fail_renames(bool v) { fail_renames_ = v; }
  void fail_mkdirs(bool v) { fail_mkdirs_ = v; }
  void reset_injections() {
    fail_writes_after_ = 0;
    corrupt_.clear();
    fail_renames_ = false;
    fail_mkdirs_ = false;
  }

  // Test introspection.
  const std::map<std::string, std::string>& raw() const { return files_; }
  uint64_t used_bytes() const;

 private:
  bool is_dir(const std::string& p) const;
  std::map<std::string, std::string> files_;   // files only
  std::map<std::string, bool> dirs_;           // dir → recursive flag
  int fail_writes_after_ = 0;
  int write_count_ = 0;
  std::map<std::string, int> corrupt_;
  std::map<std::string, int> read_count_;
  uint64_t free_space_ = 1ull << 40;
  bool fail_renames_ = false;
  bool fail_mkdirs_ = false;
};

}  // namespace instr