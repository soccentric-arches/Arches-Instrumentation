// Unit tests — faults domain (REQ §12).
#include <gtest/gtest.h>

#include "instr/common/timestamps.h"
#include "instr/faults/domain.h"

using namespace instr;
using namespace instr::faults;
using namespace instr::faults::domain;

static const char* kRules = R"yaml(
rules:
  - name: bus_error
    pattern: "AER.*Bus Error"
    fault_id: 0x5_05_1_0001
    severity: MAJOR
    x733_type: equipment
    probable_cause: bus-error
    dedup_key: "bus:$device"
    dedup_window_s: 60
    rate_limit_per_s: 100
  - name: oom_kill
    pattern: "Out of memory: Killed process"
    fault_id: 0x5_05_3_0008
    severity: MAJOR
    x733_type: processing-error
    probable_cause: memory-exhaustion
    dedup_key: "oom:$process"
    dedup_window_s: 120
    rate_limit_per_s: 100
)yaml";

TEST(FaultRules, LoadValidatesIds) {
  auto rules = load_rules(kRules);
  ASSERT_TRUE(rules.ok());
  ASSERT_EQ(rules->size(), 2u);
  EXPECT_EQ((*rules)[0].fault_id, 0x50510001u);
  EXPECT_EQ((*rules)[0].severity, common::SEV_MAJOR);
  EXPECT_EQ((*rules)[1].name, "oom_kill");
}

TEST(FaultRules, RejectsNonFaultDomainIds) {
  const char* bad = R"yaml(
rules:
  - name: bad
    pattern: "x"
    fault_id: 0x1_04_1_0007
    severity: MAJOR
)yaml";
  auto r = load_rules(bad);
  EXPECT_FALSE(r.ok());  // fault ids must be in the fault domain (§8)
}

TEST(FaultRules, RejectsBadRegex) {
  const char* bad = R"yaml(
rules:
  - name: bad
    pattern: "([unclosed"
    fault_id: 0x5_05_1_0001
)yaml";
  auto r = load_rules(bad);
  EXPECT_FALSE(r.ok());
}

TEST(FaultEngine, MatchesAndClassifies) {
  auto rules = load_rules(kRules);
  ASSERT_TRUE(rules.ok());
  RuleEngine engine(*rules);
  auto m = engine.match("pcieport 0000:01:00.0: AER: Bus Error detected");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ((*m)->name, "bus_error");
  EXPECT_FALSE(engine.match("normal kernel noise").has_value());
}

TEST(FaultEngine, DedupKeyExtraction) {
  auto key = RuleEngine::render_dedup_key("bus:$device", "mmcblk0: I/O error on dev mmcblk0");
  EXPECT_NE(key.find("mmcblk0"), std::string::npos);
  EXPECT_NE(key.find("bus:"), std::string::npos);
  auto key2 = RuleEngine::render_dedup_key("oom:$process",
                                            "Out of memory: Killed process 4321 (myapp)");
  EXPECT_NE(key2.find("myapp"), std::string::npos);
}

TEST(FaultProcessor, DedupsWithinWindow) {
  auto rules = load_rules(kRules);
  ASSERT_TRUE(rules.ok());
  FaultProcessor proc(*rules);
  uint64_t t = 1'000'000'000ull;
  auto r1 = proc.feed({"AER: Bus Error on 0000:01:00.0"}, t, false);
  ASSERT_EQ(r1.size(), 1u);
  EXPECT_EQ(r1[0].occurrences, 1u);
  auto r2 = proc.feed({"AER: Bus Error on 0000:01:00.0"}, t + 1'000'000'000ull, false);
  ASSERT_EQ(r2.size(), 1u);
  EXPECT_EQ(r2[0].occurrences, 2u);
  EXPECT_EQ(proc.stats().duplicates_merged, 1u);
}

TEST(FaultProcessor, BackfillFlagged) {
  auto rules = load_rules(kRules);
  ASSERT_TRUE(rules.ok());
  FaultProcessor proc(*rules);
  auto recs = proc.feed({"AER: Bus Error on 0000:01:00.0"}, 0, true);
  ASSERT_EQ(recs.size(), 1u);
  EXPECT_TRUE(recs[0].backfill);
  EXPECT_EQ(proc.stats().backfill_lines, 1u);
}

TEST(FaultProcessor, RateLimitSuppressesFloodWithSummary) {
  auto rules = load_rules(kRules);
  ASSERT_TRUE(rules.ok());
  std::vector<FaultRule> limited = *rules;
  limited[0].rate_limit_per_s = 2;
  limited[0].dedup_key = "flood";  // same key each time
  FaultProcessor proc(limited);
  std::vector<std::string> flood;
  for (int i = 0; i < 1000; ++i) flood.push_back("AER: Bus Error on 0000:01:00.0");
  uint64_t t = 1'000'000'000ull;
  auto recs = proc.feed(flood, t, false);
  // 2 pass, rest suppressed; records well below 1000.
  uint64_t real = 0;
  for (const auto& r : recs)
    if (r.occurrences > 0) ++real;
  EXPECT_LE(real, 3u);
  EXPECT_LE(recs.size(), 5u);
  EXPECT_NE(recs.back().raw_line.find("suppressed "), std::string::npos);
  EXPECT_GT(proc.stats().faults_raised, 0u);
}

TEST(FaultProcessor, NonMatchingLinesNeverEmit) {
  auto rules = load_rules(kRules);
  ASSERT_TRUE(rules.ok());
  FaultProcessor proc(*rules);
  auto recs = proc.feed({"boring line", "another boring line", "systemd[1]: fine"}, 0, false);
  EXPECT_TRUE(recs.empty());
  EXPECT_EQ(proc.stats().lines_scanned, 3u);
}