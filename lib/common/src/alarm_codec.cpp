#include "instr/common/alarm_codec.h"

#include <cstdio>
#include <unordered_map>

namespace instr {

const char* alarm_domain_name(AlarmDomain d) {
  switch (d) {
    case AlarmDomain::Hardware: return "hardware";
    case AlarmDomain::Software: return "software";
    case AlarmDomain::Network: return "network";
    case AlarmDomain::Application: return "application";
    case AlarmDomain::Fault: return "fault";
  }
  return "unknown";
}

const char* x733_type_name(X733Type t) {
  switch (t) {
    case X733Type::Equipment: return "equipment";
    case X733Type::Communications: return "communications";
    case X733Type::ProcessingError: return "processing-error";
    case X733Type::QualityOfService: return "quality-of-service";
    case X733Type::Environmental: return "environmental";
  }
  return "unknown";
}

AlarmDomain alarm_domain_from_name(const std::string& n) {
  if (n == "hardware") return AlarmDomain::Hardware;
  if (n == "software") return AlarmDomain::Software;
  if (n == "network") return AlarmDomain::Network;
  if (n == "application") return AlarmDomain::Application;
  if (n == "fault") return AlarmDomain::Fault;
  return AlarmDomain::Software;  // caller validates via catalog schema
}

X733Type x733_type_from_name(const std::string& n) {
  if (n == "equipment") return X733Type::Equipment;
  if (n == "communications") return X733Type::Communications;
  if (n == "processing-error") return X733Type::ProcessingError;
  if (n == "quality-of-service") return X733Type::QualityOfService;
  if (n == "environmental") return X733Type::Environmental;
  return X733Type::ProcessingError;
}

const std::map<uint8_t, std::string>& module_names() {
  static const std::map<uint8_t, std::string> m = {
      {0x01, "istate"},      {0x02, "networkd"},  {0x03, "monitord"},
      {0x04, "hardware"},    {0x05, "faults"},    {0x06, "alarms"},
      {0x07, "logmanager"},  {0x08, "telemetry"}, {0x09, "ota"},
      {0x0A, "backup"},      {0x10, "application-1"}, {0x11, "application-2"},
  };
  return m;
}

uint8_t module_from_name(const std::string& name) {
  for (const auto& kv : module_names())
    if (kv.second == name) return kv.first;
  return 0;
}

Result<AlarmId> decode_alarm_id(uint32_t id) {
  AlarmId a;
  if (id == 0)
    return Result<AlarmId>::fail(Errc::InvalidArgument, "alarm id 0 is reserved", "decode_alarm_id");
  a.domain = static_cast<AlarmDomain>((id >> 28) & 0xF);
  a.module = static_cast<uint8_t>((id >> 20) & 0xFF);
  a.type = static_cast<X733Type>((id >> 16) & 0xF);
  a.index = static_cast<uint16_t>(id & 0xFFFF);
  return Result<AlarmId>::good(a);
}

std::string alarm_id_string(uint32_t id) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%x_%02x_%x_%04x", (id >> 28) & 0xF, (id >> 20) & 0xFF,
                (id >> 16) & 0xF, id & 0xFFFF);
  return buf;
}

}  // namespace instr