#include "instr/common/atomic_file.h"

#include <unistd.h>

#include "instr/common/crypto.h"

namespace instr {

Error AtomicFile::write(IFileSystem& fs, const std::string& path, const std::string& data) {
  std::string tmp = path + ".tmp-" + std::to_string(::getpid());
  Error e = fs.write(tmp, data);
  if (!e.ok()) return e;
  e = fs.fsync_path(tmp);
  if (!e.ok()) {
    fs.remove(tmp, false);
    return e;
  }
  e = fs.rename(tmp, path);
  if (!e.ok()) {
    fs.remove(tmp, false);
    return e;
  }
  // fsync the containing directory so the rename is durable.
  size_t pos = path.find_last_of('/');
  if (pos != std::string::npos && pos > 0) fs.fsync_path(path.substr(0, pos));
  return Error::none();
}

Error AtomicFile::write_protected(IFileSystem& fs, const std::string& path,
                                  const std::string& data) {
  // Redundant copy per level (REQ §4/§6): the .bak copy holds the same new
  // content; if a crash interrupts the update cycle, the old .bak from the
  // previous completed write remains last-known-good.
  Error e = write(fs, path, data);
  if (!e.ok()) return e;
  e = fs.write(backup_path(path), data);
  if (!e.ok()) return e;
  // Checksum sidecars for both copies.
  std::string sum = crypto::sha256(data).hex();
  e = write(fs, checksum_path(path), sum);
  if (!e.ok()) return e;
  return write(fs, backup_path(path) + ".checksum", sum);
}

Result<std::string> AtomicFile::read_protected(IFileSystem& fs, const std::string& path) {
  auto primary = fs.read(path);
  if (primary.ok()) {
    auto sum = fs.read(checksum_path(path));
    if (!sum.ok()) return primary;  // no checksum recorded — unprotected legacy file
    if (crypto::sha256(*primary).hex() == *sum) return primary;
    // Primary fails its checksum: fall to the redundant copy; the failing
    // primary is never silently returned (REQ §11).
  }
  auto bak = fs.read(backup_path(path));
  if (bak.ok()) {
    auto bsum = fs.read(backup_path(path) + ".checksum");
    if (bsum.ok() && crypto::sha256(*bak).hex() == *bsum) return bak;
    if (!fs.exists(backup_path(path) + ".checksum")) return bak;  // legacy backup
  }
  return Result<std::string>::fail(Error::make(Errc::Corrupt,
                                               "both primary and redundant copy failed checks",
                                               path));
}

}  // namespace instr