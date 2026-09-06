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
 * @file daemons/logmanager/test/test_domain.cpp
 * @brief logmanager domain (REQ §12)
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#include <gtest/gtest.h>

#include "instr/common/filesystem.h"
#include "instr/logmanager/daemon.h"

#include "instr/common/crypto.h"
#include "instr/common/tar.h"

using namespace instr;
using namespace instr::logmanager;
using namespace instr::logmanager::domain;

struct Fixture {
  SimulatedFileSystem fs;
  ManualTimeSource ts;
  std::string base = "/pst/state/logs";
  LogCore core{fs, base, ts};  // NOLINT
  Fixture() {
    fs.mkdirs(base);
    std::vector<SourceSpec> sources;
    SourceSpec s;
    s.name = "app";
    s.kind = "file";
    s.path = "/var/log/app.log";
    s.level = "INFO";
    s.rotate_size_bytes = 100;  // tiny for tests
    s.retention_files = 2;
    sources.push_back(s);
    core.store().configure(sources);
  }
};

static LogLine line(uint64_t mono, const std::string& lvl, const std::string& msg) {
  LogLine l;
  l.mono_ns = mono;
  l.source = "app";
  l.level = lvl;
  l.message = msg;
  return l;
}

TEST(LogStore, AppendsAndRotatesBySize) {
  Fixture f;
  for (int i = 0; i < 20; ++i)
    EXPECT_TRUE(f.core.store().append("app", line(f.ts.mono_ns(), "INFO", std::to_string(i))).ok());
  auto files = f.core.store().files_for("app");
  // Active + at least one rotated file after exceeding 100 bytes.
  EXPECT_GE(files.size(), 2u);
}

TEST(LogStore, RetentionKeepsOnlyConfiguredRotations) {
  Fixture f;
  for (int round = 0; round < 6; ++round) {
    for (int i = 0; i < 20; ++i)
      f.core.store().append("app", line(f.ts.mono_ns(), "INFO", "r" + std::to_string(round)));
  }
  auto st = f.core.store().enforce_retention(f.ts.mono_ns());
  auto files = f.core.store().files_for("app");
  // active + 2 rotations max
  EXPECT_LE(files.size(), 3u);
}

TEST(LogStore, RuntimeLevelControlFiltersCollection) {
  Fixture f;
  f.core.store().set_runtime_level("app", "ERROR");
  EXPECT_TRUE(f.core.store().append("app", line(1, "INFO", "hidden")).ok());  // filtered, no error
  auto tail = f.core.store().read_tail("app", 100);
  ASSERT_TRUE(tail.ok());
  EXPECT_TRUE(tail->empty());  // INFO below ERROR level was not collected
  EXPECT_TRUE(f.core.store().append("app", line(2, "ERROR", "visible")).ok());
  tail = f.core.store().read_tail("app", 100);
  ASSERT_EQ(tail->size(), 1u);
  EXPECT_EQ((*tail)[0].message, "visible");
}

TEST(LogTail, FiltersBySourceLevelSince) {
  TailSession s([]() {
    TailFilter f;
    f.sources = {"app"};
    f.min_level = "WARNING";
    f.since_mono_ns = 500;
    return f;
  }(), 0);
  EXPECT_FALSE(s.accept(line(100, "ERROR", "too old"), 1));   // since filter
  EXPECT_FALSE(s.accept(line(600, "INFO", "too quiet"), 1));  // level filter
  EXPECT_TRUE(s.accept(line(600, "WARNING", "ok"), 1));
  EXPECT_EQ(s.emitted(), 1u);
}

TEST(LogTail, RateLimitsAndAutoStops) {
  TailFilter f;
  f.max_rate_per_s = 3;
  f.timeout_s = 10;
  TailSession s(f, 0);
  uint64_t now = 0;
  for (int i = 0; i < 10; ++i) s.accept(line(1, "INFO", "x"), now);
  EXPECT_EQ(s.emitted(), 3u);
  EXPECT_EQ(s.dropped(), 7u);
  // Auto-stop after timeout.
  EXPECT_TRUE(s.expired(11ull * 1'000'000'000ull));
  EXPECT_FALSE(s.accept(line(1, "INFO", "after"), 11ull * 1'000'000'000ull));
}

TEST(LogBundles, CreateMetaFetchIntegrity) {
  Fixture f;
  for (int i = 0; i < 5; ++i) f.core.store().append("app", line(i, "INFO", "hello " + std::to_string(i)));
  auto m = f.core.bundles().create(f.core.store(), {"app"}, f.ts.mono_ns());
  ASSERT_TRUE(m.ok());
  EXPECT_NE(m->handle.size(), 0);
  EXPECT_GT(m->size_bytes, 0u);
  auto meta = f.core.bundles().meta(m->handle);
  ASSERT_TRUE(meta.ok());
  EXPECT_EQ(meta->sha256, m->sha256);
  auto payload = f.core.bundles().payload(m->handle);
  ASSERT_TRUE(payload.ok());
  // Payload parses as ustar and contains the app log.
  auto entries = tar::read_ustar(*payload);
  ASSERT_TRUE(entries.ok());
  ASSERT_FALSE(entries->empty());
  EXPECT_NE((*entries)[0].data.find("hello 4"), std::string::npos);
  EXPECT_EQ(meta->sha256, crypto::sha256(*payload).hex());
}

TEST(LogBundles, UnknownHandleIsLoud) {
  Fixture f;
  auto m = f.core.bundles().meta("nope");
  EXPECT_FALSE(m.ok());
  EXPECT_EQ(m.error.code, Errc::NotFound);
}

TEST(LogForensics, CapturesBoundedCoredumpAndTail) {
  Fixture f;
  f.fs.write("/var/lib/coredumps/istate.core", std::string(1000, 'x'));
  std::vector<std::string> lines;
  for (int i = 0; i < 300; ++i) lines.push_back("log line " + std::to_string(i));
  auto c = f.core.forensics().capture("istate", "/var/lib/coredumps/istate.core", lines, 100);
  ASSERT_TRUE(c.ok());
  EXPECT_EQ(c->coredump_bytes, 100u);  // bounded
  EXPECT_GT(c->tail_lines, 0u);
  bool saw_tail = false;
  for (const auto& fl : f.core.forensics().captured_files())
    if (fl.find("istate.tail.log") != std::string::npos) saw_tail = true;
  EXPECT_TRUE(saw_tail);
}