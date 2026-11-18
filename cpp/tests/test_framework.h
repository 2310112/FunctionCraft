// test_framework.h — 极简测试框架（各模块独立写测试文件，无冲突）
#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fcgtest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

// 断言失败时抛异常，由 runner 捕获
struct Failure {
    std::string message;
};

inline void fail(const std::string& msg, const char* file, int line) {
    throw Failure{std::string(file) + ":" + std::to_string(line) + "  " + msg};
}

inline std::string fmt(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

inline std::string fmt(const std::string& v) { return "\"" + v + "\""; }
inline std::string fmt(const char* v) { return "\"" + std::string(v) + "\""; }

// 其它类型（size_t / int / 枚举…）走通用输出，避免隐式转 double
template <typename T>
std::string fmt(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

}  // namespace fcgtest

#define FCG_TEST(name)                                                        \
    static void test_##name();                                                \
    static ::fcgtest::Registrar reg_##name(#name, test_##name);               \
    static void test_##name()

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) ::fcgtest::fail("CHECK 失败: " #cond, __FILE__, __LINE__); \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        auto _va = (a);                                                       \
        auto _vb = (b);                                                       \
        if (!(_va == _vb))                                                    \
            ::fcgtest::fail("CHECK_EQ 失败: " #a " == " #b " (左=" +          \
                                ::fcgtest::fmt(_va) + ", 右=" +               \
                                ::fcgtest::fmt(_vb) + ")",                    \
                            __FILE__, __LINE__);                              \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                 \
    do {                                                                      \
        double _va = (a);                                                     \
        double _vb = (b);                                                     \
        if (std::fabs(_va - _vb) > (tol))                                     \
            ::fcgtest::fail("CHECK_NEAR 失败: " #a " ≈ " #b " (左=" +         \
                                ::fcgtest::fmt(_va) + ", 右=" +               \
                                ::fcgtest::fmt(_vb) + ")",                    \
                            __FILE__, __LINE__);                              \
    } while (0)

#define CHECK_TRUE(cond) CHECK(cond)
#define CHECK_FALSE(cond) CHECK(!(cond))
