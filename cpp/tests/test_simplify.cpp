// test_simplify.cpp — 符号化简 / 符号等价 / 符号求导 测试（engine-simplify）
// 覆盖：基本代数规则、域安全反例、规范键、性能保护、以及全部 Op 的求导正确性
//       （与数值中心差分对比，容差 1e-7）。
#include "test_framework.h"

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include "core/Diff.h"
#include "core/Expr.h"
#include "core/Numeric.h"
#include "core/Parser.h"
#include "core/Simplify.h"

using namespace fcg;

namespace {

ExprP P(const std::string& s) {
    ParseError err;
    ExprP e = parseExpr(s, &err);
    if (!e) {
        ::fcgtest::fail("解析失败: " + s + " -> " + err.message, __FILE__, __LINE__);
    }
    return e;
}

double V(const ExprP& e, double x) {
    auto r = evalReal(e, "x", x);
    if (!r.ok) {
        ::fcgtest::fail("求值失败: " + e->toStr() + " @ x=" + ::fcgtest::fmt(x) + " -> " + r.error,
                        __FILE__, __LINE__);
    }
    return r.value;
}

double VVar(const ExprP& e, const std::string& var, double x) {
    auto r = evalReal(e, var, x);
    if (!r.ok) {
        ::fcgtest::fail("求值失败: " + e->toStr() + " @ " + var + "=" + ::fcgtest::fmt(x) + " -> " +
                            r.error,
                        __FILE__, __LINE__);
    }
    return r.value;
}

bool isNumNode(const ExprP& e) { return e && e->op == Op::Num; }

// 两个表达式（化简后）数值一致
void checkValueEqual(const ExprP& a, const ExprP& b, const std::vector<double>& xs,
                     double tol = 1e-9) {
    for (double x : xs) {
        double va = V(a, x);
        double vb = V(b, x);
        if (std::fabs(va - vb) > tol * (1.0 + std::fabs(va) + std::fabs(vb))) {
            ::fcgtest::fail("数值不等: " + a->toStr() + " vs " + b->toStr() + " @ x=" +
                                ::fcgtest::fmt(x) + " (" + ::fcgtest::fmt(va) + " vs " +
                                ::fcgtest::fmt(vb) + ")",
                            __FILE__, __LINE__);
        }
    }
}

void checkSimplifiedTo(const std::string& src, const std::string& expected) {
    ExprP got = simplify(P(src));
    ExprP want = simplify(P(expected));
    if (!structurallyEqual(got, want)) {
        ::fcgtest::fail("化简结果不符: " + src + " -> " + got->toStr() + " ，期望与 " + expected +
                            "（" + want->toStr() + "）同构",
                        __FILE__, __LINE__);
    }
}

void checkCanonicalEqual(const std::string& a, const std::string& b) {
    std::string ka = canonicalKey(simplify(P(a)));
    std::string kb = canonicalKey(simplify(P(b)));
    if (ka != kb) {
        ::fcgtest::fail("规范键不等: " + a + " (" + ka + ") vs " + b + " (" + kb + ")", __FILE__,
                        __LINE__);
    }
}

void checkConstValue(const std::string& src, double expected) {
    ExprP e = simplify(P(src));
    if (!isNumNode(e)) {
        ::fcgtest::fail("常量折叠失败: " + src + " -> " + e->toStr(), __FILE__, __LINE__);
    }
    if (std::fabs(e->num - expected) > 1e-12) {
        ::fcgtest::fail("常量折叠值错: " + src + " = " + ::fcgtest::fmt(e->num) + "，期望 " +
                            ::fcgtest::fmt(expected),
                        __FILE__, __LINE__);
    }
}

// 数值中心差分
double centralDiff(const ExprP& f, double x, double h = 1e-5) {
    return (V(f, x + h) - V(f, x - h)) / (2.0 * h);
}

// 符号导数与数值中心差分对比（容差 1e-7，含相对误差）
void checkDerivative(const std::string& src, const std::vector<double>& xs, double tol = 1e-7) {
    ExprP f = P(src);
    ExprP d = diff(f, "x");
    for (double x : xs) {
        double exact = V(d, x);
        double approx = centralDiff(f, x);
        double err = std::fabs(exact - approx);
        double scale = 1.0 + std::fabs(approx);
        if (err > tol * scale) {
            ::fcgtest::fail("导数不符: d/dx " + src + " = " + d->toStr() + " @ x=" +
                                ::fcgtest::fmt(x) + " 符号=" + ::fcgtest::fmt(exact) +
                                " 数值=" + ::fcgtest::fmt(approx),
                            __FILE__, __LINE__);
        }
    }
}

}  // namespace

// =======================================================================
// 一、化简：基本规则
// =======================================================================

FCG_TEST(simplify_const_fold) {
    checkConstValue("2 + 3*4", 14.0);
    checkConstValue("(1+2)*(3+4)", 21.0);
    checkConstValue("2^10", 1024.0);
    checkConstValue("-(-5)", 5.0);
    checkConstValue("6/3", 2.0);
    checkConstValue("2 + 3 - 5", 0.0);
    checkConstValue("10 - 2*3", 4.0);
    // 不可精确表示的常量不必折叠，但数值应一致
    checkValueEqual(simplify(P("1/3")), P("1/3"), {0.0});
}

FCG_TEST(simplify_add_identity) {
    checkSimplifiedTo("x + 0", "x");
    checkSimplifiedTo("0 + x", "x");
    checkSimplifiedTo("x - 0", "x");
    checkSimplifiedTo("0 - x", "-x");
    checkSimplifiedTo("x + 0 + 0", "x");
    checkSimplifiedTo("x - x", "0");
    checkSimplifiedTo("-x + x", "0");
}

FCG_TEST(simplify_mul_identity) {
    checkSimplifiedTo("x*1", "x");
    checkSimplifiedTo("1*x", "x");
    checkSimplifiedTo("x*0", "0");
    checkSimplifiedTo("0*sin(x)", "0");
    checkSimplifiedTo("-1*x", "-x");
    checkSimplifiedTo("x*(-1)", "-x");
    checkSimplifiedTo("1*1*x", "x");
}

FCG_TEST(simplify_like_terms) {
    checkSimplifiedTo("x + x", "2x");
    checkSimplifiedTo("x + x + x", "3x");
    checkSimplifiedTo("3x + 2x", "5x");
    checkSimplifiedTo("2x - 2x + 1", "1");
    checkSimplifiedTo("sin(x) + sin(x)", "2sin(x)");
    checkSimplifiedTo("x/2 + x/2", "x");
    checkSimplifiedTo("2(x+1) - 2x", "2");
}

FCG_TEST(simplify_pow_rules) {
    checkSimplifiedTo("x^1", "x");
    checkSimplifiedTo("x^0", "1");
    checkSimplifiedTo("1^x", "1");
    checkSimplifiedTo("x * x", "x^2");
    checkSimplifiedTo("x^2 * x^3", "x^5");
    checkSimplifiedTo("(x^2)^3", "x^6");
    checkSimplifiedTo("0^3", "0");
    checkSimplifiedTo("x^2 * x * x^3", "x^6");
}

FCG_TEST(simplify_div_rules) {
    checkSimplifiedTo("x/1", "x");
    checkSimplifiedTo("6/3", "2");
    checkSimplifiedTo("0/x", "0");
    checkSimplifiedTo("x/(-1)", "-x");
    checkSimplifiedTo("exp(x)/exp(x)", "1");
    checkSimplifiedTo("(-x)/(-y)", "x/y");
}

FCG_TEST(simplify_division_cancel_nonzero) {
    // 只有恒非零因子才能约去（域安全）
    checkSimplifiedTo("(x^2 + 1)/(x^2 + 1)", "1");
    checkSimplifiedTo("exp(x)*x/exp(x)", "x");
    checkSimplifiedTo("pi/pi", "1");
}

// =======================================================================
// 二、化简：域安全反例（不能扩大定义域）
// =======================================================================

FCG_TEST(simplify_domain_safe_sqrt_square) {
    checkSimplifiedTo("sqrt(x^2)", "abs(x)");
    checkSimplifiedTo("sqrt(x*x)", "abs(x)");
    checkSimplifiedTo("sqrt(4)", "2");
    // 不可化简为 x
    CHECK(!structurallyEqual(simplify(P("sqrt(x^2)")), simplify(P("x"))));
    // sqrt(x)^2 在 x<0 处无定义，不能化为 x
    ExprP sq = simplify(P("sqrt(x)^2"));
    CHECK(!structurallyEqual(sq, simplify(P("x"))));
    CHECK(sq->op == Op::Pow);
}

FCG_TEST(simplify_domain_safe_fraction) {
    // x/x 不能化为 1（丢失 x≠0）
    ExprP e = simplify(P("x/x"));
    CHECK(!isNumNode(e));
    CHECK(e->op == Op::Div);
    CHECK(!structurallyEqual(e, simplify(P("1"))));
    // x^2/x 同样不能随意约分
    CHECK(!structurallyEqual(simplify(P("x^2/x")), simplify(P("x"))));
}

FCG_TEST(simplify_domain_safe_exp_ln) {
    checkSimplifiedTo("ln(exp(x))", "x");
    checkSimplifiedTo("ln(exp(2x))", "2x");
    // exp(ln(x)) 只在 x 恒正时才能化简
    ExprP e1 = simplify(P("exp(ln(x))"));
    CHECK(!structurallyEqual(e1, simplify(P("x"))));
    CHECK(e1->op == Op::Exp);
    // 参数恒正后就可以化简
    checkSimplifiedTo("exp(ln(exp(x)))", "exp(x)");
}

FCG_TEST(simplify_domain_safe_negative_powers) {
    // 指数异号合体会丢掉 x≠0
    ExprP e = simplify(P("x^-1 * x^2"));
    CHECK(e->op == Op::Mul);
    CHECK(!structurallyEqual(e, simplify(P("x"))));
    // 同号指数合并安全
    checkSimplifiedTo("x^-1 * x^-2", "x^-3");
}

FCG_TEST(simplify_domain_preserved_state) {
    // 化简前后定义域一致（x/x 在 0 处仍无定义）
    CHECK(!evalReal(simplify(P("x/x")), "x", 0.0).ok);
    CHECK(!evalReal(P("x/x"), "x", 0.0).ok);
    // sqrt(x^2) -> |x|：x<0 处值相等
    CHECK_NEAR(V(simplify(P("sqrt(x^2)")), -2.0), 2.0, 1e-12);
    // 原式也能求出复数范围外的实数？此处只保证与 abs 一致
    CHECK_NEAR(V(simplify(P("sqrt(x*x)")), -3.0), 3.0, 1e-12);
}

// =======================================================================
// 三、化简：函数恒等式 / 奇偶性 / 绝对值
// =======================================================================

FCG_TEST(simplify_trig_parity) {
    checkSimplifiedTo("sin(-x)", "-sin(x)");
    checkSimplifiedTo("cos(-x)", "cos(x)");
    checkSimplifiedTo("tan(-x)", "-tan(x)");
    checkSimplifiedTo("sinh(-x)", "-sinh(x)");
    checkSimplifiedTo("cosh(-x)", "cosh(x)");
    checkSimplifiedTo("tanh(-x)", "-tanh(x)");
    checkSimplifiedTo("sin(-x) + sin(x)", "0");
    checkSimplifiedTo("cos(-x) - cos(x)", "0");
}

FCG_TEST(simplify_abs_rules) {
    checkSimplifiedTo("abs(-x)", "abs(x)");
    checkSimplifiedTo("abs(abs(x))", "abs(x)");
    checkSimplifiedTo("abs(x^2)", "x^2");
    checkSimplifiedTo("abs(sqrt(x))", "sqrt(x)");
    checkSimplifiedTo("abs(-x^2)", "x^2");
    checkSimplifiedTo("abs(-7)", "7");
}

FCG_TEST(simplify_const_functions) {
    checkConstValue("sin(0)", 0.0);
    checkConstValue("cos(0)", 1.0);
    checkConstValue("tan(0)", 0.0);
    checkConstValue("exp(0)", 1.0);
    checkConstValue("ln(e)", 1.0);
    checkConstValue("log2(8)", 3.0);
    checkConstValue("log10(1000)", 3.0);
    checkConstValue("sqrt(16)", 4.0);
    checkConstValue("cbrt(8)", 2.0);
    checkConstValue("abs(-7)", 7.0);
    checkConstValue("asin(0)", 0.0);
    checkConstValue("atan(0)", 0.0);
    checkConstValue("sinh(0)", 0.0);
    checkConstValue("cosh(0)", 1.0);
    checkConstValue("atanh(0)", 0.0);
    checkConstValue("floor(2.7)", 2.0);
    checkConstValue("ceil(2.1)", 3.0);
    checkConstValue("sign(-3)", -1.0);
    checkConstValue("min(2,7)", 2.0);
    checkConstValue("max(2,7)", 7.0);
    checkConstValue("mod(7,3)", 1.0);
}

FCG_TEST(simplify_const_keep_symbolic) {
    // 不能把无理常数折叠成小数（保持精确性）
    CHECK(simplify(P("sqrt(2)"))->op == Op::Sqrt);
    CHECK(!isNumNode(simplify(P("sin(1)"))));
    CHECK(!isNumNode(simplify(P("ln(2)"))));
    CHECK(!isNumNode(simplify(P("pi"))));
}

FCG_TEST(simplify_sub_normalization) {
    checkSimplifiedTo("x + (-1)", "x - 1");
    checkSimplifiedTo("x - 1 + 1", "x");
    checkSimplifiedTo("-(x + 1)", "-x - 1");
    checkCanonicalEqual("-(x - 1)", "1 - x");
    checkCanonicalEqual("1 - x", "1 + (-x)");
    checkSimplifiedTo("--x", "x");
}

// =======================================================================
// 四、符号等价 / 规范键 / 幂等
// =======================================================================

FCG_TEST(simplify_symbolically_equal) {
    CHECK(symbolicallyEqual(P("x + 1"), P("1 + x")));
    CHECK(symbolicallyEqual(P("2(x+1)"), P("2x + 2")));
    CHECK(symbolicallyEqual(P("sqrt(x^2)"), P("abs(x)")));
    CHECK(symbolicallyEqual(P("sin(-x)"), P("-sin(x)")));
    // 需要数值兜底的恒等式
    CHECK(symbolicallyEqual(P("sin(x)^2 + cos(x)^2"), P("1")));
    CHECK(symbolicallyEqual(P("exp(x)*exp(x)"), P("exp(2x)")));
    // 不等情形
    CHECK(!symbolicallyEqual(P("x"), P("2x")));
    CHECK(!symbolicallyEqual(P("sin(x)"), P("cos(x)")));
    CHECK(!symbolicallyEqual(P("x^2"), P("x^3")));
}

FCG_TEST(simplify_numerically_equal) {
    CHECK(numericallyEqual(P("sin(x)^2 + cos(x)^2"), P("1"), {"x"}));
    CHECK(numericallyEqual(P("exp(x)"), P("exp(x) + 0"), {"x"}));
    CHECK(!numericallyEqual(P("x"), P("2x"), {"x"}));
    CHECK(!numericallyEqual(P("x"), P("x + 0.001"), {"x"}));
    // 变量推断
    CHECK(numericallyEqual(P("x + y"), P("y + x"), {}));
}

FCG_TEST(simplify_canonical_key) {
    CHECK_EQ(canonicalKey(P("x + 1")), canonicalKey(P("1 + x")));
    CHECK_EQ(canonicalKey(P("x*y")), canonicalKey(P("y*x")));
    CHECK_EQ(canonicalKey(P("x - 1")), canonicalKey(P("x + (-1)")));
    CHECK_EQ(canonicalKey(P("sin(x) + 2")), canonicalKey(P("2 + sin(x)")));
    CHECK(canonicalKey(P("x + y")) != canonicalKey(P("x - y")));
    CHECK(canonicalKey(P("x^2")) != canonicalKey(P("x^3")));
    CHECK(canonicalKey(P("min(x,y)")) == canonicalKey(P("min(y,x)")));
    // 同一表达式的键稳定
    CHECK_EQ(canonicalKey(P("exp(-x^2)")), canonicalKey(P("exp(-x^2)")));
}

FCG_TEST(simplify_idempotent) {
    const char* cases[] = {"2x + 3x",       "sin(-x) + 1",   "exp(ln(x))",  "x/x + 1",
                           "(x+1)*(x+1)",   "sqrt(x^2) + x", "2(x+1) - 2x", "abs(-x) + abs(x)",
                           "x^2 * x^3 + 1", "ln(exp(x)) + 1"};
    for (const char* c : cases) {
        ExprP s1 = simplify(P(c));
        ExprP s2 = simplify(s1);
        if (!structurallyEqual(s1, s2)) {
            ::fcgtest::fail(std::string("幂等失败: ") + c + " -> " + s1->toStr() + " -> " + s2->toStr(),
                            __FILE__, __LINE__);
        }
    }
    // simplifyOnce 也应是幂等化简的中间步骤
    CHECK(structurallyEqual(simplify(simplifyOnce(P("x + x"))), simplify(P("2x"))));
}

FCG_TEST(simplify_is_zero_simplified) {
    CHECK(isZeroSimplified(P("sin(x) - sin(x)")));
    CHECK(isZeroSimplified(P("x + 1 - x - 1")));
    CHECK(!isZeroSimplified(P("x + 1")));
}

// =======================================================================
// 五、保护：不展开高次幂、深度嵌套、性能
// =======================================================================

FCG_TEST(simplify_no_pow_expansion) {
    ExprP e = simplify(P("(x+1)^20"));
    CHECK(e->op == Op::Pow);
    CHECK(nodeCount(e) <= 5);  // 绝不能被二项式展开
    CHECK_NEAR(V(e, 0.1), std::pow(1.1, 20.0), 1e-9);
    // (x+1)^20 * 2 也不能展开
    ExprP e2 = simplify(P("2*(x+1)^20"));
    CHECK(nodeCount(e2) <= 8);
    CHECK_NEAR(V(e2, 0.2), 2.0 * std::pow(1.2, 20.0), 1e-9);
}

FCG_TEST(simplify_deep_nesting_no_crash) {
    // 1500 层嵌套加法：必须不爆栈，且数值正确
    ExprP deep = mkVar("x");
    for (int i = 0; i < 1500; ++i) deep = mkAdd(deep, mkNum(1.0));
    ExprP s = simplify(deep);
    CHECK_NEAR(V(s, 0.3), 1500.3, 1e-9);
    // 300 层负号嵌套
    ExprP n = mkVar("x");
    for (int i = 0; i < 300; ++i) n = mkNeg(n);
    ExprP sn = simplify(n);
    CHECK_NEAR(V(sn, 1.7), 1.7, 1e-9);
}

FCG_TEST(simplify_performance) {
    // 玩家单次提交的复杂度，化简必须即时（< 5ms）
    ExprP e = P(
        "sin(x)*cos(x) + exp(-x^2) + ln(x^2 + 1) + sqrt(abs(x) + 1) + tanh(x/2) + "
        "2*sin(x)*sin(x) + x^3/(x^2+1) + 3*cos(x)^2 + sin(x)/cos(x)");
    const int reps = 50;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) {
        ExprP r = simplify(e);
        if (!r) ::fcgtest::fail("化简返回空", __FILE__, __LINE__);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
    if (ms > 5.0) {
        ::fcgtest::fail("化简过慢: " + ::fcgtest::fmt(ms) + " ms（要求 < 5ms）", __FILE__, __LINE__);
    }

    // 中等规模多项和（约 120 项）也要够快
    std::string big;
    for (int i = 1; i <= 40; ++i) {
        if (i > 1) big += "+";
        big += "sin(" + std::to_string(i) + "x)+cos(" + std::to_string(i) + "x)+x^2";
    }
    ExprP bigE = P(big);
    auto t2 = std::chrono::steady_clock::now();
    ExprP bigS = simplify(bigE);
    auto t3 = std::chrono::steady_clock::now();
    double bigMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    CHECK(bigS != nullptr);
    if (bigMs > 5.0) {
        ::fcgtest::fail("大规模化简过慢: " + ::fcgtest::fmt(bigMs) + " ms", __FILE__, __LINE__);
    }
}

FCG_TEST(simplify_rationalize) {
    ExprP r1 = rationalize(P("1/sqrt(2)"));
    CHECK_NEAR(V(r1, 0.0), 1.0 / std::sqrt(2.0), 1e-12);
    ExprP r2 = rationalize(P("1/(1 + sqrt(2))"));
    CHECK_NEAR(V(r2, 0.0), 1.0 / (1.0 + std::sqrt(2.0)), 1e-12);
    ExprP r3 = rationalize(P("x/sqrt(2)"));
    for (double x : {-2.0, 0.5, 3.0}) {
        CHECK_NEAR(V(r3, x), x / std::sqrt(2.0), 1e-12);
    }
    // 无根式时保持等价
    ExprP r4 = rationalize(P("x^2 + 2x + 1"));
    for (double x : {-1.0, 2.0}) {
        CHECK_NEAR(V(r4, x), (x + 1.0) * (x + 1.0), 1e-9);
    }
}

// =======================================================================
// 六、求导：与数值中心差分对比
// =======================================================================

FCG_TEST(diff_polynomial) {
    checkDerivative("x^2 + 3x + 1", {-2.0, 0.5, 3.0});
    checkDerivative("x^5 - 2x^3 + x", {-1.5, 0.0, 1.2});
    checkDerivative("1/x", {-2.0, 0.5, 3.0});
    checkDerivative("x/2 + 3", {-1.0, 4.0});
    // 符号结果验证
    checkValueEqual(diff(P("x^2 + 3x + 1"), "x"), P("2x + 3"), {-2.0, 0.5, 3.0});
}

FCG_TEST(diff_trig) {
    checkDerivative("sin(x)", {-2.0, 0.3, 1.5});
    checkDerivative("cos(x)", {-2.0, 0.3, 1.5});
    checkDerivative("tan(x)", {-1.2, 0.4, 1.3});
    checkDerivative("sin(x)*cos(x)", {-1.9, 0.2, 2.1});
    checkValueEqual(diff(P("sin(x)"), "x"), P("cos(x)"), {-1.0, 0.5, 2.0});
}

FCG_TEST(diff_exp_log) {
    checkDerivative("exp(x)", {-1.5, 0.2, 2.0});
    checkDerivative("ln(x)", {0.4, 1.5, 3.0});
    checkDerivative("log2(x)", {0.5, 2.0, 7.0});
    checkDerivative("log10(x)", {0.5, 2.0, 7.0});
    checkDerivative("e^x", {-1.0, 0.5, 1.5});
    checkDerivative("2^x", {-1.5, 0.3, 2.2});
    checkDerivative("sqrt(x)", {0.25, 4.0, 9.0});
    checkDerivative("cbrt(x)", {-2.0, 0.5, 3.0});
}

FCG_TEST(diff_inverse_trig) {
    checkDerivative("asin(x)", {-0.8, 0.2, 0.9});
    checkDerivative("acos(x)", {-0.7, 0.1, 0.8});
    checkDerivative("atan(x)", {-2.0, 0.5, 3.0});
}

FCG_TEST(diff_hyperbolic) {
    checkDerivative("sinh(x)", {-1.3, 0.4, 2.0});
    checkDerivative("cosh(x)", {-1.3, 0.4, 2.0});
    checkDerivative("tanh(x)", {-1.3, 0.4, 2.0});
    checkDerivative("asinh(x)", {-2.0, 0.5, 2.5});
    checkDerivative("acosh(x)", {1.2, 2.0, 4.0});
    checkDerivative("atanh(x)", {-0.5, 0.3, 0.7});
}

FCG_TEST(diff_nonsmooth) {
    // abs：广义导数 = sign(x)（避开 0 点与数值差分对比）
    checkDerivative("abs(x)", {-1.7, 0.9, 2.3});
    checkValueEqual(diff(P("abs(x)"), "x"), P("sign(x)"), {-1.7, 0.9, 2.3});
    // sign / floor / ceil：几乎处处导数为 0
    checkDerivative("sign(x)", {1.3, -2.2, 0.5});
    checkDerivative("floor(x)", {1.3, 2.7, -1.2});
    checkDerivative("ceil(x)", {1.3, 2.7, -1.2});
    // min / max：几乎处处导数
    checkDerivative("min(x,2)", {0.5, 1.5, 3.0});
    checkDerivative("max(x,2)", {0.5, 1.5, 3.0});
    checkDerivative("min(x^2, 3)", {-0.5, 1.0, 2.0});
    // mod(x, c)：几乎处处导数为 1
    checkDerivative("mod(x,2)", {0.7, 1.3, 2.5, -0.5});
}

FCG_TEST(diff_pow_general) {
    checkDerivative("x^x", {0.7, 1.2, 2.0});
    checkDerivative("x^3.5", {0.5, 2.0});
    checkDerivative("x^(x+1)", {0.8, 1.7});
    checkDerivative("(x^2+1)^(x)", {0.5, 1.5});
}

FCG_TEST(diff_chain_rule) {
    checkDerivative("exp(sin(x))", {-1.0, 0.3, 1.1});
    checkDerivative("ln(x^2 + 1)", {-2.0, 0.0, 1.5});
    checkDerivative("sin(cos(x))", {-1.2, 0.4, 2.0});
    checkDerivative("sqrt(x^2 + 1)", {-2.0, 0.0, 1.5});
    checkDerivative("exp(-x^2)", {-1.5, 0.4, 2.0});
    checkDerivative("atan(x^2)", {-1.3, 0.3, 1.1});
}

FCG_TEST(diff_quotient_product) {
    checkDerivative("x/(x+1)", {-2.5, 0.5, 2.0});
    checkDerivative("sin(x)*exp(x)", {-1.0, 0.4, 1.8});
    checkDerivative("tan(x)*x", {-1.0, 0.5, 1.2});
    checkDerivative("(x^2+1)/(x-2)", {-2.0, 0.0, 3.0});
}

FCG_TEST(diff_constants_and_other_vars) {
    CHECK(isNumNode(diff(P("5"), "x")));
    CHECK(isNumNode(diff(P("pi"), "x")));
    CHECK(isNumNode(diff(P("y"), "x")));
    CHECK(isNumNode(diff(P("ln(2)"), "x")));
    // 对 y 求导
    ExprP dy = diff(P("y^2"), "y");
    for (double y : {-2.0, 0.7, 3.0}) {
        double expected = 2.0 * y;
        CHECK_NEAR(VVar(dy, "y", y), expected, 1e-9);
    }
}

FCG_TEST(diffN_orders) {
    // 0 阶导数 = 原函数
    CHECK(structurallyEqual(diffN(P("x^3"), "x", 0), P("x^3")));
    // x^3：1..4 阶
    checkValueEqual(diffN(P("x^3"), "x", 1), P("3x^2"), {-2.0, 0.7, 3.0});
    checkValueEqual(diffN(P("x^3"), "x", 2), P("6x"), {-2.0, 0.7, 3.0});
    checkValueEqual(diffN(P("x^3"), "x", 3), P("6"), {-2.0, 0.7, 3.0});
    CHECK(isNumNode(diffN(P("x^3"), "x", 4)));
    // 五阶：x^5 的五阶导 = 120
    ExprP d5 = diffN(P("x^5"), "x", 5);
    CHECK_NEAR(V(d5, 1.7), 120.0, 1e-9);
    // 三角函数二阶：sin'' = -sin
    checkValueEqual(diffN(P("sin(x)"), "x", 2), P("-sin(x)"), {-1.0, 0.3, 2.0});
    // exp 任意阶 = exp
    checkValueEqual(diffN(P("exp(x)"), "x", 3), P("exp(x)"), {-1.0, 0.5, 1.5});
}

FCG_TEST(diff_all_ops_covered) {
    // 覆盖全部 Op 的一次求导（不崩溃、结构合理）
    const char* cases[] = {
        "x",           "5",        "pi",          "e",        "i",        "-x",
        "x + x",       "x - x",    "x * x",       "x / x",    "x^x",      "mod(x,2)",
        "sin(x)",      "cos(x)",   "tan(x)",      "exp(x)",   "ln(x)",    "log2(x)",
        "log10(x)",    "sqrt(x)",  "cbrt(x)",     "abs(x)",   "asin(x)",  "acos(x)",
        "atan(x)",     "sinh(x)",  "cosh(x)",     "tanh(x)",  "asinh(x)", "acosh(x)",
        "atanh(x)",    "floor(x)", "ceil(x)",     "sign(x)",  "min(x,2)", "max(x,2)",
    };
    for (const char* c : cases) {
        ExprP f = P(c);
        ExprP raw = diffRaw(f, "x");
        if (!raw) ::fcgtest::fail(std::string("diffRaw 返回空: ") + c, __FILE__, __LINE__);
        CHECK(nodeCount(raw) >= 1);
        ExprP d = diff(f, "x");
        if (!d) ::fcgtest::fail(std::string("diff 返回空: ") + c, __FILE__, __LINE__);
        CHECK(maxDepth(d) < 200);
        // 对 x 求导后不应再含有未绑定变量以外的怪东西：只做结构冒烟检查
        CHECK(nodeCount(d) >= 1);
    }
}

FCG_TEST(diff_numeric_tolerance_checks) {
    // 组合复杂函数仍满足 1e-7 容差
    checkDerivative("exp(-x^2)*sin(2x)", {-1.2, 0.3, 1.5});
    checkDerivative("ln(cos(x) + 2)", {-1.5, 0.0, 1.5});
    checkDerivative("sqrt(sin(x) + 2)", {-1.0, 0.5, 1.3}, 1e-6);
    checkDerivative("x*sinh(x) - cosh(x)", {-2.0, 0.0, 2.0});
}

FCG_TEST(diff_raw_unreduced) {
    // diffRaw 不做化简：结果节点数明显多于 diff
    ExprP f = P("sin(x)*exp(x)");
    ExprP raw = diffRaw(f, "x");
    ExprP s = diff(f, "x");
    CHECK(nodeCount(raw) >= nodeCount(s));
    CHECK(nodeCount(s) > 0);
    // 数值上一致
    for (double x : {-1.0, 0.4, 1.6}) {
        CHECK_NEAR(V(raw, x), centralDiff(f, x), 1e-7 * (1.0 + std::fabs(V(s, x))));
    }
}
