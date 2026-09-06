// istate — /pst store tests (REQ §6, §11): virgin detection, seeding,
// corrupt fallback, budgets, migration, reset semantics.
#include <gtest/gtest.h>

#include "instr/common/filesystem.h"
#include "instr/istate/pst.h"

using namespace instr;
using namespace instr::istate;

struct PstFixture {
  SimulatedFileSystem fs;
  BudgetManager budgets{fs, "/pst"};
  PstStore pst{fs, "/pst", budgets};
  std::map<std::string, std::string> defaults;
  PstFixture() {
    defaults["istate"] = "a: 1\n";
    defaults["networkd"] = "mode: dhcp\n";
    defaults["monitord"] = "interval: 5\n";
  }
};

TEST(PstStore, MountDetectsVirginAndSeeds) {
  PstFixture f;
  auto rep = f.pst.mount();
  EXPECT_TRUE(rep.ok);
  EXPECT_TRUE(rep.virgin);
  EXPECT_TRUE(f.pst.seed_defaults(f.defaults).ok());
  auto rep2 = f.pst.mount();
  EXPECT_FALSE(rep2.virgin);
  auto doc = f.pst.load_config("monitord");
  ASSERT_TRUE(doc.ok());
  EXPECT_EQ((*doc).get_int("interval"), 5);
}

TEST(PstStore, CorruptFileFallsBackToRedundantCopy) {
  PstFixture f;
  f.pst.mount();
  f.pst.seed_defaults(f.defaults);
  // Rewrite (creates .bak), then corrupt the primary.
  ConfigDocument d;
  d.set("interval", "10", Origin::Local);
  f.pst.save_config("monitord", d);
  f.fs.corrupt_reads_of("/pst/system/monitord/config.yaml", 1);
  auto doc = f.pst.load_config("monitord");
  ASSERT_TRUE(doc.ok());
  EXPECT_EQ((*doc).get_int("interval"), 10);  // recovered from .bak
}

TEST(PstStore, SchemaVersionAndMigration) {
  PstFixture f;
  f.pst.mount();
  f.pst.seed_defaults(f.defaults);
  EXPECT_EQ(f.pst.schema_version(), "1");
  std::map<std::string, std::string> new_release = {
      {"monitord", "interval: 5\ndisk_warn_pct: 90\n"}};
  auto added = f.pst.migrate(new_release);
  ASSERT_EQ(added.size(), 1u);
  EXPECT_EQ(added[0], "monitord/disk_warn_pct");  // new keys added...
  auto doc = f.pst.load_config("monitord");
  ASSERT_TRUE(doc.ok());
  // ...existing operator values preserved...
  ConfigDocument d;
  d.set("interval", "42", Origin::Local);
  f.pst.save_config("monitord", d);
  auto added2 = f.pst.migrate(new_release);
  EXPECT_TRUE(added2.empty());
  doc = f.pst.load_config("monitord");
  EXPECT_EQ((*doc).get_int("interval"), 42);
}

TEST(PstStore, SystemResetWipesSystemAndStateKeepsFactoryBackup) {
  PstFixture f;
  f.pst.mount();
  f.pst.seed_defaults(f.defaults);
  f.fs.write("/pst/factory/identity/identity.yaml", "serial: X\n");
  f.fs.write("/pst/backup/backup-1.abk", "archivedata");
  f.fs.write("/pst/state/spool/spool-1.seg", "spooldata");
  // Operator edit that must not survive the reset.
  ConfigDocument d;
  d.set("interval", "99", Origin::Local);
  f.pst.save_config("monitord", d);

  EXPECT_TRUE(f.pst.system_reset(f.defaults).ok());
  EXPECT_TRUE(f.fs.exists("/pst/factory/identity/identity.yaml"));
  EXPECT_TRUE(f.fs.exists("/pst/backup/backup-1.abk"));
  EXPECT_FALSE(f.fs.exists("/pst/state/spool/spool-1.seg"));
  auto doc = f.pst.load_config("monitord");
  ASSERT_TRUE(doc.ok());
  EXPECT_EQ((*doc).get_int("interval"), 5);  // reseeded from image defaults
}

TEST(PstStore, FactoryResetWipesEverything) {
  PstFixture f;
  f.pst.mount();
  f.pst.seed_defaults(f.defaults);
  f.fs.write("/pst/factory/identity/identity.yaml", "serial: X\n");
  EXPECT_TRUE(f.pst.factory_reset().ok());
  EXPECT_FALSE(f.fs.exists("/pst/factory/identity/identity.yaml"));
  EXPECT_FALSE(f.fs.exists("/pst/system/.schema-version"));  // reseeded on next boot
  EXPECT_FALSE(f.fs.exists("/pst/system/istate"));
}

TEST(BudgetManager, EnforcesWithAlarmBeforeLimit) {
  PstFixture f;
  f.budgets.set_budget("logs", 1000);
  auto r = f.budgets.reserve("logs", 700);
  EXPECT_TRUE(r.ok());
  f.budgets.record_usage("logs", 700);
  EXPECT_TRUE(f.budgets.over_warning().empty());  // 70% < 80%
  f.budgets.record_usage("logs", 850);
  auto warned = f.budgets.over_warning();
  ASSERT_EQ(warned.size(), 1u);
  EXPECT_EQ(warned[0], "logs");  // alarm raised BEFORE the limit
  EXPECT_TRUE(f.budgets.over_limit().empty());
  f.budgets.record_usage("logs", 1001);
  EXPECT_FALSE(f.budgets.over_limit().empty());
  EXPECT_FALSE(f.budgets.reserve("logs", 100).ok());  // hard enforcement
}
