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
 * @file lib/common/include/instr/common/command_table.h
 * @brief the shared command table (REQ §3.2)
 *
 * Each daemon defines its operations once, in a single command table
 * (name, description, argument spec, handler). Both the gRPC service
 * implementation and the ctrl server dispatch from that table, which makes
 * _ctrl coverage structural rather than hand-maintained, and gives help and
 * shell completion for free.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */


#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "instr/common/error.h"
#include "instr/common/json.h"

namespace instr {

struct CommandResult {
  bool ok = true;
  std::string error_code;
  std::string error_message;
  std::string json_data;  // JSON object/string payload

  static CommandResult good(std::string json) {
    CommandResult r;
    r.json_data = std::move(json);
    return r;
  }
  static CommandResult good(JValue v) { return good(v.dump()); }
  static CommandResult err(Errc c, const std::string& msg) {
    CommandResult r;
    r.ok = false;
    r.error_code = errc_name(c);
    r.error_message = msg;
    return r;
  }
  static CommandResult err(Errc c, const std::string& msg, const std::string&) {
    return err(c, msg);
  }
  static CommandResult from_error(const Error& e) {
    return e.ok() ? good(std::string("{}")) : err(e.code, e.str());
  }
  // {"ok":true,"data":...} / {"ok":false,"error":{"code":...,"message":...}}
  std::string envelope() const;
};

struct ArgSpec {
  std::string name;      // JSON key
  bool required = false;
  std::string help;
};

struct CommandSpec {
  std::string op;        // e.g. "raise"
  std::string description;
  std::vector<ArgSpec> args;
  std::function<CommandResult(const std::string& json_args)> handler;
};

class CommandTable {
 public:
  void add(CommandSpec spec) { ops_[spec.op] = std::move(spec); }
  const CommandSpec* find(const std::string& op) const {
    auto it = ops_.find(op);
    return it == ops_.end() ? nullptr : &it->second;
  }
  std::vector<const CommandSpec*> all() const {
    std::vector<const CommandSpec*> out;
    for (const auto& kv : ops_) out.push_back(&kv.second);
    return out;
  }
  size_t size() const { return ops_.size(); }

  CommandResult dispatch(const std::string& op, const std::string& json_args) const {
    const CommandSpec* spec = find(op);
    if (!spec)
      return CommandResult::err(Errc::NotFound, "unknown op: " + op);
    return spec->handler(json_args);
  }

  // --help output; prog names the tool for usage line.
  std::string help_text(const std::string& prog) const;
  std::string help_for(const std::string& op) const;

  // Shell completion generation (bash/zsh) — free from the table.
  std::string completion_bash(const std::string& prog) const;
  std::string completion_zsh(const std::string& prog) const;

 private:
  std::map<std::string, CommandSpec> ops_;
};

// Convenience: parse json_args into a JValue object (empty object on empty).
Result<JValue> args_object(const std::string& json_args);

}  // namespace instr