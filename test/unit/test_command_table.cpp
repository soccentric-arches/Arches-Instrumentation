// Unit tests — command table envelope, streaming sink semantics (§3.2).
#include <gtest/gtest.h>

#include "instr/common/command_table.h"

using namespace instr;

TEST(CommandEnvelope, OkAndErrorShapes) {
  CommandResult ok = CommandResult::good(std::string("{\"x\":1}"));
  auto j = json_parse(ok.envelope());
  ASSERT_TRUE(j.ok());
  EXPECT_TRUE((*j).find("ok")->as_bool());
  EXPECT_EQ((*j).find("data")->find("x")->as_number(), 1);

  CommandResult err = CommandResult::err(Errc::BudgetExceeded, "too much");
  auto j2 = json_parse(err.envelope());
  ASSERT_TRUE(j2.ok());
  EXPECT_FALSE((*j2).find("ok")->as_bool());
  EXPECT_EQ((*j2).find("error")->find("code")->as_string(), "budget-exceeded");
  EXPECT_EQ((*j2).find("error")->find("message")->as_string(), "too much");
}

TEST(CommandTable, ArgSpecValidationIsDeclared) {
  CommandTable t;
  t.add({"op", "needs args", {{"a", true, "required"}, {"b", false, "optional"}},
         [](const std::string& args) {
           auto a = args_object(args);
           if (!a.ok()) return CommandResult::from_error(a.error);
           if (!(*a).has("a")) return CommandResult::err(Errc::InvalidArgument, "a is required");
           return CommandResult::good(std::string("{}"));
         }});
  EXPECT_FALSE(t.dispatch("op", "{}").ok);
  EXPECT_TRUE(t.dispatch("op", "{\"a\":1}").ok);
  EXPECT_FALSE(t.dispatch("op", "not-json").ok);
}

TEST(CommandTable, HelpForUnknownOpIsDiagnosable) {
  CommandTable t;
  auto h = t.help_for("ghost");
  EXPECT_NE(h.find("unknown op"), std::string::npos);
}
