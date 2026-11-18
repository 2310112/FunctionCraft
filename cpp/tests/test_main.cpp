// test_main.cpp — 测试运行器
#include <cstdio>
#include <exception>
#include <string>

#include "test_framework.h"

namespace fcgtest {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

}  // namespace fcgtest

int main(int argc, char** argv) {
    std::string filter = argc > 1 ? argv[1] : "";
    auto& tests = fcgtest::registry();
    int passed = 0;
    int failed = 0;

    for (auto& t : tests) {
        if (!filter.empty() && t.name.find(filter) == std::string::npos) continue;
        try {
            t.fn();
            ++passed;
            std::printf("  [PASS] %s\n", t.name.c_str());
        } catch (const fcgtest::Failure& f) {
            ++failed;
            std::printf("  [FAIL] %s\n         %s\n", t.name.c_str(), f.message.c_str());
        } catch (const std::exception& e) {
            ++failed;
            std::printf("  [FAIL] %s\n         异常: %s\n", t.name.c_str(), e.what());
        } catch (...) {
            ++failed;
            std::printf("  [FAIL] %s\n         未知异常\n", t.name.c_str());
        }
    }

    std::printf("\n结果: %d 通过, %d 失败, 共 %d 个\n", passed, failed, passed + failed);
    return failed == 0 ? 0 : 1;
}
