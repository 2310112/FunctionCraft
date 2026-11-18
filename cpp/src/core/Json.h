// Json.h — 极简 JSON 解析/序列化（无第三方依赖）
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fcg {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    explicit Json(bool v) : type_(Type::Bool), b_(v) {}
    explicit Json(double v) : type_(Type::Number), n_(v) {}
    explicit Json(std::string v) : type_(Type::String), s_(std::move(v)) {}
    explicit Json(std::vector<Json> v) : type_(Type::Array), arr_(std::move(v)) {}
    explicit Json(std::map<std::string, Json> v) : type_(Type::Object), obj_(std::move(v)) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return isBool() ? b_ : def; }
    double asNumber(double def = 0.0) const { return isNumber() ? n_ : def; }
    int asInt(int def = 0) const { return isNumber() ? static_cast<int>(n_) : def; }
    const std::string& asString() const { return s_; }

    // 安全访问
    const Json& operator[](const std::string& key) const;
    const Json& at(size_t i) const;
    size_t size() const;
    bool has(const std::string& key) const;
    std::vector<std::string> keys() const;

    // 数组/对象遍历
    const std::vector<Json>& items() const { return arr_; }

    static Json parse(const std::string& text, std::string* err = nullptr);
    static Json parseFile(const std::string& path, std::string* err = nullptr);
    std::string dump(int indent = 2) const;

private:
    const static Json kNull;
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0.0;
    std::string s_;
    std::vector<Json> arr_;
    std::map<std::string, Json> obj_;
};

}  // namespace fcg
