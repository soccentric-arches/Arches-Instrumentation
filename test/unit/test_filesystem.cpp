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
 * @file test/unit/test_filesystem.cpp
 * @brief simulated filesystem fault injection surface and ctrl
 *
 * server/client loopback (REQ §12).
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#include <gtest/gtest.h>

#include "instr/common/filesystem.h"
#include "instr/ctrl/ctrl.h"

using namespace instr;

TEST(SimulatedFs, BasicFileOperations) {
  SimulatedFileSystem fs;
  EXPECT_FALSE(fs.exists("/a/b.txt"));
  EXPECT_TRUE(fs.write("/a/b.txt", "hello").ok());
  EXPECT_TRUE(fs.exists("/a/b.txt"));
  auto r = fs.read("/a/b.txt");
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(*r, "hello");
  EXPECT_TRUE(fs.rename("/a/b.txt", "/a/c.txt").ok());
  EXPECT_FALSE(fs.exists("/a/b.txt"));
  EXPECT_TRUE(fs.remove("/a/c.txt", false).ok());
}

TEST(SimulatedFs, WriteFailureInjection) {
  SimulatedFileSystem fs;
  fs.fail_writes_after(2);  // third write onward fails
  EXPECT_TRUE(fs.write("/f1", "a").ok());
  EXPECT_TRUE(fs.write("/f2", "b").ok());
  auto e = fs.write("/f3", "c");
  EXPECT_FALSE(e.ok());
  EXPECT_EQ(e.code, Errc::Io);
  EXPECT_NE(e.message.find("simulated"), std::string::npos);
}

TEST(SimulatedFs, FullDiskInjection) {
  SimulatedFileSystem fs;
  fs.set_free_space(10);
  // free_space is the budget input used by spool/budget logic.
  auto free = fs.free_space("/pst");
  ASSERT_TRUE(free.ok());
  EXPECT_EQ(*free, 10u);
}

TEST(SimulatedFs, CorruptReadInjection) {
  SimulatedFileSystem fs;
  fs.write("/f", "important-data");
  fs.corrupt_reads_of("/f", 1);
  auto r = fs.read("/f");
  ASSERT_TRUE(r.ok());
  EXPECT_NE(*r, "important-data");  // first byte flipped
}

TEST(SimulatedFs, RecursiveRemoveAndList) {
  SimulatedFileSystem fs;
  fs.write("/pst/system/istate/config.yaml", "a: 1");
  fs.write("/pst/system/istate/origins.yaml", "a: default");
  fs.write("/pst/state/spool/x.seg", "data");
  auto files = fs.list_dir("/pst/system/istate");
  ASSERT_TRUE(files.ok());
  EXPECT_EQ(files->size(), 2u);
  EXPECT_TRUE(fs.remove("/pst/system", true).ok());
  EXPECT_FALSE(fs.exists("/pst/system/istate/config.yaml"));
  EXPECT_TRUE(fs.exists("/pst/state/spool/x.seg"));  // state untouched
}

// ---- ctrl server/client loopback over a real UDS socket ----

TEST(CtrlLoopback, RequestResponseOverUds) {
  CommandTable t;
  t.add({"ping", "liveness", {}, [](const std::string&) {
           return CommandResult::good(std::string("{\"pong\":true}"));
         }});
  t.add({"echo", "echo args", {{"msg", false, "text"}}, [](const std::string& args) {
           auto a = args_object(args);
           if (!a.ok()) return CommandResult::from_error(a.error);
           return CommandResult::good((*a).find("msg") ? (*a).find("msg")->as_string() : "");
         }});
  std::string path = "/tmp/instr-test-ctrl-" + std::to_string(::getpid()) + ".sock";
  // Mode 0600 root-only per §3.2 (enforced by the server on bind).
  ctrl::CtrlServer server(&t, path);
  ASSERT_TRUE(server.start().ok());

  ctrl::CtrlClient client(path);
  ASSERT_TRUE(client.connect().ok());
  auto r1 = client.call("ping", "{}");
  EXPECT_TRUE(r1.ok);
  auto r2 = client.call("echo", "{\"msg\":\"hello world\"}");
  ASSERT_TRUE(r2.ok);
  EXPECT_EQ(r2.json_data, "\"hello world\"");
  auto r3 = client.call("missing", "{}");
  EXPECT_FALSE(r3.ok);
  EXPECT_EQ(r3.error_code, "not-found");
  client.close();
  server.stop();
}

TEST(CtrlLoopback, StreamHandler) {
  CommandTable t;
  std::string path2 = "/tmp/instr-test-ctrl-stream-" + std::to_string(::getpid()) + ".sock";
  ctrl::CtrlServer server(&t, path2);
  int count = 0;
  server.set_stream_handler("tail", [](const std::string& args, ctrl::StreamSink& sink) {
    (void)args;
    for (int i = 0; i < 5; ++i)
      if (!sink.send("{\"line\":" + std::to_string(i) + "}")) return;
  });
  ASSERT_TRUE(server.start().ok());
  ctrl::CtrlClient client(path2);
  ASSERT_TRUE(client.connect().ok());
  std::vector<std::string> got;
  (void)client.call_stream("tail", "{}", [&](const std::string& f) { got.push_back(f); });
  EXPECT_EQ(got.size(), 5u);
  client.close();
  server.stop();
}