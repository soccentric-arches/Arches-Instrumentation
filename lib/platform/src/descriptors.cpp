#include "instr/platform/descriptors.h"

#include <yaml-cpp/yaml.h>

namespace instr {
namespace platform {

// Convert YAML subtree to JValue via the shared projection (declared in
// json.h as text-based; here we build from yaml-cpp nodes directly using the
// same scalar rules as json_from_yaml_text).
static JValue yaml_to_json(const YAML::Node& n);

static JValue yaml_scalar(const YAML::Node& n) {
  if (n.IsNull()) return JValue();
  if (n.IsScalar()) {
    const std::string& raw = n.Scalar();
    if (n.Tag() == "!!str") return JValue(raw);
    if (raw == "true") return JValue(true);
    if (raw == "false") return JValue(false);
    if (raw == "null" || raw == "~" || raw.empty()) return JValue();
    try {
      size_t pos;
      double d = std::stod(raw, &pos);
      if (pos == raw.size()) return JValue(d);
    } catch (...) {}
    return JValue(raw);
  }
  if (n.IsSequence()) {
    JValue arr = JValue::array();
    for (const auto& item : n) arr.push(yaml_to_json(item));
    return arr;
  }
  JValue obj = JValue::object();
  for (const auto& kv : n) obj.set(kv.first.as<std::string>(), yaml_to_json(kv.second));
  return obj;
}

static JValue yaml_to_json(const YAML::Node& n) { return yaml_scalar(n); }

static Error validate(const JValue& schema, const std::string& yaml_text,
                      const std::string& source, JValue* out) {
  auto j = json_from_yaml_text(yaml_text);
  if (!j.ok()) return j.error;
  std::vector<JsonValidator::Issue> issues;
  if (!JsonValidator::validate(schema, *j, &issues)) {
    std::string msg = "descriptor rejected (" + source + "):";
    for (const auto& is : issues) msg += " [" + is.path + "] " + is.message + ";";
    return Error::make(Errc::InvalidArgument, msg, source);
  }
  *out = *j;
  return Error::none();
}

Result<DeviceDescriptor> load_device_descriptor(const std::string& yaml_text, const JValue& schema,
                                                const std::string& source) {
  JValue j;
  Error e = validate(schema, yaml_text, source, &j);
  if (!e.ok()) return Result<DeviceDescriptor>::fail(e);
  DeviceDescriptor d;
  d.board = j.find("board") ? (*j.find("board")).as_string() : "";
  const JValue* devs = j.find("devices");
  if (devs) {
    for (const auto& dv : devs->items()) {
      DeviceSpec s;
      if (const JValue* v = dv.find("name")) s.name = v->as_string();
      if (const JValue* v = dv.find("kind")) s.kind = v->as_string();
      if (const JValue* v = dv.find("controls"))
        for (const auto& c : v->items()) s.controls.push_back(c.as_string());
      if (const JValue* v = dv.find("checks")) {
        for (const auto& c : v->items()) {
          DeviceCheck ch;
          if (const JValue* n = c.find("name")) ch.name = n->as_string();
          if (const JValue* o = c.find("op")) ch.op = o->as_string();
          if (const JValue* p = c.find("params"))
            for (const auto& pk : p->members())
              ch.params[pk.first] =
                  pk.second.is_string() ? pk.second.as_string() : pk.second.dump();
          s.checks.push_back(ch);
        }
      }
      if (const JValue* p = dv.find("params"))
        for (const auto& pk : p->members())
          s.params[pk.first] = pk.second.is_string() ? pk.second.as_string() : pk.second.dump();
      d.devices.push_back(s);
    }
  }
  return Result<DeviceDescriptor>::good(std::move(d));
}

Result<ResourceProfile> load_resource_profile(const std::string& yaml_text, const JValue& schema,
                                              const std::string& source) {
  JValue j;
  Error e = validate(schema, yaml_text, source, &j);
  if (!e.ok()) return Result<ResourceProfile>::fail(e);
  ResourceProfile p;
  if (const JValue* v = j.find("platform_class")) p.platform_class = v->as_string();
  if (const JValue* v = j.find("sample_interval_default_s")) p.sample_interval_default_s = v->as_number();
  if (const JValue* v = j.find("sample_intervals_per_class_s"))
    for (const auto& kv : v->members()) p.sample_intervals_per_class_s[kv.first] = kv.second.as_number();
  if (const JValue* v = j.find("highres_window_s")) p.highres_window_s = v->as_number();
  if (const JValue* v = j.find("highres_interval_s")) p.highres_interval_s = v->as_number();
  if (const JValue* v = j.find("telemetry_interval_s")) p.telemetry_interval_s = v->as_number();
  if (const JValue* v = j.find("telemetry_batch_max_records"))
    p.telemetry_batch_max_records = uint64_t(v->as_number());
  auto get_u64 = [&](const char* key, uint64_t dflt) {
    const JValue* v = j.find(key);
    return v ? uint64_t(v->as_number(double(dflt))) : dflt;
  };
  p.spool_max_bytes = get_u64("spool_max_bytes", p.spool_max_bytes);
  p.logs_max_bytes = get_u64("logs_max_bytes", p.logs_max_bytes);
  p.ota_staging_bytes = get_u64("ota_staging_bytes", p.ota_staging_bytes);
  p.backup_max_bytes = get_u64("backup_max_bytes", p.backup_max_bytes);
  p.timeline_max_events = get_u64("timeline_max_events", p.timeline_max_events);
  return Result<ResourceProfile>::good(std::move(p));
}

Result<CapabilityManifest> load_capability_manifest(const std::string& yaml_text,
                                                    const JValue& schema,
                                                    const std::string& source) {
  JValue j;
  Error e = validate(schema, yaml_text, source, &j);
  if (!e.ok()) return Result<CapabilityManifest>::fail(e);
  CapabilityManifest m;
  if (const JValue* v = j.find("board")) m.board = v->as_string();
  if (const JValue* v = j.find("capabilities"))
    for (const auto& c : v->items()) m.capabilities.push_back(c.as_string());
  return Result<CapabilityManifest>::good(std::move(m));
}

}  // namespace platform
}  // namespace instr