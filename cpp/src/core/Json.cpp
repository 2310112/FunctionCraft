// Json.cpp — 极简 JSON 实现
#include "Json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fcg {

const Json Json::kNull;

namespace {

class JParser {
public:
    explicit JParser(const std::string& s) : s_(s) {}

    Json parse(std::string* err) {
        skipWs();
        Json v = value();
        skipWs();
        if (ok_ && pos_ != s_.size()) fail("JSON 结尾有多余内容");
        if (!ok_ && err) *err = errMsg_;
        return ok_ ? v : Json();
    }

private:
    void fail(const std::string& m) {
        if (ok_) {
            ok_ = false;
            errMsg_ = m + " (位置 " + std::to_string(pos_) + ")";
        }
    }

    void skipWs() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool consume(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    Json value() {
        skipWs();
        if (pos_ >= s_.size()) {
            fail("意外的结尾");
            return Json();
        }
        char c = s_[pos_];
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') return Json(string());
        if (c == 't') {
            if (s_.compare(pos_, 4, "true") == 0) {
                pos_ += 4;
                return Json(true);
            }
            fail("无效字面量");
            return Json();
        }
        if (c == 'f') {
            if (s_.compare(pos_, 5, "false") == 0) {
                pos_ += 5;
                return Json(false);
            }
            fail("无效字面量");
            return Json();
        }
        if (c == 'n') {
            if (s_.compare(pos_, 4, "null") == 0) {
                pos_ += 4;
                return Json();
            }
            fail("无效字面量");
            return Json();
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return number();
        fail(std::string("无效字符 '") + c + "'");
        return Json();
    }

    Json object() {
        consume('{');
        std::map<std::string, Json> m;
        skipWs();
        if (consume('}')) return Json(m);
        while (true) {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') {
                fail("对象的键必须是字符串");
                return Json();
            }
            std::string k = string();
            skipWs();
            if (!consume(':')) {
                fail("缺少 ':'");
                return Json();
            }
            Json v = value();
            if (!ok_) return Json();
            m[k] = std::move(v);
            skipWs();
            if (consume(',')) continue;
            if (consume('}')) break;
            fail("对象缺少 '}'");
            return Json();
        }
        return Json(std::move(m));
    }

    Json array() {
        consume('[');
        std::vector<Json> arr;
        skipWs();
        if (consume(']')) return Json(std::move(arr));
        while (true) {
            Json v = value();
            if (!ok_) return Json();
            arr.push_back(std::move(v));
            skipWs();
            if (consume(',')) continue;
            if (consume(']')) break;
            fail("数组缺少 ']'");
            return Json();
        }
        return Json(std::move(arr));
    }

    std::string string() {
        consume('"');
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == '"') {
                ++pos_;
                return out;
            }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= s_.size()) break;
                char e = s_[pos_++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'u': {
                        if (pos_ + 4 > s_.size()) {
                            fail("\\u 转义不完整");
                            return out;
                        }
                        std::string hex = s_.substr(pos_, 4);
                        pos_ += 4;
                        unsigned cp = std::stoul(hex, nullptr, 16);
                        // UTF-8 编码
                        if (cp < 0x80) {
                            out += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default:
                        out += e;
                        break;
                }
                continue;
            }
            out += c;
            ++pos_;
        }
        fail("字符串未闭合");
        return out;
    }

    Json number() {
        size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        std::string num = s_.substr(start, pos_ - start);
        if (num.empty()) {
            fail("无效数字");
            return Json();
        }
        return Json(std::strtod(num.c_str(), nullptr));
    }

    const std::string& s_;
    size_t pos_ = 0;
    bool ok_ = true;
    std::string errMsg_;
};

void dumpTo(std::ostringstream& os, const std::string& s) {
    os << '"';
    for (char c : s) {
        switch (c) {
            case '"': os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n"; break;
            case '\t': os << "\\t"; break;
            case '\r': os << "\\r"; break;
            default: os << c; break;
        }
    }
    os << '"';
}

void dumpValue(std::ostringstream& os, const Json& j, int indent, int level);

void dumpValue(std::ostringstream& os, const Json& j, int indent, int level) {
    std::string pad(static_cast<size_t>(indent * level), ' ');
    std::string padIn(static_cast<size_t>(indent * (level + 1)), ' ');
    switch (j.type()) {
        case Json::Type::Null: os << "null"; break;
        case Json::Type::Bool: os << (j.asBool() ? "true" : "false"); break;
        case Json::Type::Number: {
            double v = j.asNumber();
            if (v == std::floor(v) && std::fabs(v) < 1e15) {
                os << static_cast<long long>(v);
            } else {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.17g", v);
                os << buf;
            }
            break;
        }
        case Json::Type::String: dumpTo(os, j.asString()); break;
        case Json::Type::Array: {
            os << "[";
            const auto& items = j.items();
            if (!items.empty()) {
                os << "\n";
                for (size_t i = 0; i < items.size(); ++i) {
                    os << padIn;
                    dumpValue(os, items[i], indent, level + 1);
                    if (i + 1 < items.size()) os << ",";
                    os << "\n";
                }
                os << pad;
            }
            os << "]";
            break;
        }
        case Json::Type::Object: {
            os << "{";
            auto keys = j.keys();
            if (!keys.empty()) {
                os << "\n";
                for (size_t i = 0; i < keys.size(); ++i) {
                    os << padIn;
                    dumpTo(os, keys[i]);
                    os << ": ";
                    dumpValue(os, j[keys[i]], indent, level + 1);
                    if (i + 1 < keys.size()) os << ",";
                    os << "\n";
                }
                os << pad;
            }
            os << "}";
            break;
        }
    }
}

}  // namespace

const Json& Json::operator[](const std::string& key) const {
    if (!isObject()) return kNull;
    auto it = obj_.find(key);
    return it == obj_.end() ? kNull : it->second;
}

const Json& Json::at(size_t i) const {
    if (!isArray() || i >= arr_.size()) return kNull;
    return arr_[i];
}

size_t Json::size() const {
    if (isArray()) return arr_.size();
    if (isObject()) return obj_.size();
    if (isString()) return s_.size();
    return 0;
}

bool Json::has(const std::string& key) const { return isObject() && obj_.count(key) > 0; }

std::vector<std::string> Json::keys() const {
    std::vector<std::string> out;
    for (const auto& kv : obj_) out.push_back(kv.first);
    return out;
}

Json Json::parse(const std::string& text, std::string* err) {
    JParser p(text);
    return p.parse(err);
}

Json Json::parseFile(const std::string& path, std::string* err) {
    // 路径按 UTF-8 解释（Windows 下需转宽字符，否则中文路径无法打开）
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) {
        if (err) *err = "无法打开文件: " + path;
        return Json();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str(), err);
}

std::string Json::dump(int indent) const {
    std::ostringstream os;
    dumpValue(os, *this, indent, 0);
    return os.str();
}

}  // namespace fcg
