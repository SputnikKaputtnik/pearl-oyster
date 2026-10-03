#include "core/json.h"

#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace oyster {

static const Json kNull;

const Json& Json::operator[](const std::string& key) const {
    if (type_ != Type::Object) return kNull;
    auto it = obj_.find(key);
    return it == obj_.end() ? kNull : it->second;
}

const Json& Json::operator[](size_t i) const {
    if (type_ != Type::Array || i >= arr_.size()) return kNull;
    return arr_[i];
}

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    Json value() {
        ws();
        if (p_ >= s_.size()) fail("unexpected end");
        char c = s_[p_];
        Json j;
        if (c == '{') {
            j.type_ = Json::Type::Object;
            ++p_;
            ws();
            if (peek('}')) { ++p_; return j; }
            for (;;) {
                ws();
                std::string k = string();
                ws();
                expect(':');
                j.obj_[k] = value();
                ws();
                if (peek(',')) { ++p_; continue; }
                expect('}');
                return j;
            }
        }
        if (c == '[') {
            j.type_ = Json::Type::Array;
            ++p_;
            ws();
            if (peek(']')) { ++p_; return j; }
            for (;;) {
                j.arr_.push_back(value());
                ws();
                if (peek(',')) { ++p_; continue; }
                expect(']');
                return j;
            }
        }
        if (c == '"') {
            j.type_ = Json::Type::String;
            j.str_ = string();
            return j;
        }
        if (s_.compare(p_, 4, "true") == 0) { p_ += 4; j.type_ = Json::Type::Bool; j.b_ = true; return j; }
        if (s_.compare(p_, 5, "false") == 0) { p_ += 5; j.type_ = Json::Type::Bool; return j; }
        if (s_.compare(p_, 4, "null") == 0) { p_ += 4; return j; }
        if (s_.compare(p_, 8, "Infinity") == 0) { p_ += 8; j.type_ = Json::Type::Number; j.num_ = std::numeric_limits<double>::infinity(); return j; }
        if (s_.compare(p_, 9, "-Infinity") == 0) { p_ += 9; j.type_ = Json::Type::Number; j.num_ = -std::numeric_limits<double>::infinity(); return j; }
        if (s_.compare(p_, 3, "NaN") == 0) { p_ += 3; j.type_ = Json::Type::Number; j.num_ = std::numeric_limits<double>::quiet_NaN(); return j; }
        const char* b = s_.c_str() + p_;
        char* e = nullptr;
        j.num_ = std::strtod(b, &e);
        if (e == b) fail("bad value");
        p_ += static_cast<size_t>(e - b);
        j.type_ = Json::Type::Number;
        return j;
    }

    void finish() {
        ws();
        if (p_ != s_.size()) fail("trailing data");
    }

private:
    void ws() { while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\n' || s_[p_] == '\r' || s_[p_] == '\t')) ++p_; }
    bool peek(char c) const { return p_ < s_.size() && s_[p_] == c; }
    void expect(char c) {
        if (!peek(c)) fail(std::string("expected ") + c);
        ++p_;
    }
    [[noreturn]] void fail(const std::string& m) const { throw std::runtime_error("json: " + m + " at " + std::to_string(p_)); }

    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 63)); }
        else { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63)); }
    }

    std::string string() {
        expect('"');
        std::string o;
        while (p_ < s_.size() && s_[p_] != '"') {
            char c = s_[p_++];
            if (c != '\\') { o += c; continue; }
            if (p_ >= s_.size()) fail("bad escape");
            char e = s_[p_++];
            switch (e) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    unsigned cp = static_cast<unsigned>(std::strtoul(s_.substr(p_, 4).c_str(), nullptr, 16));
                    p_ += 4;
                    utf8(o, cp);
                    break;
                }
                default: o += e;
            }
        }
        expect('"');
        return o;
    }

    const std::string& s_;
    size_t p_ = 0;
};

Json Json::parse(const std::string& text) {
    JsonParser p(text);
    Json j = p.value();
    p.finish();
    return j;
}

}  // namespace oyster
