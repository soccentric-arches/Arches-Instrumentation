// Contract tests (REQ §12): a breaking .proto schema change fails CI rather
// than a field device. The golden descriptor hash pins the wire contract;
// the registration/ctrl contracts pin the runtime behaviour on top of it.
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include "instr/common/alarm_codec.h"
#include "instr/common/crypto.h"
#include "instr/common/frames.h"
#include "instr/common/spool.h"
#include "instr/istate/orchestrator.h"
#include "instr/transport/transport.h"

using namespace instr;

static std::string slurp(const std::string& path) {
  std::ifstream f(path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

TEST(ContractProtos, DescriptorHashMatchesGolden) {
  std::string desc = slurp(PROTO_DESC_FILE);
  ASSERT_GT(desc.size(), 100u) << "descriptor set missing; build protos_golden first";
  std::string computed = crypto::sha256(desc).hex();
  std::string golden = slurp(GOLDEN_FILE);
  while (!golden.empty() && (golden.back() == '\n' || golden.back() == ' ' || golden.back() == '\r'))
    golden.pop_back();
  if (golden.empty()) {
    // First run: seed the golden file (CI must commit it).
    std::ofstream out(GOLDEN_FILE);
    out << computed << "\n";
    GTEST_SKIP() << "golden seeded; commit " << GOLDEN_FILE;
  }
  EXPECT_EQ(computed, golden)
      << "Proto definitions changed — this is a breaking wire contract change.\n"
      << "Update the golden deliberately: test/contract/protos.golden.sha256\n"
      << "(regenerate with: protoc --descriptor_set_out, then re-hash) and\n"
      << "bump transport::kProtoSchemaVersion if wire-incompatible.";
}

TEST(ContractHandshake, SchemaVersionIsSuiteWide) {
  // One wire format for services: every spoke and istate share one schema
  // version; the constant is the single point of truth (§3.1, R3).
  EXPECT_EQ(transport::kProtoSchemaVersion, 1u);
}

TEST(ContractHandshake, RegistrationFieldsAreStable) {
  // The handshake contract: daemon name, build version, proto schema
  // version, capability list — encoded in common.proto RegistrationRequest.
  common::RegistrationRequest req;
  req.set_daemon("monitord");
  req.set_build_version("1.0.0");
  req.set_proto_schema_version(transport::kProtoSchemaVersion);
  req.add_capabilities("samples");
  std::string wire;
  ASSERT_TRUE(req.AppendToString(&wire));
  common::RegistrationRequest back;
  ASSERT_TRUE(back.ParseFromString(wire));
  EXPECT_EQ(back.daemon(), "monitord");
  EXPECT_EQ(back.proto_schema_version(), transport::kProtoSchemaVersion);
  EXPECT_EQ(back.capabilities(0), "samples");
}

TEST(ContractCtrl, FrameFormatIsLengthPrefixedLe32) {
  // The raw-UDS ctrl contract: 4-byte LE length + payload.
  auto frame = frames::encode_frame("payload");
  ASSERT_EQ(frame.size(), 4 + 7);
  EXPECT_EQ(uint8_t(frame[0]), 7);  // little-endian payload length
  EXPECT_EQ(uint8_t(frame[1]), 0);
  frames::FrameDecoder dec;
  std::string out;
  ASSERT_TRUE(dec.feed(frame.data(), frame.size(), &out));
  EXPECT_EQ(out, "payload");
}

TEST(ContractAlarmIds, SameSchemeForAlarmsAndFaults) {
  // §8: alarms and faults share the 32-bit identity scheme so HQ can
  // correlate a kernel fault with an application-raised alarm.
  uint32_t alarm = encode_alarm_id(AlarmDomain::Hardware, 0x04, X733Type::Equipment, 7);
  uint32_t fault = encode_alarm_id(AlarmDomain::Fault, 0x05, X733Type::ProcessingError, 8);
  auto da = decode_alarm_id(alarm);
  auto flt = decode_alarm_id(fault);
  ASSERT_TRUE(da.ok());
  ASSERT_TRUE(flt.ok());
  EXPECT_EQ(da->domain, AlarmDomain::Hardware);
  EXPECT_EQ(flt->domain, AlarmDomain::Fault);
  EXPECT_EQ(alarm_id_string(fault), "0x5_05_3_0008");
}

TEST(ContractSpool, DropPriorityOrderIsContractual) {
  // §11: oldest telemetry first, then log refs, then events; alarms and
  // faults protected. The enum ordering encodes the contract.
  EXPECT_EQ(int(SpoolPriority::Telemetry), 0);
  EXPECT_EQ(int(SpoolPriority::LogRef), 1);
  EXPECT_EQ(int(SpoolPriority::Event), 2);
  EXPECT_EQ(int(SpoolPriority::Fault), 3);
  EXPECT_EQ(int(SpoolPriority::Alarm), 4);
}
