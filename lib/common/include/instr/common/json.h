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
 * @file lib/common/include/instr/common/json.h
 * @brief JSON value, parser, serialiser and a JSON Schema
 *
 * subset validator (REQ §4 config loading, §9). Every YAML format has a JSON
 * Schema in schemas/ and is validated at load, rejected with a clear
 * file-and-line error on failure.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "instr/common/error.h"

namespace instr
{

class JValue
{
  public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    JValue() : type_(Type::Null)
    {
    }
    explicit JValue(bool b) : type_(Type::Bool), bool_(b)
    {
    }
    explicit JValue(double n) : type_(Type::Number), num_(n)
    {
    }
    explicit JValue(int n) : type_(Type::Number), num_(n)
    {
    }
    explicit JValue(int64_t n) : type_(Type::Number), num_(static_cast<double>(n))
    {
    }
    explicit JValue(std::string s) : type_(Type::String), str_(std::move(s))
    {
    }
    explicit JValue(const char* s) : type_(Type::String), str_(s)
    {
    } // beats pointer→bool

    static JValue array()
    {
        JValue v;
        v.type_ = Type::Array;
        return v;
    }
    static JValue object()
    {
        JValue v;
        v.type_ = Type::Object;
        return v;
    }

    Type type() const
    {
        return type_;
    }
    bool is_null() const
    {
        return type_ == Type::Null;
    }
    bool is_bool() const
    {
        return type_ == Type::Bool;
    }
    bool is_number() const
    {
        return type_ == Type::Number;
    }
    bool is_string() const
    {
        return type_ == Type::String;
    }
    bool is_array() const
    {
        return type_ == Type::Array;
    }
    bool is_object() const
    {
        return type_ == Type::Object;
    }

    bool as_bool(bool dflt = false) const
    {
        return is_bool() ? bool_ : dflt;
    }
    double as_number(double dflt = 0) const
    {
        return is_number() ? num_ : dflt;
    }
    const std::string& as_string() const
    {
        static const std::string e;
        return is_string() ? str_ : e;
    }

    // Arrays
    const std::vector<JValue>& items() const
    {
        return arr_;
    }
    void push(JValue v)
    {
        arr_.push_back(std::move(v));
    }
    size_t size() const
    {
        return type_ == Type::Array ? arr_.size() : (type_ == Type::Object ? obj_.size() : 0);
    }

    // Objects preserve insertion order.
    const std::vector<std::pair<std::string, JValue>>& members() const
    {
        return obj_;
    }
    const JValue* find(const std::string& key) const
    {
        if (type_ != Type::Object) return nullptr;
        for (const auto& kv : obj_)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    JValue* find(const std::string& key)
    {
        if (type_ != Type::Object) return nullptr;
        for (auto& kv : obj_)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    void set(const std::string& key, JValue v)
    {
        if (type_ != Type::Object)
        {
            type_ = Type::Object;
            obj_.clear();
            arr_.clear();
        }
        for (auto& kv : obj_)
            if (kv.first == key)
            {
                kv.second = std::move(v);
                return;
            }
        obj_.emplace_back(key, std::move(v));
    }
    bool has(const std::string& key) const
    {
        return find(key) != nullptr;
    }

    // ---- convenience accessors for command-table handlers ----
    std::string get_string(const std::string& key, const std::string& dflt = "") const
    {
        const JValue* v = find(key);
        return (v && v->is_string()) ? v->as_string() : (v && v->is_number() ? v->dump() : dflt);
    }
    double get_number(const std::string& key, double dflt = 0) const
    {
        const JValue* v = find(key);
        return v && v->is_number() ? v->as_number() : dflt;
    }
    int64_t get_int(const std::string& key, int64_t dflt = 0) const
    {
        return static_cast<int64_t>(get_number(key, double(dflt)));
    }
    bool get_bool(const std::string& key, bool dflt = false) const
    {
        const JValue* v = find(key);
        return v && v->is_bool() ? v->as_bool() : dflt;
    }

    std::string dump(int indent = 0) const;

  private:
    Type type_;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<JValue> arr_;
    std::vector<std::pair<std::string, JValue>> obj_;
};

// Strict JSON parser. On failure the error carries the byte offset.
Result<JValue> json_parse(const std::string& text);

// YAML → JSON projection (scalars, maps, sequences). Used to validate YAML
// configuration against JSON Schemas.
Result<JValue> json_from_yaml_text(const std::string& yaml_text);

class JsonValidator
{
  public:
    struct Issue
    {
        std::string path; // JSON pointer-ish, e.g. /network/ifaces/2/mode
        std::string message;
    };
    // Validates `instance` against `schema`. Returns false and fills issues on
    // failure. Supported keywords: type, properties, required,
    // additionalProperties (boolean or schema), enum, items, minItems,
    // maxItems, minLength, maxLength, minimum, maximum, pattern,
    // description (ignored).
    static bool validate(const JValue& schema, const JValue& instance, std::vector<Issue>* issues);
};

} // namespace instr