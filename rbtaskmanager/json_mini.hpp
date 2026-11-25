// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
// Copyright (C) 2026 xiaprojects
//
// Tiny read-only JSON parser for Wayfire IPC replies. Handles escaped strings
// (window titles are arbitrary text) and nested objects. Not a general library.
#pragma once

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace jsonmini
{

struct Value
{
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::map<std::string, Value> obj;

    bool is_object() const { return type == Type::Object; }
    bool is_array() const { return type == Type::Array; }

    // Member access; returns a Null value when missing or not an object.
    const Value &operator[](const std::string &key) const
    {
        static const Value null_value;
        if (type != Type::Object)
            return null_value;
        auto it = obj.find(key);
        return it == obj.end() ? null_value : it->second;
    }
    std::string as_string(const std::string &def = {}) const { return type == Type::String ? str : def; }
    int as_int(int def = -1) const { return type == Type::Number ? static_cast<int>(num) : def; }
    bool as_bool(bool def = false) const { return type == Type::Bool ? b : def; }
};

class Parser
{
public:
    explicit Parser(const std::string &s) : s_(s) {}

    // Returns false on syntax error.
    bool parse(Value &out)
    {
        if (!value(out, 0))
            return false;
        ws();
        return i_ == s_.size();
    }

private:
    const std::string &s_;
    size_t i_ = 0;

    void ws()
    {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t'))
            ++i_;
    }
    bool lit(const char *w)
    {
        size_t n = std::char_traits<char>::length(w);
        if (s_.compare(i_, n, w) != 0)
            return false;
        i_ += n;
        return true;
    }
    static void utf8(std::string &o, unsigned cp)
    {
        if (cp < 0x80)
            o += static_cast<char>(cp);
        else if (cp < 0x800)
        {
            o += static_cast<char>(0xC0 | (cp >> 6));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned &cp)
    {
        if (i_ + 4 > s_.size())
            return false;
        cp = 0;
        for (int k = 0; k < 4; ++k)
        {
            char c = s_[i_++];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= c - '0';
            else if (c >= 'a' && c <= 'f') cp |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') cp |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool string(std::string &o)
    {
        if (i_ >= s_.size() || s_[i_] != '"')
            return false;
        ++i_;
        while (i_ < s_.size())
        {
            char c = s_[i_++];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                o += c;
                continue;
            }
            if (i_ >= s_.size())
                return false;
            char e = s_[i_++];
            switch (e)
            {
            case '"': o += '"'; break;
            case '\\': o += '\\'; break;
            case '/': o += '/'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'n': o += '\n'; break;
            case 'r': o += '\r'; break;
            case 't': o += '\t'; break;
            case 'u':
            {
                unsigned cp;
                if (!hex4(cp))
                    return false;
                if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0)
                {
                    i_ += 2;
                    unsigned lo;
                    if (!hex4(lo))
                        return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(o, cp);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }
    bool value(Value &v, int depth)
    {
        if (depth > 64)
            return false;
        ws();
        if (i_ >= s_.size())
            return false;
        char c = s_[i_];
        if (c == '{')
        {
            v.type = Value::Type::Object;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == '}')
                return ++i_, true;
            for (;;)
            {
                ws();
                std::string key;
                if (!string(key))
                    return false;
                ws();
                if (i_ >= s_.size() || s_[i_++] != ':')
                    return false;
                if (!value(v.obj[key], depth + 1))
                    return false;
                ws();
                if (i_ >= s_.size())
                    return false;
                if (s_[i_] == ',') { ++i_; continue; }
                if (s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (c == '[')
        {
            v.type = Value::Type::Array;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == ']')
                return ++i_, true;
            for (;;)
            {
                v.arr.emplace_back();
                if (!value(v.arr.back(), depth + 1))
                    return false;
                ws();
                if (i_ >= s_.size())
                    return false;
                if (s_[i_] == ',') { ++i_; continue; }
                if (s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (c == '"')
        {
            v.type = Value::Type::String;
            return string(v.str);
        }
        if (lit("true")) { v.type = Value::Type::Bool; v.b = true; return true; }
        if (lit("false")) { v.type = Value::Type::Bool; v.b = false; return true; }
        if (lit("null")) { v.type = Value::Type::Null; return true; }

        const char *start = s_.c_str() + i_;
        char *end = nullptr;
        double d = std::strtod(start, &end);
        if (end == start)
            return false;
        i_ += static_cast<size_t>(end - start);
        v.type = Value::Type::Number;
        v.num = d;
        return true;
    }
};

inline bool parse(const std::string &s, Value &out)
{
    return Parser(s).parse(out);
}

} // namespace jsonmini
