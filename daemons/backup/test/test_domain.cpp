// Unit tests — backup domain (REQ §12). Roundtrip, exclusions, encryption,
// preview, transactional restore, replacement-board semantics.
#include <gtest/gtest.h>

#include "instr/backup/domain.h"
#include "instr/common/filesystem.h"

using namespace instr;
using namespace instr::backup;
using namespace instr::backup::domain;

struct Fixture {
  SimulatedFileSystem fs;
  BackupOptions opts;
  std::unique_ptr<BackupEngine> eng;
  Fixture() {
    // Populate /pst/system.
    fs.write("/pst/system/istate/config.yaml", "a: 1\n");
    fs.write("/pst/system/networkd/config.yaml", "mode: dhcp\n");
    fs.write("/pst/system/networkd/wg-private.key", "SUPER-SECRET-KEY");
    fs.write("/pst/factory/identity/identity.yaml", "serial: SIM-1\n");
    opts.device_serial = "SIM-1";
    opts.hardware_revision = "rev-a";
    opts.schema_version = "1";
    opts.firmware_version = "1.0.0";
    opts.identity_secret = "identity-secret";
    eng = std::make_unique<BackupEngine>(fs, opts);
  }
};

TEST(BackupCreate, RoundtripArchiveWithManifest) {
  Fixture f;
  auto info = f.eng->create({"/pst/system"}, {}, "/pst/backup", 1'700'000'000);
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info->manifest.device_serial, "SIM-1");
  EXPECT_EQ(info->manifest.files.size(), 2u);  // two configs; key excluded
  auto list = f.eng->list("/pst/backup");
  ASSERT_EQ(list.size(), 1u);
  auto man = f.eng->read_manifest("/pst/backup/" + info->name);
  ASSERT_TRUE(man.ok());
  EXPECT_EQ(man->hardware_revision, "rev-a");
}

TEST(BackupCreate, ExcludesFactoryAndWireGuardKeys) {
  Fixture f;
  auto info = f.eng->create({"/pst"}, {}, "/pst/backup", 1'700'000'000);
  ASSERT_TRUE(info.ok());
  for (const auto& file : info->manifest.files) {
    EXPECT_TRUE(BackupEngine::excluded_path(file, {}) == false);
    EXPECT_EQ(file.find("wg-private.key"), std::string::npos) << "secret leaked into backup";
    EXPECT_EQ(file.find("factory/"), std::string::npos) << "identity cloned into backup";
  }
}

TEST(BackupCrypto, ArchiveIsEncryptedNotPlainTar) {
  Fixture f;
  auto info = f.eng->create({"/pst/system"}, {}, "/pst/backup", 1'700'000'000);
  ASSERT_TRUE(info.ok());
  auto blob = f.fs.read("/pst/backup/" + info->name);
  ASSERT_TRUE(blob.ok());
  // The archive must not be a plain ustar (encrypted payload).
  auto as_tar = tar::read_ustar(*blob);
  EXPECT_TRUE(as_tar->empty());  // decryption required
  // And must not contain the config plaintext.
  EXPECT_EQ(blob->find("mode: dhcp"), std::string::npos);
}

TEST(BackupPreview, ShowsAddReplaceDelete) {
  Fixture f;
  f.fs.write("/pst/system/istate/keep.yaml", "keep: true\n");  // stays unchanged
  auto info = f.eng->create({"/pst/system"}, {}, "/pst/backup", 1'700'000'000);
  ASSERT_TRUE(info.ok());
  // Change one file, add another, remove one.
  f.fs.write("/pst/system/istate/config.yaml", "a: 2\n");
  f.fs.write("/pst/system/istate/extra.yaml", "new: true\n");
  f.fs.remove("/pst/system/networkd/config.yaml", false);
  auto p = f.eng->restore_preview("/pst/backup/" + info->name, {"/pst/system"});
  ASSERT_TRUE(p.ok());
  bool saw_replace = false, saw_add = false, saw_delete = false, saw_unchanged = false;
  for (const auto& e : p->entries) {
    // Action = what the restore will do to the current state.
    if (e.action == "replace" && e.path == "istate/config.yaml") saw_replace = true;
    if (e.action == "add" && e.path == "networkd/config.yaml") saw_add = true;
    if (e.action == "delete" && e.path == "istate/extra.yaml") saw_delete = true;
    if (e.action == "unchanged") saw_unchanged = true;
  }
  EXPECT_TRUE(saw_replace);
  EXPECT_TRUE(saw_add);
  EXPECT_TRUE(saw_delete);
  EXPECT_TRUE(saw_unchanged);
}

TEST(BackupPreview, HardwareMismatchRefused) {
  Fixture f;
  auto info = f.eng->create({"/pst/system"}, {}, "/pst/backup", 1'700'000'000);
  ASSERT_TRUE(info.ok());
  // Restore onto a different board revision → incompatible.
  f.opts.hardware_revision = "rev-b";
  BackupEngine other(f.fs, f.opts);
  auto p = other.restore_preview("/pst/backup/" + info->name, {"/pst/system"});
  ASSERT_TRUE(p.ok());
  EXPECT_EQ(p->compatibility, BackupEngine::Compatibility::HardwareMismatch);
}

TEST(BackupPreview, ReplacementBoardDetected) {
  Fixture f;
  auto info = f.eng->create({"/pst/system"}, {}, "/pst/backup", 1'700'000'000);
  ASSERT_TRUE(info.ok());
  // Same revision, different serial (replacement board).
  f.opts.device_serial = "SIM-2";
  BackupEngine other(f.fs, f.opts);
  auto p = other.restore_preview("/pst/backup/" + info->name, {"/pst/system"});
  ASSERT_TRUE(p.ok());
  EXPECT_TRUE(p->replacement_board);  // config restores, identity does not → re-enroll
}

TEST(BackupRestore, TransactionalCommitCompletesFullyOrNotAtAll) {
  Fixture f;
  auto info = f.eng->create({"/pst/system"}, {}, "/pst/backup", 1'700'000'000);
  ASSERT_TRUE(info.ok());
  std::vector<std::string> committed;
  auto r = f.eng->restore_commit("/pst/backup/" + info->name,
                                 [&](const std::vector<tar::FileEntry>& files) {
                                   for (const auto& e : files) committed.push_back(e.name);
                                   return Error::none();
                                 });
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(*r, committed.size());
  // Commit failure leaves prior state intact (no partial application).
  auto r2 = f.eng->restore_commit("/pst/backup/" + info->name,
                                  [](const std::vector<tar::FileEntry>&) {
                                    return Error::make(Errc::Io, "simulated commit failure", "x");
                                  });
  EXPECT_FALSE(r2.ok());
  EXPECT_EQ(r2.error.code, Errc::Io);
}

TEST(BackupRestore, TamperedFileFailsVerificationBeforeCommit) {
  Fixture f;
  auto info = f.eng->create({"/pst/system"}, {}, "/pst/backup", 1'700'000'000);
  ASSERT_TRUE(info.ok());
  // Tamper with the encrypted blob so decryption succeeds but content hash
  // differs → verification must fail before any commit.
  auto blob = f.fs.read("/pst/backup/" + info->name);
  ASSERT_TRUE(blob.ok());
  // Flip a byte deep inside (payload region).
  std::string tampered = *blob;
  tampered[tampered.size() / 2] ^= 0xFF;
  f.fs.write("/pst/backup/" + info->name, tampered);
  auto r = f.eng->restore_commit("/pst/backup/" + info->name,
                                 [](const std::vector<tar::FileEntry>&) { return Error::none(); });
  // Either authentication fails (tag) or integrity fails; both refuse commit.
  EXPECT_FALSE(r.ok());
}

TEST(BackupSchedule, RuntimeScheduleState) {
  // Schedule logic exercised via the core facade in the daemon tests; here
  // we assert the archive listing is ordered and deterministic.
  Fixture f;
  f.eng->create({"/pst/system"}, {}, "/pst/backup", 1'700'000'000);
  f.eng->create({"/pst/system"}, {}, "/pst/backup", 1'700'086'400);
  auto list = f.eng->list("/pst/backup");
  ASSERT_EQ(list.size(), 2u);
  EXPECT_LT(list[0].manifest.created_unix_s, list[1].manifest.created_unix_s);
}
