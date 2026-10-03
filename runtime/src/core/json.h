// Minimal JSON reader (used for the story definition exported by tools/lua_data_dump.py until
// the original Lua runs inside the player).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace oyster {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json parse(const std::string& text);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    double num(double def = 0.0) const { return type_ == Type::Number ? num_ : (type_ == Type::Bool ? (b_ ? 1 : 0) : def); }
    bool boolean(bool def = false) const { return type_ == Type::Bool ? b_ : def; }
    const std::string& str() const { return str_; }
    const std::vector<Json>& arr() const { return arr_; }
    const std::map<std::string, Json>& obj() const { return obj_; }
    size_t size() const { return type_ == Type::Array ? arr_.size() : obj_.size(); }

    const Json& operator[](const std::string& key) const;  // Null if missing
    const Json& operator[](size_t i) const;                // Null if out of range
    bool has(const std::string& key) const { return obj_.count(key) != 0; }

private:
    friend class JsonParser;
    Type type_ = Type::Null;
    bool b_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Json> arr_;
    std::map<std::string, Json> obj_;
};

}  // namespace oyster
