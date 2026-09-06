// Arches Instrumentation — configuration with origin tracking and merge
// semantics (REQ §4, §9). Every key carries an origin: default / app / local
// / hq. Resets and HQ pushes consult origin to decide what they may
// overwrite; merge semantics preserve operator edits when a release adds
// keys. All formats validated against JSON Schemas at load.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "instr/common/error.h"
#include "instr/common/json.h"

namespace instr {

enum class Origin { Default = 0, App = 1, Local = 2, Hq = 3 };
const char* origin_name(Origin);
Result<Origin> origin_from_name(const std::string&);

class ConfigDocument {
 public:
  ConfigDocument() = default;

  // Parse YAML text, validate against the JSON Schema, flatten to dot-paths.
  // source_name appears in rejection errors (file-and-line diagnosability).
  static Result<ConfigDocument> parse(const std::string& yaml_text,
                                      const JValue& schema,
                                      const std::string& source_name);

  // Empty document (no schema validation) for programmatic construction.
  static ConfigDocument empty();

  bool has(const std::string& dot_path) const;
  std::string get_string(const std::string& dot_path, const std::string& dflt = "") const;
  int64_t get_int(const std::string& dot_path, int64_t dflt = 0) const;
  double get_double(const std::string& dot_path, double dflt = 0) const;
  bool get_bool(const std::string& dot_path, bool dflt = false) const;
  // List values (e.g. log sources); empty when absent.
  std::vector<std::string> get_list(const std::string& dot_path) const;

  // Raw value as a YAML-ish scalar string, "" when absent.
  std::string raw(const std::string& dot_path) const;

  void set(const std::string& dot_path, const std::string& scalar, Origin origin);
  void set_number(const std::string& dot_path, double v, Origin origin);
  void set_bool(const std::string& dot_path, bool v, Origin origin);

  Origin origin_of(const std::string& dot_path) const;  // Default when unset
  const std::map<std::string, std::string>& values() const { return values_; }
  const std::map<std::string, Origin>& origins() const { return origins_; }

  // Merge `incoming` (origin `in_origin`) into this document.
  // HQ pushes overwrite default/app/hq keys; local (operator) keys are
  // protected unless force. Returns the rejected dot-paths.
  std::vector<std::string> merge_from(const ConfigDocument& incoming, Origin in_origin,
                                      bool force);

  // Release merge: add keys that are missing entirely (new defaults); keep
  // every existing value. Returns the added dot-paths.
  std::vector<std::string> merge_release(const ConfigDocument& defaults);

  // System reset: drop everything except origin=Default entries (REQ §6.1).
  void reset_to_defaults();

  // Serialise back to YAML (nested maps; scalar strings preserved verbatim).
  std::string to_yaml() const;
  // Origins sidecar: dot-path → origin name.
  std::string origins_to_yaml() const;
  void load_origins_yaml(const std::string& text);

 private:
  std::map<std::string, std::string> values_;
  std::map<std::string, Origin> origins_;
};

// Load a JSON schema from text (for schemas/*.json at daemon startup).
Result<JValue> load_schema_text(const std::string& text);

}  // namespace instr