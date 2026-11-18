// test_integration.cpp — 跨模块端到端测试（Lead 维护）
// 覆盖：关卡数据自洽、判定正确性（防误判/漏判）、评分合理性、生成器、打印回环、性能
#include "test_framework.h"

#include "core/Constraint.h"
#include "core/Domain.h"
#include "core/EndlessGen.h"
#include "core/Expr.h"
#include "core/Level.h"
#include "core/Numeric.h"
#include "core/Parser.h"
#include "core/Scoring.h"
#include "core/Simplify.h"

#include <algorithm>
#include <chrono>

using namespace fcg;

namespace {

std::string LevelsPath() {
#ifdef FCG_SOURCE_DIR
    return std::string(FCG_SOURCE_DIR) + "/data/levels.json";
#else
    return "data/levels.json";
#endif
}

std::vector<LevelDef> LoadAll() {
    std::string err;
    auto ls = loadLevels(LevelsPath(), &err);
    if (!err.empty()) ::fcgtest::fail("加载关卡失败: " + err, __FILE__, __LINE__);
    return ls;
}

const LevelDef* FindLevel(const std::vector<LevelDef>& ls, const std::string& id) {
    for (auto& l : ls)
        if (l.id == id) return &l;
    return nullptr;
}

ExprP P(const std::string& s) {
    ParseError pe;
    ExprP e = parseExpr(s, &pe);
    if (!e) ::fcgtest::fail("解析失败: " + s + " -> " + pe.message, __FILE__, __LINE__);
    return e;
}

bool Accepts(const std::string& levelId, const std::string& answer) {
    auto ls = LoadAll();
    const LevelDef* lv = FindLevel(ls, levelId);
    if (!lv) ::fcgtest::fail("找不到关卡 " + levelId, __FILE__, __LINE__);
    std::vector<ConstraintReport> reports;
    return checkAllConstraints(P(answer), *lv, &reports);
}

std::string FirstFailure(const std::string& levelId, const std::string& answer) {
    auto ls = LoadAll();
    const LevelDef* lv = FindLevel(ls, levelId);
    std::vector<ConstraintReport> reports;
    checkAllConstraints(P(answer), *lv, &reports);
    for (auto& r : reports)
        if (!r.ok) return r.label + ": " + r.detail;
    return "";
}

}  // namespace

// ---------------------------------------------------------------
// 关卡数据
// ---------------------------------------------------------------
FCG_TEST(integration_levels_load) {
    auto ls = LoadAll();
    CHECK(ls.size() >= 20);
    // 章节覆盖 1..4，id 唯一
    std::vector<std::string> ids;
    int maxChapter = 0;
    for (auto& l : ls) {
        ids.push_back(l.id);
        if (l.chapter > maxChapter) maxChapter = l.chapter;
        CHECK(!l.title.empty());
        CHECK(!l.constraints.empty());
        std::string err;
        CHECK(l.valid(&err));
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::unique(ids.begin(), ids.end()) == ids.end());
    CHECK(maxChapter >= 4);
}

FCG_TEST(integration_all_references_pass) {
    for (auto& l : LoadAll()) {
        std::vector<ConstraintReport> reports;
        ExprP ref = P(l.reference);
        CHECK_TRUE(checkAllConstraints(ref, l, &reports));
        // 参考解评分应当很高
        auto sc = evaluateSubmission(ref, l, 0.0);
        CHECK_TRUE(sc.correct);
        CHECK(sc.total >= 80.0);
    }
}

FCG_TEST(integration_reference_roundtrip) {
    // toStr 打印回环：解析打印结果应与原表达式数值等价
    for (auto& l : LoadAll()) {
        ExprP ref = P(l.reference);
        std::string printed = ref->toStr();
        ExprP again = parseExpr(printed, nullptr);
        CHECK(again != nullptr);
        if (!again) continue;
        for (double x : {-1.7, -0.3, 0.0, 0.9, 2.3}) {
            auto a = evalReal(ref, "x", x);
            auto b = evalReal(again, "x", x);
            if (a.ok != b.ok) {
                ::fcgtest::fail("回环后定义域变化: " + l.reference + " -> " + printed, __FILE__,
                                __LINE__);
            }
            if (a.ok) CHECK_NEAR(a.value, b.value, 1e-9);
        }
        CHECK_TRUE(symbolicallyEqual(ref, again));
    }
}

// ---------------------------------------------------------------
// 判定正确性
// ---------------------------------------------------------------
FCG_TEST(integration_accepts_valid_answers) {
    struct Case {
        const char* level;
        const char* answer;
    };
    const Case cases[] = {
        {"1-1", "cos(x)"},       {"1-1", "1-x*x/2"},      {"1-2", "x"},
        {"1-2", "exp(x)"},       {"2-4", "cos(x)"},       {"2-4", "1-x*x"},
        {"3-1", "1-x*x"},        {"3-3", "x/(1+x*x)"},    {"3-3", "x/(x^2+2)"},
        {"3-4", "exp(-x*x)"},    {"3-4", "1/(1+x^2)"},    {"4-1", "exp(-x*x)"},
        {"4-2", "1/(1+x*x)"},    {"4-6", "exp(-x*x)"},
    };
    for (auto& c : cases) {
        bool ok = Accepts(c.level, c.answer);
        if (!ok)
            ::fcgtest::fail(std::string("应接受却被拒绝: ") + c.level + " <- " + c.answer + " (" +
                                FirstFailure(c.level, c.answer) + ")",
                            __FILE__, __LINE__);
    }
}

FCG_TEST(integration_rejects_invalid_answers) {
    struct Case {
        const char* level;
        const char* answer;
    };
    const Case cases[] = {
        // 偶函数关：奇函数/非偶函数应拒绝
        {"1-1", "x"},        {"1-1", "sin(x)"},
        // 单调关：反向单调应拒绝
        {"1-2", "-x"},       {"1-2", "cos(x)"},
        // 解析性关：abs 非解析
        {"2-4", "abs(x)"},   {"2-6", "abs(x)*x"},
        // 渐近线关：无条件发散
        {"3-3", "x"},        {"3-3", "sin(x)"},
        // 值域/有界关
        {"4-2", "x"},        {"4-2", "x*x"},
        // 允许函数限制：用了未允许的函数
        {"4-6", "cos(x)"},
    };
    for (auto& c : cases) {
        bool ok = Accepts(c.level, c.answer);
        if (ok)
            ::fcgtest::fail(std::string("应拒绝却被接受: ") + c.level + " <- " + c.answer, __FILE__,
                            __LINE__);
    }
}

FCG_TEST(integration_analytic_rejects_nonsmooth) {
    // 找到一个带"解析性"约束的关卡，验证 abs/floor 被拒绝
    auto ls = LoadAll();
    for (auto& l : ls) {
        bool hasAnalytic = false;
        for (auto& c : l.constraints)
            if (c.kind == ConstraintKind::AnalyticOn) hasAnalytic = true;
        if (!hasAnalytic) continue;
        std::vector<ConstraintReport> r1, r2;
        checkAllConstraints(P("abs(x)+" + l.reference), l, &r1);
        // 参考解 + abs 一定非解析
        bool analyticOk = true;
        for (auto& r : r1)
            if (r.kind == ConstraintKind::AnalyticOn) analyticOk = r.ok;
        CHECK_FALSE(analyticOk);
        break;
    }
}

// ---------------------------------------------------------------
// 评分
// ---------------------------------------------------------------
FCG_TEST(integration_score_simpler_is_better) {
    auto ls = LoadAll();
    const LevelDef* lv = FindLevel(ls, "1-1");
    CHECK(lv != nullptr);
    auto a = evaluateSubmission(P("cos(x)"), *lv, 0.0);
    auto b = evaluateSubmission(P("cos(x)+sin(x)*sin(x)*0.5"), *lv, 0.0);
    CHECK_TRUE(a.correct);
    CHECK_TRUE(b.correct);
    CHECK(a.total > b.total);
    CHECK(a.elegance > b.elegance);
}

FCG_TEST(integration_failure_scores_zero) {
    auto ls = LoadAll();
    const LevelDef* lv = FindLevel(ls, "1-1");
    auto sc = evaluateSubmission(P("x"), *lv, 0.0);
    CHECK_FALSE(sc.correct);
    CHECK_NEAR(sc.total, 0.0, 1e-9);
    CHECK_EQ(sc.grade, 'D');
}

FCG_TEST(integration_time_bonus) {
    auto ls = LoadAll();
    LevelDef lv = *FindLevel(ls, "1-1");
    lv.timeLimit = 100.0;
    auto fast = evaluateSubmission(P("cos(x)"), lv, 10.0);   // 剩 90%
    auto slow = evaluateSubmission(P("cos(x)"), lv, 95.0);   // 剩 5%
    CHECK_TRUE(fast.correct);
    CHECK_TRUE(slow.correct);
    CHECK(fast.speed > slow.speed);
    CHECK(fast.total >= slow.total);
    // 不限时的关卡：speed 满分
    lv.timeLimit = 0.0;
    auto free = evaluateSubmission(P("cos(x)"), lv, 9999.0);
    CHECK_NEAR(free.speed, 100.0, 1e-9);
}

// ---------------------------------------------------------------
// 无尽模式
// ---------------------------------------------------------------
FCG_TEST(integration_endless_solvable) {
    for (uint64_t seed : {1ULL, 42ULL, 2024ULL}) {
        for (int i = 1; i <= 60; ++i) {
            LevelDef lv = generateEndlessLevel(i, seed);
            CHECK(!lv.constraints.empty());
            ExprP ref = P(lv.reference);
            std::vector<ConstraintReport> reports;
            bool ok = checkAllConstraints(ref, lv, &reports);
            if (!ok) {
                std::string why;
                for (auto& r : reports)
                    if (!r.ok) why = r.label + ": " + r.detail;
                ::fcgtest::fail("无尽关不可解 seed=" + std::to_string(seed) + " i=" +
                                    std::to_string(i) + " ref=" + lv.reference + " 原因:" + why,
                                __FILE__, __LINE__);
            }
        }
    }
}

FCG_TEST(integration_endless_difficulty_grows) {
    double firstAvg = 0, lastAvg = 0;
    const int n = 20;
    for (int i = 1; i <= n; ++i) {
        firstAvg += static_cast<double>(generateEndlessLevel(i, 7).constraints.size());
        lastAvg += static_cast<double>(generateEndlessLevel(100 + i, 7).constraints.size());
    }
    firstAvg /= n;
    lastAvg /= n;
    CHECK(lastAvg > firstAvg);
}

// ---------------------------------------------------------------
// 性能
// ---------------------------------------------------------------
FCG_TEST(integration_submit_performance) {
    auto ls = LoadAll();
    for (auto& l : ls) {
        ExprP ref = P(l.reference);
        // 预热
        evaluateSubmission(ref, l, 0.0);
        auto t0 = std::chrono::high_resolution_clock::now();
        const int reps = 5;
        for (int i = 0; i < reps; ++i) evaluateSubmission(ref, l, 0.0);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
        // 交互要求：单次提交 < 20ms
        if (ms > 20.0) {
            ::fcgtest::fail("关卡 " + l.id + " 评分过慢: " + std::to_string(ms) + "ms", __FILE__,
                            __LINE__);
        }
    }
}
