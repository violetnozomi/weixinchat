#pragma once

#include <cstdint>
#include <exception>
#include <string>

#include <json/json.h>

namespace dto::json {

inline bool Parse(const std::string& text, Json::Value& root) {
    Json::Reader reader;
    return reader.parse(text, root);
}

inline std::string Serialize(const Json::Value& value) {
    Json::FastWriter writer;
    auto text = writer.write(value);
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

inline bool GetRequiredString(const Json::Value& root,
                              const char* key,
                              std::string& out) {
    if (!root.isObject() || !root.isMember(key)) {
        return false;
    }
    const auto& value = root[key];
    if (!value.isString()) {
        return false;
    }
    out = value.asString();
    return true;
}

inline bool GetRequiredInt(const Json::Value& root,
                           const char* key,
                           int& out) {
    if (!root.isObject() || !root.isMember(key)) {
        return false;
    }
    const auto& value = root[key];
    if (value.isInt()) {
        out = value.asInt();
        return true;
    }
    if (value.isString()) {
        try {
            out = std::stoi(value.asString());
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

inline bool GetRequiredUInt64(const Json::Value& root,
                              const char* key,
                              uint64_t& out) {
    if (!root.isObject() || !root.isMember(key)) {
        return false;
    }
    const auto& value = root[key];
    if (value.isUInt64()) {
        out = value.asUInt64();
        return true;
    }
    if (value.isInt64()) {
        const auto parsed = value.asInt64();
        if (parsed < 0) {
            return false;
        }
        out = static_cast<uint64_t>(parsed);
        return true;
    }
    if (value.isString()) {
        try {
            out = std::stoull(value.asString());
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

inline uint64_t GetOptionalUInt64(const Json::Value& root,
                                  const char* key,
                                  uint64_t default_value = 0) {
    uint64_t value = default_value;
    if (!GetRequiredUInt64(root, key, value)) {
        return default_value;
    }
    return value;
}

inline int64_t GetOptionalInt64(const Json::Value& root,
                                const char* key,
                                int64_t default_value = 0) {
    if (!root.isMember(key) || root[key].isNull()) {
        return default_value;
    }
    const auto& value = root[key];
    if (value.isInt64()) {
        return value.asInt64();
    }
    if (value.isString()) {
        try {
            return std::stoll(value.asString());
        } catch (const std::exception&) {
            return default_value;
        }
    }
    return default_value;
}

}  // namespace dto::json
