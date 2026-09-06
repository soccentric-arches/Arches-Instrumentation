// ============================================================================
//  Arches Instrumentation
//
//  Copyright (C) 2026 Soccentric. All rights reserved.
//  Author: Sandesh Ghimire (sandesh@soccentric)
//
//  This file is part of a proprietary product of Soccentric.
//
//  NO WARRANTY:
//  This software is provided "AS IS", without warranty of any kind, either
//  express or implied, including but not limited to the warranties of
//  merchantability, fitness for a particular purpose, and non-infringement.
//  In no event shall Soccentric be liable for any claim, damages, or other
//  liability arising from the use of this software.
//
//  This software may not be used, copied, modified, distributed, or
//  disclosed, in whole or in part, without the express prior written
//  consent of Soccentric.
// ============================================================================
/**
 * @file daemons/ota/test/test_domain.cpp
 * @brief ota domain (REQ §12). Full lifecycle: receive → verify →
 *
 * stage → apply → confirm / auto-rollback; anti-rollback; maintenance
 * windows; resume; bandwidth limiter.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#include <gtest/gtest.h>

#include "instr/common/filesystem.h"
#include "instr/common/json.h"
#include "instr/ota/domain.h"
#include "instr/platform/simulated.h"

using namespace instr;
using namespace instr::ota;
using namespace instr::ota::domain;

struct Fixture {
  SimulatedFileSystem fs;
  platform::SimulatedPlatform plat;
  ManualTimeSource ts;
  OtaConfig cfg;
  std::unique_ptr<OtaEngine> eng;
  std::vector<OtaProgressEvent> events;
  // What the install engine was asked to do. SWUpdate itself is never run
  // here: the seam lets us assert the manager's half of the contract —
  // which artifact, into which slot — without touching the host.
  struct InstallCall {
    std::string artifact;
    std::string slot;
    std::string version;
  };
  std::vector<InstallCall> installs;
  bool install_should_fail = false;

  Fixture() {
    cfg.confirm_timeout_s = 600;
    eng = std::make_unique<OtaEngine>(fs, plat, ts, cfg);
    eng->set_install_fn([&](const std::string& artifact, const std::string& slot,
                            const ArtifactManifest& m) -> Result<int> {
      installs.push_back({artifact, slot, m.version});
      if (install_should_fail)
        return Result<int>::fail(Errc::Io, "simulated swupdate failure", "install");
      return Result<int>::good(0);
    });
    eng->set_event_sink([&](const OtaProgressEvent& e) { events.push_back(e); });
    eng->set_enrollment_secret_provider([] { return std::string("enrollment-secret"); });
    plat.set_factory_identity([]() {
      platform::FactoryIdentity id;
      id.serial = "SIM-1";
      id.valid = true;
      return id;
    }());
  }
  ArtifactManifest make_manifest(const std::string& body, const std::string& version) {
    ArtifactManifest m;
    m.version = version;
    m.sha256 = crypto::sha256(body).hex();
    m.hmac = crypto::hmac_sha256("enrollment-secret", version + m.sha256).hex();
    m.size_bytes = body.size();
    return m;
  }
};

TEST(OtaVersions, CompareDotted) {
  EXPECT_EQ(*compare_versions("1.2.10", "1.2.9"), 1);
  EXPECT_EQ(*compare_versions("1.2.9", "1.2.10"), -1);
  EXPECT_EQ(*compare_versions("v2.0.0", "2.0.0"), 0);
  EXPECT_EQ(*compare_versions("1.0.0", "1.0"), 0);  // padded
}

TEST(OtaPipeline, ReceiveVerifyStageApplyConfirm) {
  Fixture f;
  std::string body = "SWU-PAYLOAD-IMAGE";
  auto m = f.make_manifest(body, "2.0.0");
  // Land the artifact in chunks (interface shaped for streaming later).
  auto r1 = f.eng->receive_chunk(body.substr(0, 5), 0);
  ASSERT_TRUE(r1.ok());
  auto r2 = f.eng->receive_chunk(body.substr(5), 5);
  ASSERT_TRUE(r2.ok());
  EXPECT_EQ(*r2, body.size());
  EXPECT_TRUE(f.eng->stage(m, "enrollment-secret", "1.0.0").ok());
  EXPECT_EQ(f.eng->state().state, "staged");
  // The artifact went to the install engine, aimed at the inactive slot.
  ASSERT_EQ(f.installs.size(), 1u);
  EXPECT_EQ(f.installs[0].version, "2.0.0");
  EXPECT_EQ(f.installs[0].slot, f.eng->state().staged_slot);
  EXPECT_FALSE(f.installs[0].artifact.empty());
  EXPECT_EQ(f.eng->state().staged_slot, "B");
  EXPECT_TRUE(f.eng->apply().ok());
  EXPECT_EQ(f.eng->state().state, "applied_pending_confirm");
  EXPECT_TRUE(f.eng->confirm().ok());
  EXPECT_EQ(f.eng->state().state, "confirmed");
  ASSERT_EQ(f.eng->history().size(), 1u);
  EXPECT_EQ(f.eng->history()[0].result, "confirmed");
}

TEST(OtaPipeline, CorruptArtifactFailsIntegrity) {
  Fixture f;
  std::string body = "SWU-PAYLOAD";
  auto m = f.make_manifest(body, "2.0.0");
  f.eng->receive_chunk(body + "tampered", 0);
  Error e = f.eng->verify(m, "enrollment-secret", "1.0.0");
  EXPECT_EQ(e.code, Errc::Corrupt);
  EXPECT_NE(e.message.find("integrity"), std::string::npos);
}

TEST(OtaPipeline, BadSignatureRefusedBeforeStaging) {
  Fixture f;
  std::string body = "SWU-PAYLOAD";
  auto m = f.make_manifest(body, "2.0.0");
  f.eng->receive_chunk(body, 0);
  m.hmac = "deadbeef";  // forged signature
  Error e = f.eng->verify(m, "enrollment-secret", "1.0.0");
  EXPECT_EQ(e.code, Errc::SignatureInvalid);
  EXPECT_EQ(f.eng->state().state, "download");  // never staged
}

TEST(OtaPipeline, AntiRollbackRefusesDowngrade) {
  Fixture f;
  f.eng->set_windows({});  // always in window
  std::string body = "OLD";
  auto m = f.make_manifest(body, "0.9.0");
  f.eng->receive_chunk(body, 0);
  Error e = f.eng->stage(m, "enrollment-secret", "1.0.0");
  EXPECT_EQ(e.code, Errc::VersionMismatch);
  EXPECT_NE(e.message.find("downgrade"), std::string::npos);
}

TEST(OtaPipeline, UnconfirmedUpdateAutoRollsBack) {
  Fixture f;
  std::string body = "NEW";
  auto m = f.make_manifest(body, "2.0.0");
  f.eng->receive_chunk(body, 0);
  f.eng->stage(m, "enrollment-secret", "1.0.0");
  f.eng->apply();
  // Advance past the confirmation deadline → tick fires auto-rollback.
  f.ts.mono += 601ull * 1'000'000'000ull;
  auto ev = f.eng->tick(f.ts.mono_ns());
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(f.eng->state().state, "rolled_back");
  // History records the rollback for the canary/auto-halt path.
  ASSERT_EQ(f.eng->history().size(), 1u);
  EXPECT_EQ(f.eng->history()[0].result, "rolled_back");
}

TEST(OtaPipeline, ResumeRejectsMismatchedOffset) {
  Fixture f;
  auto r = f.eng->receive_chunk("abc", 42);
  EXPECT_FALSE(r.ok());
  EXPECT_NE(r.error.message.find("resume mismatch"), std::string::npos);
}

TEST(OtaMaintenance, WindowGatesStaging) {
  Fixture f;
  // Window: 02:00–04:00 daily. Wall clock is fixed far from the window.
  f.eng->set_windows({{"*", "02:00", "04:00"}});
  std::string body = "NEW";
  auto m = f.make_manifest(body, "2.0.0");
  f.eng->receive_chunk(body, 0);
  Error e = f.eng->stage(m, "enrollment-secret", "1.0.0");
  EXPECT_EQ(e.code, Errc::Busy);
  EXPECT_NE(e.message.find("maintenance"), std::string::npos);
  // Outside schedule check reflects it.
  EXPECT_FALSE(f.eng->in_maintenance_window(1'700'013'180));  // 2023-11-15 01:53 UTC
  EXPECT_TRUE(f.eng->in_maintenance_window(1'700'016'600));   // 02:50 UTC
}

TEST(OtaBandwidth, LimiterGrantsBytesPerSecond) {
  Fixture f;
  f.eng->set_bandwidth_limit(1000);
  auto g1 = f.eng->bandwidth_allotment(0.0);
  EXPECT_EQ(g1, 1000u);  // burst = 1s worth
  auto g2 = f.eng->bandwidth_allotment(0.0);
  EXPECT_EQ(g2, 0u);  // no time passed, bucket drained
  auto g3 = f.eng->bandwidth_allotment(1.0);
  EXPECT_EQ(g3, 1000u);
}

TEST(OtaCanary, ReportsStateForStagedRollouts) {
  Fixture f;
  auto j = json_parse(f.eng->canary_json(87.5, 2.0));
  ASSERT_TRUE(j.ok());
  EXPECT_DOUBLE_EQ((*j).find("health_score")->as_number(), 87.5);
  EXPECT_EQ((*j).find("fault_rate_24h")->as_number(), 2.0);
}

// §7.9: SWUpdate is the install engine. A failing install must not leave a
// staged update behind for apply() to promote — it fails loudly and is
// recorded in history so HQ can see it.
TEST(OtaPipeline, InstallFailureIsRecordedAndNothingIsStaged) {
  Fixture f;
  std::string body = "SWU-PAYLOAD-IMAGE";
  auto m = f.make_manifest(body, "2.0.0");
  ASSERT_TRUE(f.eng->receive_chunk(body, 0).ok());
  f.install_should_fail = true;
  Error e = f.eng->stage(m, "enrollment-secret", "1.0.0");
  EXPECT_FALSE(e.ok());
  EXPECT_EQ(f.eng->state().state, "failed");
  EXPECT_NE(f.eng->state().error.find("swupdate"), std::string::npos);
  // apply() must refuse: there is nothing staged.
  EXPECT_FALSE(f.eng->apply().ok());
  ASSERT_EQ(f.eng->history().size(), 1u);
  EXPECT_EQ(f.eng->history()[0].result, "failed");
}

// The install engine is pointed at a configured binary, so an image can ship
// SWUpdate somewhere other than /usr/bin without a code change (§7.9).
TEST(OtaPipeline, SwupdateBinaryIsConfigurable) {
  Fixture f;
  EXPECT_EQ(f.eng->swupdate_binary(), "/usr/bin/swupdate");
  f.eng->set_swupdate_binary("/opt/swupdate/bin/swupdate");
  EXPECT_EQ(f.eng->swupdate_binary(), "/opt/swupdate/bin/swupdate");
}
