#include "instr/common/config.h"

#include <yaml-cpp/yaml.h>

#include <cmath>

namespace instr {

const char* origin_name(Origin o) {
  switch (o) {
    case Origin::Default: return "default";
    case Origin::App: return "app";
    case Origin::Local: return "local";
    case Origin::Hq: return "hq";
  }
  return "default";
}

Result<Origin> origin_from_name(const std::string& n) {
  if (n == "default") return Result<Origin>::good(Origin::Default);
  if (n == "app") return Result<Origin>::good(Origin::App);
  if (n == "local") return Result<Origin>::good(Origin::Local);
  if (n == "hq") return Result<Origin>::good(Origin::Hq);
  return Result<Origin>::fail(Errc::InvalidArgument, "unknown origin: " + n, "origin_from_name");
}

Result<JValue> load_schema_text(const std::string& text) {
  auto j = json_parse(text);
  if (!j.ok()) return j;
  if (!(*j).is_object())
    return Result<JValue>::fail(Errc::InvalidArgument, "schema root must be an object",
                                "load_schema_text");
  return j;
}

ConfigDocument ConfigDocument::empty() { return ConfigDocument(); }

Result<ConfigDocument> ConfigDocument::parse(const std::string& yaml_text, const JValue& schema,
                                             const std::string& source_name) {
  auto j = json_from_yaml_text(yaml_text);
  if (!j.ok())
    return Result<ConfigDocument>::fail(Error::make(j.error.code, j.error.message, source_name));
  std::vector<JsonValidator::Issue> issues;
  if (!JsonValidator::validate(schema, *j, &issues)) {
    std::string msg = "configuration rejected (" + source_name + "):";
    for (const auto& is : issues) msg += " [" + is.path + "] " + is.message + ";";
    return Result<ConfigDocument>::fail(Errc::InvalidArgument, msg, source_name);
  }
  ConfigDocument doc;
  // Flatten: walk the JSON projection.
  // Lambda flattening object trees into dot-paths.
  struct Flatten {
    static void walk(const JValue& v, const std::string& prefix,
                     std::map<std::string, std::string>* out) {
      if (v.is_object()) {
        for (const auto& kv : v.members()) {
          std::string p = prefix.empty() ? kv.first : prefix + "." + kv.first;
          walk(kv.second, p, out);
        }
      } else if (v.is_array()) {
        // Lists stay as YAML in the raw projection; store size marker plus
        // indexed items for structured access.
        out->operator[](prefix + ".__count") = std::to_string(v.items().size());
        for (size_t i = 0; i < v.items().size(); ++i) {
          std::string p = prefix + "." + std::to_string(i);
          walk(v.items()[i], p, out);
        }
      } else if (v.is_null()) {
        out->operator[](prefix) = "";
      } else if (v.is_bool()) {
        out->operator[](prefix) = v.as_bool() ? "true" : "false";
      } else if (v.is_number()) {
        double d = v.as_number();
        if (d == std::floor(d) && std::fabs(d) < 9.0e15)
          out->operator[](prefix) = std::to_string(static_cast<long long>(d));
        else
          out->operator[](prefix) = std::to_string(d);
      } else {
        out->operator[](prefix) = v.as_string();
      }
    }
  };
  Flatten::walk(*j, "", &doc.values_);
  for (const auto& kv : doc.values_) doc.origins_[kv.first] = Origin::Default;
  return Result<ConfigDocument>::good(std::move(doc));
}

bool ConfigDocument::has(const std::string& dot_path) const {
  return values_.count(dot_path) > 0;
}

std::string ConfigDocument::raw(const std::string& dot_path) const {
  auto it = values_.find(dot_path);
  return it == values_.end() ? std::string() : it->second;
}

std::string ConfigDocument::get_string(const std::string& dot_path, const std::string& dflt) const {
  std::string v = raw(dot_path);
  if (v.empty() && !has(dot_path)) return dflt;
  return v;
}

int64_t ConfigDocument::get_int(const std::string& dot_path, int64_t dflt) const {
  std::string v = raw(dot_path);
  if (v.empty() && !has(dot_path)) return dflt;
  try {
    return std::stoll(v);
  } catch (...) {
    return dflt;
  }
}

double ConfigDocument::get_double(const std::string& dot_path, double dflt) const {
  std::string v = raw(dot_path);
  if (v.empty() && !has(dot_path)) return dflt;
  try {
    return std::stod(v);
  } catch (...) {
    return dflt;
  }
}

bool ConfigDocument::get_bool(const std::string& dot_path, bool dflt) const {
  std::string v = raw(dot_path);
  if (v.empty() && !has(dot_path)) return dflt;
  return v == "true" || v == "True" || v == "1" || v == "yes";
}

std::vector<std::string> ConfigDocument::get_list(const std::string& dot_path) const {
  std::vector<std::string> out;
  std::string count_key = dot_path + ".__count";
  if (!has(count_key)) return out;
  int64_t n = get_int(count_key, 0);
  for (int64_t i = 0; i < n; ++i) out.push_back(raw(dot_path + "." + std::to_string(i)));
  return out;
}

void ConfigDocument::set(const std::string& dot_path, const std::string& scalar, Origin origin) {
  values_[dot_path] = scalar;
  origins_[dot_path] = origin;
  // Clear a stale list marker when a scalar replaces a list.
  values_.erase(dot_path + ".__count");
}

void ConfigDocument::set_number(const std::string& dot_path, double v, Origin origin) {
  if (v == std::floor(v) && std::fabs(v) < 9.0e15)
    set(dot_path, std::to_string(static_cast<long long>(v)), origin);
  else
    set(dot_path, std::to_string(v), origin);
}

void ConfigDocument::set_bool(const std::string& dot_path, bool v, Origin origin) {
  set(dot_path, v ? "true" : "false", origin);
}

Origin ConfigDocument::origin_of(const std::string& dot_path) const {
  auto it = origins_.find(dot_path);
  return it == origins_.end() ? Origin::Default : it->second;
}

std::vector<std::string> ConfigDocument::merge_from(const ConfigDocument& incoming,
                                                    Origin in_origin, bool force) {
  std::vector<std::string> rejected;
  for (const auto& kv : incoming.values_) {
    auto oit = origins_.find(kv.first);
    Origin existing = (oit == origins_.end()) ? Origin::Default : oit->second;
    // Origin rules: an HQ push may overwrite default/app/hq keys; local
    // operator edits are protected unless explicitly forced.
    bool allowed = true;
    if (in_origin == Origin::Hq && existing == Origin::Local && !force) allowed = false;
    if (in_origin == Origin::App && existing == Origin::Local && !force) allowed = false;
    if (!allowed) {
      rejected.push_back(kv.first);
      continue;
    }
    values_[kv.first] = kv.second;
    origins_[kv.first] = in_origin;
  }
  return rejected;
}

std::vector<std::string> ConfigDocument::merge_release(const ConfigDocument& defaults) {
  std::vector<std::string> added;
  for (const auto& kv : defaults.values_) {
    if (!values_.count(kv.first)) {
      values_[kv.first] = kv.second;
      origins_[kv.first] = Origin::Default;
      added.push_back(kv.first);
    }
  }
  return added;
}

void ConfigDocument::reset_to_defaults() {
  for (auto it = values_.begin(); it != values_.end();) {
    auto oit = origins_.find(it->first);
    if (oit != origins_.end() && oit->second != Origin::Default) {
      origins_.erase(oit);
      it = values_.erase(it);
    } else {
      ++it;
    }
  }
}

// ---- YAML serialisation ----

namespace {

// Escapes a scalar minimally: quote when it would otherwise parse as another
// type or contain YAML specials.
bool needs_quotes(const std::string& s) {
  // Quote only when the scalar would break YAML parsing: empty, leading or
  // trailing whitespace, block indicators, or inline specials. Typed scalars
  // (true/30) stay unquoted so they round-trip as the same value.
  if (s.empty()) return true;
  if (s.front() == ' ' || s.front() == '\t' || s.back() == ' ' || s.back() == '\t') return true;
  if (s.find(':') != std::string::npos || s.find(" #") != std::string::npos) return true;
  if (s.find_first_of("{}[]&*!|>'\"%@`\n") != std::string::npos) return true;
  if (s.rfind("-", 0) == 0 || s.rfind("? ", 0) == 0) return true;
  return false;
}

std::string quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

struct YamlWriter {
  static void walk(const std::map<std::string, std::string>& values,
                   const std::map<std::string, Origin>& origins, const std::string& prefix,
                   int indent, std::string* out) {
    // Collect direct children and list indices.
    std::vector<std::string> keys;
    std::vector<int64_t> indices;
    std::string count_key = prefix + ".__count";
    bool is_list = values.count(count_key) > 0;
    for (const auto& kv : values) {
      const std::string& p = kv.first;
      if (!prefix.empty()) {
        if (p.rfind(prefix + ".", 0) != 0) continue;
      }
      std::string rest = prefix.empty() ? p : p.substr(prefix.size() + 1);
      if (rest.empty()) continue;
      size_t dot = rest.find('.');
      std::string head = (dot == std::string::npos) ? rest : rest.substr(0, dot);
      if (head == "__count") continue;
      // Unique-ify.
      if (!head.empty() && std::isdigit(static_cast<unsigned char>(head[0])) &&
          head.find_first_not_of("0123456789") == std::string::npos) {
        int64_t idx = std::stoll(head);
        bool seen = false;
        for (int64_t v : indices) seen |= (v == idx);
        if (!seen) indices.push_back(idx);
      } else {
        bool seen = false;
        for (const auto& k : keys) seen |= (k == head);
        if (!seen) keys.push_back(head);
      }
    }
    std::string pad(indent * 2, ' ');
    if (is_list) {
      int64_t n = 0;
      auto it = values.find(count_key);
      if (it != values.end()) n = std::stoll(it->second);
      for (int64_t i = 0; i < n; ++i) {
        std::string child = prefix.empty() ? std::to_string(i) : prefix + "." + std::to_string(i);
        emit_item(values, origins, child, indent, out);
      }
      return;
    }
    for (const auto& k : keys) {
      std::string child = prefix.empty() ? k : prefix + "." + k;
      bool has_children = subtree_exists(values, prefix, k);
      if (has_children) {
        *out += pad + k + ":\n";
        walk(values, origins, child, indent + 1, out);
      } else {
        auto vit = values.find(child);
        if (vit == values.end()) continue;
        std::string v = vit->second;
        if (v.empty()) {
          *out += pad + k + ": null\n";
        } else if (needs_quotes(v)) {
          *out += pad + k + ": " + quote(v) + "\n";
        } else {
          *out += pad + k + ": " + v + "\n";
        }
      }
    }
  }

  static bool subtree_exists(const std::map<std::string, std::string>& values,
                             const std::string& prefix, const std::string& head) {
    std::string child = prefix.empty() ? head : prefix + "." + head;
    // A child is a subtree if any key starts with "child." and is not just
    // the child itself, or child is a list with a __count marker.
    if (values.count(child + ".__count") > 0) return true;
    for (const auto& kv : values) {
      if (kv.first.rfind(child + ".", 0) == 0) return true;
    }
    return false;
  }

  static void emit_item(const std::map<std::string, std::string>& values,
                        const std::map<std::string, Origin>& origins, const std::string& item,
                        int indent, std::string* out) {
    std::string pad(indent * 2, ' ');
    // Item is a list element: could be scalar or subtree.
    if (values.count(item) > 0 && !subtree_exists(values, parent_of(item), last_of(item))) {
      std::string v = values.at(item);
      if (v.empty()) *out += pad + "- null\n";
      else if (needs_quotes(v)) *out += pad + "- " + quote(v) + "\n";
      else *out += pad + "- " + v + "\n";
      return;
    }
    // Subtree item: "- key: value" style with subsequent lines deeper.
    std::string body;
    walk(values, origins, item, 0, &body);
    bool first = true;
    size_t pos = 0;
    while (pos < body.size()) {
      size_t nl = body.find('\n', pos);
      if (nl == std::string::npos) nl = body.size();
      std::string line = body.substr(pos, nl - pos);
      if (first) {
        *out += pad + "- " + (line.empty() ? "" : line.substr(0)) + "\n";
        first = false;
      } else {
        *out += pad + "  " + line + "\n";
      }
      pos = nl + 1;
    }
  }

  static std::string parent_of(const std::string& p) {
    size_t pos = p.find_last_of('.');
    return pos == std::string::npos ? std::string() : p.substr(0, pos);
  }
  static std::string last_of(const std::string& p) {
    size_t pos = p.find_last_of('.');
    return pos == std::string::npos ? p : p.substr(pos + 1);
  }
};

}  // namespace

std::string ConfigDocument::to_yaml() const {
  std::string out;
  YamlWriter::walk(values_, origins_, "", 0, &out);
  return out;
}

std::string ConfigDocument::origins_to_yaml() const {
  std::string out;
  for (const auto& kv : origins_) out += kv.first + ": " + origin_name(kv.second) + "\n";
  return out;
}

void ConfigDocument::load_origins_yaml(const std::string& text) {
  try {
    YAML::Node root = YAML::Load(text);
    for (const auto& kv : root) {
      auto o = origin_from_name(kv.second.as<std::string>("default"));
      if (o.ok()) origins_[kv.first.as<std::string>()] = *o;
    }
  } catch (const std::exception&) {
    // Origins sidecar unreadable: keys stay at their parsed default origin.
  }
}

}  // namespace instr