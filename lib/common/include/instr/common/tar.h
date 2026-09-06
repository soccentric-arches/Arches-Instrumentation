// Arches Instrumentation — minimal POSIX ustar archive writer/reader.
// Used by Backup/Restore and LogManager bundles: tar the files into an
// archive; restore by untarring (REQ §7.10). No external tar binary — the
// format is implemented here so it is testable and deterministic.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "instr/common/error.h"

namespace instr {
namespace tar {

struct FileEntry {
  std::string name;   // relative path inside the archive
  std::string data;
  uint32_t mode = 0644;
  uint64_t mtime = 0;
};

std::string write_ustar(const std::vector<FileEntry>& files);
Result<std::vector<FileEntry>> read_ustar(const std::string& archive);

}  // namespace tar
}  // namespace instr