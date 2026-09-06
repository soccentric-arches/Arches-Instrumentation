// istate — config service tests (REQ §9, §10): origin rules, signed HQ
// pushes, audit trail, drift report.
#include <gtest/gtest.h>

#include "instr/common/filesystem.h"
#include "instr/istate/config_service.h"

using namespace instr;
using namespace instr::istate;

struct CfgFixture {
  SimulatedFileSystem fs;
  BudgetManager budgets{fs, "/pst"};
  PstStore pst{fs, "/pst", budgets};
  ConfigService svc{pst, fs};
  std::map<std::string, std::string> defaults{{"networkd", "mode: dhcp\ninterval: 5\n"}};
  CfgFixture() {
    pst.mount();
    pst.seed_defaults(defaults);
    svc.set_schema("networkd", *load_schema_text(R"({"type":"object","properties":{
      "mode":{"type":"string","enum":["dhcp","static"]},
      "interval":{"type":"number"}}})"));
  }
};

TEST(ConfigService, GetReturnsSeededDefaults) {
  CfgFixture f;
  auto doc = f.svc.get("networkd");
  ASSERT_TRUE(doc.ok());
  EXPECT_EQ((*doc).get_string("mode"), "dhcp");
  EXPECT_EQ((*doc).origin_of("mode"), Origin::Default);
}

TEST(ConfigService, SetValidatesAgainstSchemaLoudly) {
  CfgFixture f;
  auto r = f.svc.set("networkd", "mode: pppoe\n", Origin::Local, false);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error.find("mode"), std::string::npos);
  // Valid set works.
  auto r2 = f.svc.set("networkd", "mode: static\n", Origin::Local, false);
  EXPECT_TRUE(r2.ok);
  auto doc = f.svc.get("networkd");
  EXPECT_EQ((*doc).get_string("mode"), "static");
}

TEST(ConfigService, HqPushCannotOverwriteLocalWithoutForce) {
  CfgFixture f;
  EXPECT_TRUE(f.svc.set("networkd", "mode: static\ninterval: 9\n", Origin::Local, false).ok);
  // HQ push (unsigned path used only for the merge rules here): both keys
  // are operator-edited → both protected.
  auto r = f.svc.set("networkd", "mode: dhcp\ninterval: 7\n", Origin::Hq, false);
  EXPECT_TRUE(r.ok);
  ASSERT_EQ(r.rejected.size(), 2u);  // mode + interval preserved
  auto doc = f.svc.get("networkd");
  EXPECT_EQ((*doc).get_string("mode"), "static");
  EXPECT_EQ((*doc).get_int("interval"), 9);
}

TEST(ConfigService, SignedPushVerifiedBeforeApply) {
  CfgFixture f;
  std::string secret = "device-enrollment-secret";
  std::string yaml = "mode: dhcp\ninterval: 3\n";
  auto digest = crypto::hmac_sha256(secret, "networkd|" + yaml);
  std::string hmac = crypto::hex_encode(std::string(reinterpret_cast<const char*>(digest.b.data()), 32));
  // Tampered signature → refused, nothing written.
  auto bad = f.svc.apply_signed("networkd", yaml, "key-1", "00" + hmac.substr(2), secret);
  EXPECT_FALSE(bad.ok());
  EXPECT_EQ(bad.error.code, Errc::SignatureInvalid);
  auto doc = f.svc.get("networkd");
  EXPECT_EQ((*doc).get_int("interval"), 5);  // untouched
  // Correct signature → applied.
  auto good = f.svc.apply_signed("networkd", yaml, "key-1", hmac, secret);
  ASSERT_TRUE(good.ok());
  doc = f.svc.get("networkd");
  EXPECT_EQ((*doc).get_int("interval"), 3);
  EXPECT_EQ((*doc).origin_of("interval"), Origin::Hq);
}

TEST(ConfigService, DriftReportShowsEffectiveConfigAndOrigins) {
  CfgFixture f;
  f.svc.set("networkd", "interval: 9\n", Origin::Local, false);
  auto j = json_parse(f.svc.drift_report_json("networkd"));
  ASSERT_TRUE(j.ok());
  EXPECT_EQ((*j).find("effective")->find("interval")->as_number(), 9);
  EXPECT_EQ((*j).find("origins")->find("interval")->as_string(), "local");
  EXPECT_EQ((*j).find("origins")->find("mode")->as_string(), "default");
}

TEST(AuditLog, RecordsWhoChangedWhatWhenFromWhere) {
  CfgFixture f;
  ManualTimeSource ts;
  AuditLog audit(f.fs, "/pst/state/audit.log", 100);
  audit.append("alice", "configuration-ui", "config_set", "{\"daemon\":\"networkd\"}", ts);
  audit.append("hq", "hq-tunnel", "update", "{\"to\":\"2.0.0\"}", ts);
  EXPECT_TRUE(audit.persist().ok());
  AuditLog loaded(f.fs, "/pst/state/audit.log", 100);
  ASSERT_TRUE(loaded.load().ok());
  ASSERT_EQ(loaded.entries().size(), 2u);
  EXPECT_EQ(loaded.entries()[0].actor, "alice");
  EXPECT_EQ(loaded.entries()[0].source, "configuration-ui");
  EXPECT_EQ(loaded.entries()[1].action, "update");
}

TEST(AuditLog, BoundedRing) {
  CfgFixture f;
  ManualTimeSource ts;
  AuditLog audit(f.fs, "/pst/state/audit2.log", 10);
  for (int i = 0; i < 100; ++i)
    audit.append("actor", "src", "action", "{\"i\":" + std::to_string(i) + "}", ts);
  EXPECT_EQ(audit.entries().size(), 10u);
  EXPECT_EQ(audit.entries().back().detail_json, "{\"i\":99}");
}
