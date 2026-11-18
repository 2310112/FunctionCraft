// test_gameplay.cpp — 约束验证 / 评分 / 关卡数据 / 无尽生成的测试
//
// 覆盖范围：
//   * 约束种类元信息与中文描述；
//   * 逐种约束的判定（边界值/奇偶/周期/单调/值域/恒正/零点/极值/渐近线/
//     对称/函数库/复杂度/定义域/解析性）；
//   * 评分公式、评级阈值、硬约束门槛、优雅度/精确度/鲁棒性/速度；
//   * JSON 关卡解析（含表达式字符串数值、开闭区间、错误处理）；
//   * data/levels.json 的 24 关数据（结构、约束覆盖、参考解可解析且真正满足
//     全部约束、参考解得分不低于 parScore）；
//   * 无尽模式生成器（200 关结构、参考解可解、确定性、难度递增、Boss 关）。
// 说明：Domain/Limit/Simplify/Diff 已实现，依赖它们的断言同样按严格标准编写；
//       Constraint.cpp 内保留独立数值兜底，用于引擎无法判定的场合。
#include <cmath>
#include <cstdint>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "test_framework.h"

#include "core/Constraint.h"
#include "core/EndlessGen.h"
#include "core/Level.h"
#include "core/Numeric.h"
#include "core/Parser.h"
#include "core/Scoring.h"

using namespace fcg;

namespace {

constexpr double kPi = 3.14159265358979323846;

ExprP P(const std::string& s) {
    ParseError err;
    ExprP e = parseExpr(s, &err);
    if (!e) ::fcgtest::fail("解析失败: " + s + " -> " + err.message, __FILE__, __LINE__);
    return e;
}

Constraint mkC(ConstraintKind kind) {
    Constraint c;
    c.kind = kind;
    return c;
}

ConstraintReport check(const Constraint& c, const std::string& expr) {
    return checkConstraint(c, P(expr));
}

LevelDef makeLevel(std::vector<Constraint> cs, const std::string& ref = "",
                   double timeLimit = 0.0) {
    LevelDef L;
    L.id = "test";
    L.title = "测试关卡";
    L.chapter = 1;
    L.constraints = std::move(cs);
    L.reference = ref;
    L.timeLimit = timeLimit;
    L.parScore = 85.0;
    return L;
}

// 依次尝试若干相对路径，定位 data/levels.json
std::string findLevelsFile() {
    static const char* candidates[] = {"data/levels.json",
                                       "../data/levels.json",
                                       "cpp/data/levels.json",
                                       "../cpp/data/levels.json",
                                       "build/bin/data/levels.json"};
    for (const char* p : candidates) {
        std::ifstream in(p, std::ios::binary);
        if (in.good()) return p;
    }
    return "";
}

std::vector<LevelDef> loadAllLevels() {
    const std::string path = findLevelsFile();
    if (path.empty()) ::fcgtest::fail("找不到 data/levels.json", __FILE__, __LINE__);
    std::string err;
    std::vector<LevelDef> levels = loadLevels(path, &err);
    if (!err.empty()) {
        ::fcgtest::fail("加载关卡失败(" + path + "): " + err, __FILE__, __LINE__);
    }
    return levels;
}

}  // namespace

// =================================================================
// 1. 约束种类元信息
// =================================================================
FCG_TEST(gameplay_constraint_kind_meta) {
    const ConstraintKind all[] = {
        ConstraintKind::AllowedFunctions, ConstraintKind::DomainOn,
        ConstraintKind::AnalyticOn,       ConstraintKind::BoundaryValue,
        ConstraintKind::RangeOn,          ConstraintKind::PositiveOn,
        ConstraintKind::Monotonic,        ConstraintKind::Parity,
        ConstraintKind::Periodic,         ConstraintKind::Asymptote,
        ConstraintKind::Extremum,         ConstraintKind::SymmetryAxis,
        ConstraintKind::ZeroCount,        ConstraintKind::ComplexityLimit,
        ConstraintKind::PeriodicSymmetry};
    std::set<std::string> names;
    for (ConstraintKind k : all) {
        const std::string name = constraintKindName(k);
        const std::string display = constraintKindDisplay(k);
        CHECK(!name.empty());
        CHECK(!display.empty());
        CHECK(name != "unknown");
        CHECK(display != "未知约束");
        CHECK(constraintKindSupported(k));
        names.insert(name);
    }
    CHECK_EQ(names.size(), size_t(15));
}

// =================================================================
// 2. 中文描述
// =================================================================
FCG_TEST(gameplay_constraint_describe) {
    auto contains = [](const std::string& hay, const std::string& needle) {
        return hay.find(needle) != std::string::npos;
    };

    {
        Constraint c = mkC(ConstraintKind::AllowedFunctions);
        c.allowedOps = {Op::Sin, Op::Cos, Op::Add, Op::Neg};
        const std::string d = c.describe();
        CHECK(contains(d, "只能使用"));
        CHECK(contains(d, "sin"));
        CHECK(contains(d, "cos"));
    }
    {
        Constraint c = mkC(ConstraintKind::DomainOn);
        c.interval = Interval::closed(-kPi, kPi);
        const std::string d = c.describe();
        CHECK(contains(d, "有定义"));
        CHECK(contains(d, "π"));
    }
    {
        Constraint c = mkC(ConstraintKind::AnalyticOn);
        c.interval = Interval::open(-1, 1);
        const std::string d = c.describe();
        CHECK(contains(d, "解析"));
        CHECK(contains(d, "(-1, 1)"));
    }
    {
        Constraint c = mkC(ConstraintKind::BoundaryValue);
        c.point = kPi / 2.0;
        c.value = 1.0;
        CHECK_EQ(c.describe(), std::string("f(π/2) = 1"));
    }
    {
        Constraint c = mkC(ConstraintKind::RangeOn);
        c.interval = Interval::closed(-1, 1);
        c.lo = -1.0;
        c.hi = 1.0;
        const std::string d = c.describe();
        CHECK(contains(d, "值域") || contains(d, "f(x) ∈"));
    }
    {
        Constraint c = mkC(ConstraintKind::PositiveOn);
        c.interval = Interval::open(0, 1);
        CHECK(contains(c.describe(), "> 0"));
    }
    {
        Constraint c = mkC(ConstraintKind::Monotonic);
        c.interval = Interval::closed(0, kPi);
        c.increasing = true;
        c.strict = true;
        const std::string d = c.describe();
        CHECK(contains(d, "严格单调递增"));
        CHECK(contains(d, "π"));
    }
    {
        Constraint c = mkC(ConstraintKind::Parity);
        c.even = true;
        CHECK(contains(c.describe(), "偶函数"));
        c.even = false;
        CHECK(contains(c.describe(), "奇函数"));
    }
    {
        Constraint c = mkC(ConstraintKind::Periodic);
        c.value = 2.0 * kPi;
        const std::string d = c.describe();
        CHECK(contains(d, "周期"));
        CHECK(contains(d, "2π"));
    }
    {
        Constraint c = mkC(ConstraintKind::Asymptote);
        c.value = 0.0;
        c.asymptoteSign = 0;
        const std::string d = c.describe();
        CHECK(contains(d, "水平渐近线"));
        CHECK(contains(d, "0"));
    }
    {
        Constraint c = mkC(ConstraintKind::Extremum);
        c.point = 0.0;
        c.isMax = true;
        CHECK(contains(c.describe(), "极大值"));
        c.isMax = false;
        CHECK(contains(c.describe(), "极小值"));
    }
    {
        Constraint c = mkC(ConstraintKind::SymmetryAxis);
        c.point = 0.0;
        CHECK(contains(c.describe(), "对称"));
    }
    {
        Constraint c = mkC(ConstraintKind::ZeroCount);
        c.interval = Interval::closed(-2, 2);
        c.count = 2;
        const std::string d = c.describe();
        CHECK(contains(d, "零点"));
        CHECK(contains(d, "2"));
    }
    {
        Constraint c = mkC(ConstraintKind::ComplexityLimit);
        c.maxFuncCalls = 3;
        CHECK(contains(c.describe(), "不超过 3"));
    }
    {
        Constraint c = mkC(ConstraintKind::PeriodicSymmetry);
        c.point = 0.0;
        c.value = 1.0;
        CHECK(contains(c.describe(), "中心对称"));
    }
    {
        Constraint c = mkC(ConstraintKind::Parity);
        c.label = "必须左右镜像";
        CHECK_EQ(c.describe(), std::string("必须左右镜像"));
    }
}

// =================================================================
// 3. 约束判定（不依赖其他模块的部分）
// =================================================================
FCG_TEST(gameplay_check_boundary_value) {
    Constraint c = mkC(ConstraintKind::BoundaryValue);
    c.point = 0.0;
    c.value = 1.0;
    ConstraintReport r = check(c, "x + 1");
    CHECK(r.ok);
    CHECK_EQ(static_cast<int>(r.kind), static_cast<int>(ConstraintKind::BoundaryValue));
    CHECK(!r.label.empty());

    c.value = 2.0;
    r = check(c, "x + 1");
    CHECK(!r.ok);
    CHECK(!r.detail.empty());
    CHECK(r.detail.find("1") != std::string::npos);

    // 在 π/2 处：sin(π/2) = 1
    Constraint c2 = mkC(ConstraintKind::BoundaryValue);
    c2.point = kPi / 2.0;
    c2.value = 1.0;
    CHECK(check(c2, "sin(x)").ok);
}

FCG_TEST(gameplay_check_complexity_limit) {
    Constraint c = mkC(ConstraintKind::ComplexityLimit);
    c.maxFuncCalls = 2;
    CHECK(check(c, "sin(x) + cos(2x)").ok);
    c.maxFuncCalls = 1;
    ConstraintReport r = check(c, "sin(x) + cos(2x)");
    CHECK(!r.ok);
    CHECK(r.detail.find("2") != std::string::npos);
    c.maxFuncCalls = 0;
    CHECK(check(c, "x*x + 1").ok);
    CHECK(!check(c, "sin(x)").ok);
}

FCG_TEST(gameplay_check_allowed_functions) {
    Constraint c = mkC(ConstraintKind::AllowedFunctions);
    // sin(2x) 的 2x 会用到隐式乘法，因此 Mul 也要允许
    c.allowedOps = {Op::Sin, Op::Mul, Op::Add};
    CHECK(check(c, "sin(x) + sin(2x)").ok);
    ConstraintReport r = check(c, "sin(x) + cos(x)");
    CHECK(!r.ok);
    CHECK(r.detail.find("cos") != std::string::npos);

    // 负号需要单独允许
    Constraint c2 = mkC(ConstraintKind::AllowedFunctions);
    c2.allowedOps = {Op::Cos};
    CHECK(!check(c2, "-cos(x)").ok);
    c2.allowedOps = {Op::Cos, Op::Neg};
    CHECK(check(c2, "-cos(x)").ok);

    // 空列表 = 不限制
    Constraint c3 = mkC(ConstraintKind::AllowedFunctions);
    CHECK(check(c3, "sin(x) * exp(x)").ok);
}

FCG_TEST(gameplay_check_parity) {
    Constraint even = mkC(ConstraintKind::Parity);
    even.even = true;
    CHECK(check(even, "cos(x)").ok);
    CHECK(check(even, "x*x").ok);
    CHECK(check(even, "1/(1 + x*x)").ok);
    CHECK(!check(even, "sin(x)").ok);
    CHECK(!check(even, "cos(x) + x").ok);

    Constraint odd = mkC(ConstraintKind::Parity);
    odd.even = false;
    CHECK(check(odd, "sin(x)").ok);
    CHECK(check(odd, "x^3").ok);
    CHECK(!check(odd, "cos(x)").ok);
    CHECK(!check(odd, "sin(x) + 1").ok);
}

FCG_TEST(gameplay_check_periodic) {
    Constraint c = mkC(ConstraintKind::Periodic);
    c.value = 2.0 * kPi;
    CHECK(check(c, "cos(x)").ok);
    CHECK(check(c, "sin(x)").ok);
    CHECK(check(c, "sin(x) + cos(2x)").ok);
    c.value = kPi;
    CHECK(check(c, "cos(2x)").ok);
    CHECK(!check(c, "cos(x)").ok);
    c.value = 1.0;
    CHECK(!check(c, "cos(x)").ok);
    // 周期必须为正
    c.value = -1.0;
    CHECK(!check(c, "cos(x)").ok);
}

FCG_TEST(gameplay_check_symmetry) {
    Constraint axis = mkC(ConstraintKind::SymmetryAxis);
    axis.point = 0.0;
    CHECK(check(axis, "cos(x)").ok);
    CHECK(!check(axis, "sin(x)").ok);
    axis.point = kPi / 2.0;
    CHECK(!check(axis, "cos(x)").ok);

    Constraint center = mkC(ConstraintKind::PeriodicSymmetry);
    center.point = 0.0;
    center.value = 0.0;
    CHECK(check(center, "x^3").ok);
    CHECK(check(center, "sin(x)").ok);
    CHECK(!check(center, "x^2").ok);
}

FCG_TEST(gameplay_check_monotonic) {
    Constraint c = mkC(ConstraintKind::Monotonic);
    c.interval = Interval::closed(0, 1);
    c.increasing = true;
    CHECK(check(c, "x").ok);
    CHECK(check(c, "exp(x)").ok);
    c.increasing = false;
    CHECK(!check(c, "x").ok);
    CHECK(check(c, "1 - x").ok);
    c.increasing = true;
    c.interval = Interval::closed(0, kPi / 2.0);
    CHECK(check(c, "sin(x)").ok);
    c.interval = Interval::closed(0, 2.0 * kPi);
    CHECK(!check(c, "sin(x)").ok);  // 先升后降
}

FCG_TEST(gameplay_check_range) {
    Constraint c = mkC(ConstraintKind::RangeOn);
    c.interval = Interval::closed(-kPi / 2.0, kPi / 2.0);
    c.lo = -1.0001;
    c.hi = 1.0001;
    CHECK(check(c, "sin(x)").ok);
    c.lo = -0.5;
    c.hi = 0.5;
    ConstraintReport r = check(c, "sin(x)");
    CHECK(!r.ok);
    CHECK(!r.detail.empty());

    Constraint c2 = mkC(ConstraintKind::RangeOn);
    c2.interval = Interval::closed(-1, 1);
    c2.lo = 0.0;
    c2.hi = 1.0;
    CHECK(check(c2, "x*x").ok);
    c2.hi = 0.5;
    CHECK(!check(c2, "x*x").ok);
}

FCG_TEST(gameplay_check_positive) {
    Constraint c = mkC(ConstraintKind::PositiveOn);
    c.interval = Interval::open(-2, 2);
    CHECK(check(c, "exp(-x*x)").ok);
    CHECK(check(c, "x*x + 1").ok);
    CHECK(!check(c, "x").ok);
    CHECK(!check(c, "x*x").ok);
}

FCG_TEST(gameplay_check_zero_count) {
    Constraint c = mkC(ConstraintKind::ZeroCount);
    c.interval = Interval::closed(-2, 2);
    c.count = 2;
    CHECK(check(c, "cos(x)").ok);  // ±π/2
    c.count = 0;
    CHECK(!check(c, "cos(x)").ok);
    c.count = 1;
    CHECK(check(c, "x^3").ok);
    CHECK(check(c, "x").ok);
    c.count = 0;
    CHECK(check(c, "x*x + 1").ok);
}

FCG_TEST(gameplay_check_extremum) {
    Constraint c = mkC(ConstraintKind::Extremum);
    c.point = 0.0;
    c.isMax = true;
    CHECK(check(c, "1 - x*x").ok);
    CHECK(check(c, "exp(-x*x)").ok);
    CHECK(check(c, "1/(1 + x*x)").ok);
    c.isMax = false;
    CHECK(!check(c, "1 - x*x").ok);
    CHECK(check(c, "x*x").ok);
}

FCG_TEST(gameplay_check_asymptote) {
    Constraint c = mkC(ConstraintKind::Asymptote);
    c.value = 0.0;
    c.asymptoteSign = 0;
    CHECK(check(c, "exp(-x*x)").ok);
    CHECK(check(c, "x/(1 + x*x)").ok);
    CHECK(check(c, "1/(1 + x*x)").ok);

    c.value = 1.0;
    CHECK(!check(c, "exp(-x*x)").ok);

    Constraint right = mkC(ConstraintKind::Asymptote);
    right.value = 1.0;
    right.asymptoteSign = +1;
    CHECK(check(right, "tanh(x)").ok);

    // 双侧极限不同 → 不存在双侧渐近线
    Constraint both = mkC(ConstraintKind::Asymptote);
    both.value = 1.0;
    both.asymptoteSign = 0;
    ConstraintReport r = check(both, "tanh(x)");
    CHECK(!r.ok);
    CHECK(!r.detail.empty());
}

// 定义域 / 解析性判定（依赖 Domain.cpp；集成后已可实现严格断言）
FCG_TEST(gameplay_check_domain_and_analytic) {
    Constraint d = mkC(ConstraintKind::DomainOn);
    d.interval = Interval::closed(-kPi, kPi);
    ConstraintReport r = check(d, "sin(x)");
    CHECK_EQ(static_cast<int>(r.kind), static_cast<int>(ConstraintKind::DomainOn));
    CHECK(!r.label.empty());
    CHECK(r.ok);

    // 1/x 在 0 处无定义：数值复核与定义域分析都应发现
    Constraint d2 = mkC(ConstraintKind::DomainOn);
    d2.interval = Interval::closed(-1, 1);
    ConstraintReport r2 = check(d2, "1/x");
    CHECK(!r2.ok);
    CHECK(!r2.detail.empty());

    // ln(x) 在 (-1, 1) 上不满足"处处有定义"
    Constraint d3 = mkC(ConstraintKind::DomainOn);
    d3.interval = Interval::closed(-1, 1);
    CHECK(!check(d3, "ln(x)").ok);

    Constraint a = mkC(ConstraintKind::AnalyticOn);
    a.interval = Interval::open(-1, 1);
    ConstraintReport r3 = check(a, "sin(x)");
    CHECK_EQ(static_cast<int>(r3.kind), static_cast<int>(ConstraintKind::AnalyticOn));
    CHECK(r3.ok);

    // abs(x) 是非解析函数：结构化检查
    ConstraintReport r4 = check(a, "abs(x)");
    CHECK(!r4.ok);
    CHECK(!r4.detail.empty());
}

// =================================================================
// 4. 评分
// =================================================================
FCG_TEST(gameplay_grade_thresholds) {
    CHECK_EQ(gradeOf(100.0), 'S');
    CHECK_EQ(gradeOf(90.0), 'S');
    CHECK_EQ(gradeOf(89.9), 'A');
    CHECK_EQ(gradeOf(80.0), 'A');
    CHECK_EQ(gradeOf(79.9), 'B');
    CHECK_EQ(gradeOf(70.0), 'B');
    CHECK_EQ(gradeOf(69.9), 'C');
    CHECK_EQ(gradeOf(60.0), 'C');
    CHECK_EQ(gradeOf(59.9), 'D');
    CHECK_EQ(gradeOf(0.0), 'D');
    for (char g : {'S', 'A', 'B', 'C', 'D'}) {
        CHECK(std::string(gradeLabel(g)).size() > 1);
    }
}

FCG_TEST(gameplay_score_formula_and_hard_fail) {
    Constraint b = mkC(ConstraintKind::BoundaryValue);
    b.point = 0.0;
    b.value = 1.0;
    LevelDef L = makeLevel({b}, "cos(x)");
    ExprP f = P("x + 1");

    ScoreBreakdown sb = evaluateSubmission(f, L, 0.0);
    CHECK(sb.correct);
    CHECK(sb.reports.size() == size_t(1));
    CHECK(sb.reports[0].ok);
    CHECK_EQ(sb.nodeCnt, nodeCount(f));
    CHECK_EQ(sb.funcCalls, funcCallCount(f));
    // 各维度在 0..100
    for (double v : {sb.precision, sb.elegance, sb.robustness, sb.speed, sb.total}) {
        CHECK(v >= -1e-9 && v <= 100.0 + 1e-9);
    }
    // 总分公式：total = 0.35p + 0.30e + 0.25r + 0.10s
    const double expect =
        0.35 * sb.precision + 0.30 * sb.elegance + 0.25 * sb.robustness + 0.10 * sb.speed;
    CHECK_NEAR(sb.total, expect, 1e-9);
    CHECK_EQ(sb.grade, gradeOf(sb.total));
    CHECK(!sb.feedback.empty());
    CHECK(!sb.gradeLabel.empty());

    // 硬约束不通过 → total = 0、correct = false、评级 D，且四个维度全部归零
    // （避免 UI 把未通过的提交画成满格横条）
    b.value = 2.0;
    LevelDef bad = makeLevel({b}, "");
    ScoreBreakdown sb2 = evaluateSubmission(P("x + 1"), bad, 0.0);
    CHECK(!sb2.correct);
    CHECK_NEAR(sb2.total, 0.0, 1e-12);
    CHECK_EQ(sb2.grade, 'D');
    CHECK(!sb2.feedback.empty());
    CHECK(!sb2.reports[0].ok);
    CHECK_NEAR(sb2.precision, 0.0, 1e-12);
    CHECK_NEAR(sb2.elegance, 0.0, 1e-12);
    CHECK_NEAR(sb2.robustness, 0.0, 1e-12);
    CHECK_NEAR(sb2.speed, 0.0, 1e-12);
}

FCG_TEST(gameplay_score_empty_expr) {
    Constraint b = mkC(ConstraintKind::BoundaryValue);
    LevelDef L = makeLevel({b});
    ScoreBreakdown sb = evaluateSubmission(nullptr, L, 0.0);
    CHECK(!sb.correct);
    CHECK_NEAR(sb.total, 0.0, 1e-12);
    CHECK(!sb.feedback.empty());
    CHECK(sb.reports.size() == size_t(1));
}

FCG_TEST(gameplay_score_speed) {
    Constraint b = mkC(ConstraintKind::BoundaryValue);
    b.point = 0.0;
    b.value = 1.0;
    LevelDef L = makeLevel({b}, "cos(x)", 100.0);
    const ExprP f = P("x + 1");
    CHECK_NEAR(evaluateSubmission(f, L, 0.0).speed, 100.0, 1e-9);
    CHECK_NEAR(evaluateSubmission(f, L, 50.0).speed, 50.0, 1e-9);
    CHECK_NEAR(evaluateSubmission(f, L, 100.0).speed, 0.0, 1e-9);
    CHECK_NEAR(evaluateSubmission(f, L, 250.0).speed, 0.0, 1e-9);  // 钳制在 0

    // 不限时 → 速度项满分
    LevelDef unlimited = makeLevel({b});
    CHECK_NEAR(evaluateSubmission(f, unlimited, 9999.0).speed, 100.0, 1e-9);
}

FCG_TEST(gameplay_score_elegance_ordering) {
    Constraint lim = mkC(ConstraintKind::ComplexityLimit);
    lim.maxFuncCalls = 100;
    LevelDef L = makeLevel({lim}, "sin(x)");
    const ScoreBreakdown simple = evaluateSubmission(P("sin(x)"), L, 0.0);
    const ScoreBreakdown complex =
        evaluateSubmission(P("sin(x) + cos(2x) + sin(3x) + cos(4x)"), L, 0.0);
    CHECK(simple.correct);
    CHECK(complex.correct);
    CHECK(simple.funcCalls < complex.funcCalls);
    CHECK(simple.elegance >= complex.elegance);
    CHECK(simple.total >= complex.total);  // 其他维度相同，更优雅的解总分不应更低
    CHECK_NEAR(simple.elegance, 100.0, 1e-9);  // 与参考解相同 → 满分

    // 与参考解比较：参考解更复杂时，简单的解也应拿到满分
    LevelDef L2 = makeLevel({lim}, "sin(x) + cos(x) + sin(2x)");
    const ScoreBreakdown sb = evaluateSubmission(P("sin(x)"), L2, 0.0);
    CHECK_NEAR(sb.elegance, 100.0, 1e-9);
}

// 优雅度抗"填充"测试：复杂度基于原始 AST，×0 填充、等价展开、堆函数调用
// 都必须被如实扣分；且正确解的总分不得因此掉到 C/D。
FCG_TEST(gameplay_score_elegance_anti_padding) {
    // 关卡 1-1 的真实约束：ref = cos(x)（1 次函数调用 / 2 节点 / 深度 2）
    Constraint parity = mkC(ConstraintKind::Parity);
    parity.even = true;
    Constraint bound = mkC(ConstraintKind::BoundaryValue);
    bound.point = 0.0;
    bound.value = 1.0;
    LevelDef L = makeLevel({parity, bound}, "cos(x)");

    struct CurveCase {
        const char* expr;
        double lo;
        double hi;
    };
    const CurveCase cases[] = {
        {"cos(x)", 99.9, 100.0},               // 与参考解相同 → 100
        {"x^2*0+cos(x)", 40.0, 60.0},          // ×0 填充
        {"abs(x)*0+cos(x)", 40.0, 55.0},       // ×0 填充 + 多余函数调用
        {"1-x*x/2+x^4/24", 30.0, 45.0},        // 等价但笨重的展开式
        {"cos(x)+sin(x)*sin(x)", 30.0, 42.0},  // 多余的函数调用
    };
    for (const CurveCase& c : cases) {
        const ScoreBreakdown sb = evaluateSubmission(P(c.expr), L, 0.0);
        if (!sb.correct) {
            ::fcgtest::fail(std::string("该表达式应满足 1-1 的约束：") + c.expr, __FILE__,
                            __LINE__);
        }
        if (sb.elegance < c.lo || sb.elegance > c.hi) {
            ::fcgtest::fail(std::string("elegance 不在预期区间：") + c.expr + " -> " +
                                ::fcgtest::fmt(sb.elegance) + " 不在 [" +
                                ::fcgtest::fmt(c.lo) + ", " + ::fcgtest::fmt(c.hi) + "]",
                            __FILE__, __LINE__);
        }
        // 做对了但写得啰嗦：总分至少 C/B 区间，不能掉到 D
        CHECK(sb.total >= 70.0);
        CHECK(sb.grade == 'S' || sb.grade == 'A' || sb.grade == 'B');
    }

    // 比参考解更简洁 → 仍满分
    const ScoreBreakdown simpler = evaluateSubmission(P("1"), L, 0.0);
    CHECK(simpler.correct);
    CHECK_NEAR(simpler.elegance, 100.0, 1e-9);
    CHECK_NEAR(simpler.total, 100.0, 1e-9);

    // 极度堆砌的正确解：elegance 有下界（>=25），不会掉到 D 级
    const ScoreBreakdown padded = evaluateSubmission(
        P("cos(x)+x^2*0+x^4*0+x^6*0+x^8*0+x^10*0+x^12*0"), L, 0.0);
    CHECK(padded.correct);
    CHECK(padded.elegance >= 25.0 - 1e-9);
    CHECK(padded.total >= 70.0);
    CHECK(padded.grade != 'D');
}

FCG_TEST(gameplay_score_precision) {
    Constraint b = mkC(ConstraintKind::BoundaryValue);
    b.point = 0.0;
    b.value = 1.0;
    LevelDef L = makeLevel({b}, "cos(x)");
    const ScoreBreakdown exact = evaluateSubmission(P("x + 1"), L, 0.0);
    CHECK(exact.correct);
    CHECK_NEAR(exact.precision, 100.0, 1e-9);

    // 不通过硬约束时：各维度统一置 0（UI 不再画满格横条），total=0
    LevelDef L2 = makeLevel({b}, "cos(x)");
    b.value = 1.001;
    L2.constraints = {b};
    const ScoreBreakdown off = evaluateSubmission(P("x + 1"), L2, 0.0);
    CHECK(!off.correct);
    CHECK_NEAR(off.total, 0.0, 1e-12);
    CHECK_NEAR(off.precision, 0.0, 1e-12);
    CHECK_NEAR(off.elegance, 0.0, 1e-12);
    CHECK_NEAR(off.robustness, 0.0, 1e-12);
}

FCG_TEST(gameplay_score_robustness) {
    Constraint d = mkC(ConstraintKind::DomainOn);
    d.interval = Interval::closed(0, 1);
    LevelDef L = makeLevel({d}, "");
    // 全局有定义的函数：边界内外都可求值
    const ScoreBreakdown good = evaluateSubmission(P("sin(x)"), L, 0.0);
    CHECK(good.correct);
    CHECK_NEAR(good.robustness, 100.0, 1e-9);
    // 只在 [0,1] 有定义的函数：外扩探针失败，鲁棒性应下降
    const ScoreBreakdown limited = evaluateSubmission(P("sqrt(x)"), L, 0.0);
    CHECK(limited.correct);
    CHECK(limited.robustness < 100.0);
}

FCG_TEST(gameplay_score_robustness_range_boundary) {
    // 值域约束只要求区间内成立；区间外取值越界不应被判为"不鲁棒"
    Constraint r = mkC(ConstraintKind::RangeOn);
    r.interval = Interval::closed(0, kPi);
    r.lo = 0.0;
    r.hi = 1.0;
    LevelDef L = makeLevel({r}, "sin(x)");
    const ScoreBreakdown sb = evaluateSubmission(P("sin(x)"), L, 0.0);
    CHECK(sb.correct);
    CHECK_NEAR(sb.robustness, 100.0, 1e-9);
    CHECK_NEAR(sb.total, 100.0, 1e-9);
}

// =================================================================
// 5. JSON 关卡解析
// =================================================================
FCG_TEST(gameplay_level_from_json_manual) {
    const char* text = R"json({
      "id": "t-1",
      "title": "手写测试关",
      "chapter": 2,
      "hint": "提示文本",
      "reference": "sin(x)",
      "parScore": 88,
      "time_limit": 120,
      "constraints": [
        {"type": "domain", "interval": ["-pi", "pi"]},
        {"type": "analytic", "interval": {"lo": -1, "hi": 1, "open": true}},
        {"type": "boundary", "point": "pi/2", "value": 1},
        {"type": "range", "interval": [0, "pi"], "bounds": [0, 1]},
        {"type": "monotonic", "interval": [0, "pi"], "direction": "decreasing"},
        {"type": "parity", "parity": "odd"},
        {"type": "periodic", "period": "2*pi"},
        {"type": "asymptote", "value": 0, "side": "neg"},
        {"type": "extremum", "point": 1, "extremum": "min"},
        {"type": "symmetry_axis", "axis": 0},
        {"type": "zero_count", "interval": [-2, 2], "count": 1},
        {"type": "complexity_limit", "max_func_calls": 5},
        {"type": "allowed_functions", "allowed": ["sin", "cos", "+", "-", "*", "/", "^"]},
        {"type": "point_symmetry", "point": 0, "value": 0}
      ]
    })json";
    std::string err;
    const Json j = Json::parse(text, &err);
    CHECK(err.empty());
    const LevelDef L = levelFromJson(j, &err);
    CHECK(err.empty());
    CHECK_EQ(L.id, std::string("t-1"));
    CHECK_EQ(L.title, std::string("手写测试关"));
    CHECK_EQ(L.chapter, 2);
    CHECK_EQ(L.hint, std::string("提示文本"));
    CHECK_NEAR(L.timeLimit, 120.0, 1e-12);
    CHECK_NEAR(L.parScore, 88.0, 1e-12);
    CHECK_EQ(L.constraints.size(), size_t(14));

    CHECK_EQ(static_cast<int>(L.constraints[0].kind), static_cast<int>(ConstraintKind::DomainOn));
    CHECK(!L.constraints[0].interval.loInf);
    CHECK_NEAR(L.constraints[0].interval.lo, -kPi, 1e-12);
    CHECK_NEAR(L.constraints[0].interval.hi, kPi, 1e-12);

    CHECK_EQ(static_cast<int>(L.constraints[1].kind), static_cast<int>(ConstraintKind::AnalyticOn));
    CHECK(!L.constraints[1].interval.loClosed);
    CHECK(!L.constraints[1].interval.hiClosed);

    CHECK_EQ(static_cast<int>(L.constraints[2].kind), static_cast<int>(ConstraintKind::BoundaryValue));
    CHECK_NEAR(L.constraints[2].point, kPi / 2.0, 1e-12);
    CHECK_NEAR(L.constraints[2].value, 1.0, 1e-12);

    CHECK_EQ(static_cast<int>(L.constraints[3].kind), static_cast<int>(ConstraintKind::RangeOn));
    CHECK_NEAR(L.constraints[3].lo, 0.0, 1e-12);
    CHECK_NEAR(L.constraints[3].hi, 1.0, 1e-12);

    CHECK_EQ(static_cast<int>(L.constraints[4].kind), static_cast<int>(ConstraintKind::Monotonic));
    CHECK(!L.constraints[4].increasing);

    CHECK_EQ(static_cast<int>(L.constraints[5].kind), static_cast<int>(ConstraintKind::Parity));
    CHECK(!L.constraints[5].even);

    CHECK_EQ(static_cast<int>(L.constraints[6].kind), static_cast<int>(ConstraintKind::Periodic));
    CHECK_NEAR(L.constraints[6].value, 2.0 * kPi, 1e-12);

    CHECK_EQ(static_cast<int>(L.constraints[7].kind), static_cast<int>(ConstraintKind::Asymptote));
    CHECK_EQ(L.constraints[7].asymptoteSign, -1);

    CHECK_EQ(static_cast<int>(L.constraints[8].kind), static_cast<int>(ConstraintKind::Extremum));
    CHECK(!L.constraints[8].isMax);

    CHECK_EQ(static_cast<int>(L.constraints[9].kind), static_cast<int>(ConstraintKind::SymmetryAxis));
    CHECK_NEAR(L.constraints[9].point, 0.0, 1e-12);

    CHECK_EQ(static_cast<int>(L.constraints[10].kind), static_cast<int>(ConstraintKind::ZeroCount));
    CHECK_EQ(L.constraints[10].count, 1);

    CHECK_EQ(static_cast<int>(L.constraints[11].kind), static_cast<int>(ConstraintKind::ComplexityLimit));
    CHECK_EQ(L.constraints[11].maxFuncCalls, 5);

    CHECK_EQ(static_cast<int>(L.constraints[12].kind), static_cast<int>(ConstraintKind::AllowedFunctions));
    CHECK(!L.constraints[12].allowedOps.empty());

    CHECK_EQ(static_cast<int>(L.constraints[13].kind), static_cast<int>(ConstraintKind::PeriodicSymmetry));
}

FCG_TEST(gameplay_level_from_json_errors) {
    std::string err;
    // 未知类型
    {
        Json j = Json::parse(R"({"id":"x","constraints":[{"type":"no_such"}]})", &err);
        CHECK(err.empty());
        std::string e2;
        levelFromJson(j, &e2);
        CHECK(!e2.empty());
    }
    // 缺少约束
    {
        Json j = Json::parse(R"({"id":"x"})", &err);
        std::string e2;
        levelFromJson(j, &e2);
        CHECK(!e2.empty());
    }
    // 区间倒置
    {
        Json j = Json::parse(
            R"({"id":"x","constraints":[{"type":"domain","interval":[2,1]}]})", &err);
        std::string e2;
        levelFromJson(j, &e2);
        CHECK(!e2.empty());
    }
    // value 表达式非法
    {
        Json j = Json::parse(
            R"({"id":"x","constraints":[{"type":"boundary","point":0,"value":"sin("}]})",
            &err);
        std::string e2;
        levelFromJson(j, &e2);
        CHECK(!e2.empty());
    }
    // value 含变量
    {
        Json j = Json::parse(
            R"({"id":"x","constraints":[{"type":"boundary","point":0,"value":"x+1"}]})",
            &err);
        std::string e2;
        levelFromJson(j, &e2);
        CHECK(!e2.empty());
    }
    // 未知函数名
    {
        Json j = Json::parse(
            R"({"id":"x","constraints":[{"type":"allowed_functions","allowed":["nope"]}]})",
            &err);
        std::string e2;
        levelFromJson(j, &e2);
        CHECK(!e2.empty());
    }
    // 周期为负
    {
        Json j = Json::parse(
            R"({"id":"x","constraints":[{"type":"periodic","period":-1}]})", &err);
        std::string e2;
        levelFromJson(j, &e2);
        CHECK(!e2.empty());
    }
    // 参考解无法解析 → valid() 报错
    {
        Json j = Json::parse(
            R"({"id":"x","reference":"sin(","constraints":[{"type":"boundary","point":0,"value":0}]})",
            &err);
        std::string e2;
        levelFromJson(j, &e2);
        CHECK(!e2.empty());
    }
    // 不存在的文件
    {
        std::string e2;
        std::vector<LevelDef> none = loadLevels("no_such_levels_file.json", &e2);
        CHECK(none.empty());
        CHECK(!e2.empty());
    }
}

FCG_TEST(gameplay_level_valid_rules) {
    LevelDef L;
    std::string err;
    CHECK(!L.valid(&err));  // id 为空
    L.id = "v-1";
    CHECK(!L.valid(&err));  // 无约束
    Constraint b = mkC(ConstraintKind::BoundaryValue);
    b.point = 0.0;
    b.value = 1.0;
    L.constraints = {b};
    CHECK(L.valid(&err));
    CHECK(err.empty());

    Constraint badRange = mkC(ConstraintKind::RangeOn);
    badRange.interval = Interval::closed(0, 1);
    badRange.lo = 2.0;
    badRange.hi = 1.0;
    LevelDef L2 = L;
    L2.constraints = {badRange};
    CHECK(!L2.valid(&err));

    Constraint badPeriod = mkC(ConstraintKind::Periodic);
    badPeriod.value = 0.0;
    LevelDef L3 = L;
    L3.constraints = {badPeriod};
    CHECK(!L3.valid(&err));

    LevelDef L4 = L;
    L4.reference = "sin(";
    CHECK(!L4.valid(&err));
}

// =================================================================
// 6. data/levels.json 内容
// =================================================================
FCG_TEST(gameplay_levels_json_load) {
    const std::vector<LevelDef> levels = loadAllLevels();
    CHECK_EQ(levels.size(), size_t(24));

    std::set<std::string> ids;
    std::set<int> chapters;
    int perChapter[5] = {0, 0, 0, 0, 0};
    std::set<int> kinds;
    for (const LevelDef& L : levels) {
        CHECK(!L.id.empty());
        CHECK(!L.title.empty());
        CHECK(!L.hint.empty());
        CHECK(L.chapter >= 1 && L.chapter <= 4);
        CHECK(!L.reference.empty());
        CHECK(L.parScore >= 0.0 && L.parScore <= 100.0);
        CHECK(ids.insert(L.id).second);  // id 唯一
        chapters.insert(L.chapter);
        ++perChapter[L.chapter];
        const int n = static_cast<int>(L.constraints.size());
        CHECK(n >= 1 && n <= 6);
        if (L.chapter == 1) CHECK(n >= 1 && n <= 2);
        if (L.chapter == 2) CHECK(n >= 3 && n <= 4);
        if (L.chapter == 3) CHECK(n >= 4 && n <= 5);
        if (L.chapter == 4) CHECK(n >= 5 && n <= 6);
        for (const Constraint& c : L.constraints) {
            kinds.insert(static_cast<int>(c.kind));
            CHECK(constraintKindSupported(c.kind));
        }
        // 参考解必须能被解析
        ParseError pe;
        CHECK(parseExpr(L.reference, &pe) != nullptr);
    }
    CHECK_EQ(chapters.size(), size_t(4));
    for (int ch = 1; ch <= 4; ++ch) CHECK_EQ(perChapter[ch], 6);
    // 约束种类覆盖：15 种全部出现
    CHECK_EQ(kinds.size(), size_t(15));
}

// 参考解必须真正满足本关全部约束（桩阶段依靠 Constraint.cpp 内的数值兜底；
// 集成后若失败，说明依赖模块与兜底算法不一致，需要 lead 复核）
FCG_TEST(gameplay_levels_reference_satisfies) {
    const std::vector<LevelDef> levels = loadAllLevels();
    std::string firstFail;
    int failCount = 0;
    for (const LevelDef& L : levels) {
        ParseError pe;
        ExprP ref = parseExpr(L.reference, &pe);
        CHECK(ref != nullptr);
        std::vector<ConstraintReport> reports;
        const bool ok = checkAllConstraints(ref, L, &reports);
        if (!ok) {
            ++failCount;
            if (firstFail.empty()) {
                for (const ConstraintReport& r : reports) {
                    if (r.ok) continue;
                    firstFail = L.id + " 的参考解不满足「" + r.label + "」：" + r.detail;
                    break;
                }
            }
        }
    }
    if (failCount != 0) {
        ::fcgtest::fail("有 " + std::to_string(failCount) + " 关的参考解未通过：" + firstFail,
                        __FILE__, __LINE__);
    }
    CHECK_EQ(failCount, 0);
}

FCG_TEST(gameplay_levels_reference_scores_above_par) {
    const std::vector<LevelDef> levels = loadAllLevels();
    std::string firstBad;
    for (const LevelDef& L : levels) {
        ExprP ref = P(L.reference);
        const ScoreBreakdown sb = evaluateSubmission(ref, L, 0.0);
        if (!sb.correct) {
            if (firstBad.empty()) firstBad = L.id + " 参考解评分未通过约束";
            continue;
        }
        if (sb.total + 1e-9 < L.parScore && firstBad.empty()) {
            firstBad = L.id + " 参考解得分 " + ::fcgtest::fmt(sb.total) + " 低于 parScore " +
                       ::fcgtest::fmt(L.parScore);
        }
    }
    CHECK(firstBad.empty());
}

// =================================================================
// 7. 无尽模式生成器
// =================================================================
FCG_TEST(gameplay_endless_structure) {
    for (int i = 1; i <= 200; ++i) {
        const LevelDef L = generateEndlessLevel(i, 12345);
        CHECK(!L.id.empty());
        CHECK(!L.title.empty());
        CHECK(!L.hint.empty());
        CHECK(L.chapter >= 1 && L.chapter <= 4);
        const int n = static_cast<int>(L.constraints.size());
        CHECK(n >= 2);
        CHECK(n <= 6);
        std::string err;
        CHECK(L.valid(&err));
        ParseError pe;
        CHECK(parseExpr(L.reference, &pe) != nullptr);
        for (const Constraint& c : L.constraints) {
            CHECK(constraintKindSupported(c.kind));
        }
        CHECK_EQ(isBossLevel(i), i % 10 == 0);
        CHECK(!difficultyLabel(i).empty());
    }
    CHECK(isBossLevel(10));
    CHECK(isBossLevel(200));
    CHECK(!isBossLevel(11));
    CHECK(!isBossLevel(0));
    CHECK(!isBossLevel(-5));
    CHECK(difficultyLabel(3).find("基础") != std::string::npos);
    CHECK(difficultyLabel(25).find("组合") != std::string::npos);
    CHECK(difficultyLabel(45).find("进阶") != std::string::npos);
    CHECK(difficultyLabel(80).find("大师") != std::string::npos);
    CHECK(difficultyLabel(10).find("Boss") != std::string::npos);
}

// 生成器自检：每一关的参考解都必须通过本关全部约束
FCG_TEST(gameplay_endless_reference_solvable) {
    std::string err;
    if (!selfTestGenerator(200, 2024, &err)) {
        ::fcgtest::fail("selfTestGenerator 失败：" + err, __FILE__, __LINE__);
    }
    CHECK(err.empty());
    // 200 关都覆盖到：生成 + 用参考解直接验证
    for (int i = 1; i <= 200; ++i) {
        const LevelDef L = generateEndlessLevel(i, 777);
        ExprP ref = P(L.reference);
        std::vector<ConstraintReport> reports;
        const bool ok = checkAllConstraints(ref, L, &reports);
        if (!ok) {
            std::string msg = "第 " + std::to_string(i) + " 关参考解未通过";
            for (const ConstraintReport& r : reports) {
                if (!r.ok) {
                    msg += "「" + r.label + "」" + r.detail;
                    break;
                }
            }
            ::fcgtest::fail(msg, __FILE__, __LINE__);
        }
    }
}

FCG_TEST(gameplay_endless_deterministic) {
    const LevelDef a = generateEndlessLevel(37, 999);
    const LevelDef b = generateEndlessLevel(37, 999);
    CHECK_EQ(a.reference, b.reference);
    CHECK_EQ(a.constraints.size(), b.constraints.size());
    CHECK_EQ(a.title, b.title);

    // 不同种子至少产生一部分不同的关卡
    bool anyDifferent = false;
    for (int i = 1; i <= 40; ++i) {
        const LevelDef x = generateEndlessLevel(i, 1);
        const LevelDef y = generateEndlessLevel(i, 2);
        if (x.reference != y.reference || x.constraints.size() != y.constraints.size()) {
            anyDifferent = true;
            break;
        }
    }
    CHECK(anyDifferent);
}

FCG_TEST(gameplay_endless_difficulty_progress) {
    // 难度曲线：约束数量总体随 index 增加
    auto avgCount = [](int from, int to, uint64_t seed) {
        int sum = 0;
        int n = 0;
        for (int i = from; i <= to; ++i) {
            sum += static_cast<int>(generateEndlessLevel(i, seed).constraints.size());
            ++n;
        }
        return static_cast<double>(sum) / static_cast<double>(n);
    };
    const double early = avgCount(1, 10, 42);
    const double late = avgCount(101, 140, 42);
    CHECK(early >= 2.0);
    CHECK(late > early);
    CHECK(late <= 6.0);

    // Boss 关一定带复杂度上限约束
    bool bossHasComplexity = true;
    for (int i = 10; i <= 200; i += 10) {
        const LevelDef L = generateEndlessLevel(i, 2024);
        bool found = false;
        for (const Constraint& c : L.constraints) {
            if (c.kind == ConstraintKind::ComplexityLimit) found = true;
        }
        if (!found) bossHasComplexity = false;
    }
    CHECK(bossHasComplexity);
}

FCG_TEST(gameplay_endless_generated_levels_playable) {
    // 生成关卡也必须能正常评分：参考解拿高分、乱写表达式不会崩溃
    for (int i = 1; i <= 20; ++i) {
        const LevelDef L = generateEndlessLevel(i, 555);
        ExprP ref = P(L.reference);
        const ScoreBreakdown good = evaluateSubmission(ref, L, 0.0);
        if (!good.correct) {
            std::string msg = "第 " + std::to_string(i) + " 关（" + L.title + "）参考解评分不通过";
            for (const ConstraintReport& r : good.reports) {
                if (!r.ok) {
                    msg += "；「" + r.label + "」" + r.detail;
                    break;
                }
            }
            ::fcgtest::fail(msg, __FILE__, __LINE__);
        }
        CHECK(good.total >= 80.0);
        const ScoreBreakdown bad = evaluateSubmission(P("x"), L, 0.0);
        CHECK(!bad.correct || bad.total >= 0.0);  // 不崩溃即可
        CHECK(bad.total <= 100.0);
    }
}
