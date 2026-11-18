// test_analysis.cpp — 定义域 / 解析性 / 单调性 / 值域 / 零点 / 极限 测试
#include "test_framework.h"

#include "core/Domain.h"
#include "core/Expr.h"
#include "core/Limit.h"
#include "core/Parser.h"

using namespace fcg;

namespace {

constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kE = 2.71828182845904523536;

ExprP P(const std::string& s) {
    ParseError err;
    ExprP e = parseExpr(s, &err);
    if (!e) ::fcgtest::fail("解析失败: " + s + " -> " + err.message, __FILE__, __LINE__);
    return e;
}

bool inDomain(const ExprP& e, double x) { return domainOf(e, "x").contains(x); }

double domainInvalidCount(const ExprP& e) { return static_cast<double>(domainOf(e, "x").empty() ? 1 : 0); }

bool nearAny(const std::vector<double>& pts, double v, double tol = 1e-6) {
    for (double p : pts) {
        if (std::fabs(p - v) <= tol) return true;
    }
    return false;
}

}  // namespace

// ==================================================================
// Interval / DomainSet
// ==================================================================
FCG_TEST(analysis_interval_basic) {
    Interval a = Interval::closed(-1.0, 2.0);
    CHECK(a.contains(-1.0));
    CHECK(a.contains(0.0));
    CHECK(a.contains(2.0));
    CHECK(!a.contains(2.5));
    CHECK(!a.contains(-1.5));
    CHECK_NEAR(a.length(), 3.0, 1e-12);

    Interval b = Interval::open(-1.0, 2.0);
    CHECK(!b.contains(-1.0));
    CHECK(!b.contains(2.0));
    CHECK(b.contains(-0.5));
    CHECK(!(a == b));
    CHECK(a == Interval::closed(-1.0, 2.0));
}

FCG_TEST(analysis_interval_empty) {
    CHECK(Interval::closed(2.0, 1.0).isEmpty());
    CHECK(Interval::open(1.0, 1.0).isEmpty());
    CHECK(Interval::closed(1.0, 1.0).isEmpty() == false);
    CHECK(Interval::closed(1.0, 1.0).contains(1.0));
    CHECK(!Interval::closed(1.0, 1.0).contains(1.1));
}

FCG_TEST(analysis_interval_infinite) {
    Interval all = Interval::all();
    CHECK(all.contains(0.0));
    CHECK(all.contains(1e6));
    CHECK(all.contains(-1e6));
    CHECK(!all.contains(std::numeric_limits<double>::infinity()));
    CHECK(all.length() > 1e300);

    Interval up;
    up.lo = 0.0;
    up.loInf = false;
    up.loClosed = true;
    CHECK(up.contains(5.0));
    CHECK(!up.contains(-1.0));
    CHECK(up.length() > 1e300);
    CHECK(up.str().find("∞") != std::string::npos);
}

FCG_TEST(analysis_interval_str) {
    std::string s = Interval::closed(-1.0, 2.0).str();
    CHECK(s.find('[') != std::string::npos);
    CHECK(s.find("2]") != std::string::npos);
    CHECK(s.find("-1") != std::string::npos);
    std::string t = Interval::closed(-kPi / 2.0, kPi / 2.0).str();
    CHECK(t.find("π") != std::string::npos);
    std::string u = Interval::open(0.0, 1.0).str();
    CHECK(u.find('(') != std::string::npos);
    CHECK(u.find("1)") != std::string::npos);
}

FCG_TEST(analysis_domainset_normalize) {
    DomainSet d;
    d.parts.push_back(Interval::closed(0.0, 1.0));
    d.parts.push_back(Interval::closed(0.5, 2.0));
    d.normalize();
    CHECK_EQ(d.parts.size(), size_t(1));
    CHECK(d.contains(1.7));
    CHECK(!d.contains(2.5));

    DomainSet e;
    e.parts.push_back(Interval::closed(0.0, 1.0));
    e.parts.push_back(Interval::closed(2.0, 3.0));
    e.normalize();
    CHECK_EQ(e.parts.size(), size_t(2));
    CHECK(!e.contains(1.5));
    CHECK(e.contains(2.5));
}

FCG_TEST(analysis_domainset_contains_all) {
    DomainSet d;
    d.parts.push_back(Interval::closed(0.0, 1.0));
    d.parts.push_back(Interval::closed(2.0, 3.0));
    d.normalize();
    CHECK(d.containsAll(Interval::closed(0.0, 1.0)));
    CHECK(d.containsAll(Interval::closed(0.2, 0.8)));
    CHECK(d.containsAll(Interval::closed(2.0, 3.0)));
    CHECK(!d.containsAll(Interval::closed(0.0, 2.0)));
    CHECK(!d.containsAll(Interval::open(1.0, 2.0)));
    CHECK(d.containsAll(Interval::open(2.0, 3.0)));

    DomainSet w = DomainSet::whole();
    CHECK(w.containsAll(Interval::all()));
    CHECK(!w.empty());
    CHECK(w.str().find("∞") != std::string::npos);
}

// ==================================================================
// 定义域
// ==================================================================
FCG_TEST(analysis_domain_polynomial_whole) {
    ExprP e = P("x^2 + 1");
    DomainSet d = domainOf(e, "x");
    CHECK_EQ(d.parts.size(), size_t(1));
    CHECK(d.parts[0].loInf);
    CHECK(d.parts[0].hiInf);
    CHECK(inDomain(e, 0.0));
    CHECK(inDomain(e, -1e6));
    CHECK(inDomain(e, 1e6));
}

FCG_TEST(analysis_domain_reciprocal) {
    ExprP e = P("1/x");
    CHECK(!inDomain(e, 0.0));
    CHECK(inDomain(e, 1.0));
    CHECK(inDomain(e, -2.0));
    ExprP f = P("1/(x-1)");
    CHECK(!inDomain(f, 1.0));
    CHECK(inDomain(f, 0.0));
    CHECK(inDomain(f, 2.0));
}

FCG_TEST(analysis_domain_sqrt) {
    ExprP e = P("sqrt(x)");
    CHECK(inDomain(e, 0.0));
    CHECK(inDomain(e, 4.0));
    CHECK(!inDomain(e, -1.0));
    CHECK(!inDomain(e, -1e-6));

    CHECK(inDomain(P("sqrt(x^2+1)"), 100.0));
    ExprP g = P("sqrt(1-x^2)");
    CHECK(inDomain(g, 0.0));
    CHECK(inDomain(g, 1.0));
    CHECK(inDomain(g, -1.0));
    CHECK(!inDomain(g, 1.5));
    CHECK(!inDomain(g, -1.5));
}

FCG_TEST(analysis_domain_ln) {
    ExprP e = P("ln(x)");
    CHECK(inDomain(e, 1e-9));
    CHECK(!inDomain(e, 0.0));
    CHECK(!inDomain(e, -1.0));
    ExprP f = P("ln(x^2)");
    CHECK(!inDomain(f, 0.0));
    CHECK(inDomain(f, 2.0));
    CHECK(inDomain(f, -2.0));
}

FCG_TEST(analysis_domain_logs) {
    CHECK(inDomain(P("log2(x)"), 1e-6));
    CHECK(!inDomain(P("log2(x)"), -1.0));
    CHECK(inDomain(P("log10(x)"), 100.0));
    CHECK(!inDomain(P("log10(x)"), 0.0));
}

FCG_TEST(analysis_domain_tan_poles) {
    ExprP e = P("tan(x)");
    CHECK(inDomain(e, 0.0));
    CHECK(inDomain(e, 1.0));
    CHECK(inDomain(e, kPi));
    CHECK(!inDomain(e, kPi / 2.0));
    CHECK(!inDomain(e, -kPi / 2.0));
    CHECK(!inDomain(e, 3.0 * kPi / 2.0));
    CHECK(inDomain(e, 0.5 * kPi + 0.3));  // 极限点旁仍然有定义
}

FCG_TEST(analysis_domain_asin_acos) {
    ExprP a = P("asin(x)");
    CHECK(inDomain(a, 0.0));
    CHECK(inDomain(a, 1.0));
    CHECK(inDomain(a, -1.0));
    CHECK(!inDomain(a, 1.1));
    CHECK(!inDomain(a, -1.1));

    ExprP b = P("asin(2x)");
    CHECK(inDomain(b, 0.5));
    CHECK(!inDomain(b, 0.6));

    ExprP c = P("acos(x/2)");
    CHECK(inDomain(c, 2.0));
    CHECK(inDomain(c, -2.0));
    CHECK(!inDomain(c, 2.1));
}

FCG_TEST(analysis_domain_atanh_acosh) {
    ExprP a = P("atanh(x)");
    CHECK(inDomain(a, 0.5));
    CHECK(inDomain(a, -0.5));
    CHECK(!inDomain(a, 1.0));
    CHECK(!inDomain(a, -1.0));

    ExprP b = P("acosh(x)");
    CHECK(inDomain(b, 1.0));
    CHECK(inDomain(b, 2.0));
    CHECK(!inDomain(b, 0.9));
}

FCG_TEST(analysis_domain_pow) {
    CHECK_EQ(domainOf(P("x^2"), "x").parts.size(), size_t(1));
    CHECK(domainOf(P("x^2"), "x").parts[0].loInf);

    ExprP neg = mkPow(mkVar("x"), mkNum(-1.0));
    CHECK(!inDomain(neg, 0.0));
    CHECK(inDomain(neg, 2.0));

    ExprP half = P("x^0.5");
    CHECK(inDomain(half, 0.0));
    CHECK(inDomain(half, 4.0));
    CHECK(!inDomain(half, -0.25));

    ExprP pv = P("x^pi");
    CHECK(inDomain(pv, 2.0));
    CHECK(!inDomain(pv, -2.0));
}

FCG_TEST(analysis_domain_composite) {
    ExprP e = P("ln(sin(x))");
    CHECK(inDomain(e, kPi / 2.0));
    CHECK(!inDomain(e, -0.1));
    CHECK(!inDomain(e, 3.5));

    ExprP g = P("sqrt(ln(x))");
    CHECK(inDomain(g, 2.0));
    CHECK(!inDomain(g, 0.5));

    ExprP h = P("ln(1/(x-1))");
    CHECK(inDomain(h, 2.0));
    CHECK(!inDomain(h, 0.5));
    CHECK(!inDomain(h, 1.0));
}

FCG_TEST(analysis_domain_div_composite) {
    ExprP e = P("1/(x^2-1)");
    CHECK(!inDomain(e, 1.0));
    CHECK(!inDomain(e, -1.0));
    CHECK(inDomain(e, 0.0));

    ExprP g = P("1/sin(x)");
    CHECK(!inDomain(g, 0.0));
    CHECK(!inDomain(g, kPi));
    CHECK(inDomain(g, 1.0));
}

FCG_TEST(analysis_domain_constant_invalid) {
    CHECK(domainOf(P("ln(-1)"), "x").empty());
    CHECK(domainOf(P("sqrt(-1)"), "x").empty());
    CHECK(domainOf(P("1/0"), "x").empty());
    CHECK(!domainOf(P("x"), "x").empty());
}

FCG_TEST(analysis_domain_abs_floor_whole) {
    CHECK(inDomain(P("abs(x)"), 0.0));
    CHECK(inDomain(P("abs(x)"), -5.0));
    CHECK(inDomain(P("floor(x)"), 2.5));
    CHECK(inDomain(P("min(x, 0)"), 3.0));
    CHECK(inDomain(P("mod(x, 2)"), 3.0));
    CHECK(domainOf(P("mod(x, 0)"), "x").empty());
}

FCG_TEST(analysis_domain_exponential) {
    CHECK(inDomain(P("exp(x)"), 100.0));
    CHECK(inDomain(P("exp(x)"), -100.0));
    CHECK(inDomain(P("atan(x)"), 1e6));
    CHECK(inDomain(P("sinh(x)"), 10.0));
    CHECK(inDomain(P("tanh(x)"), 10.0));
}

FCG_TEST(analysis_domain_str_contains) {
    std::string s = domainOf(P("sqrt(x)"), "x").str();
    CHECK(s.find("∞") != std::string::npos);
    CHECK(s.find("0") != std::string::npos);
}

// ==================================================================
// 解析性
// ==================================================================
FCG_TEST(analysis_analytic_polynomial) {
    std::string reason;
    CHECK(isAnalyticOn(P("x^2 + 1"), Interval::closed(-5.0, 5.0), "x", &reason));
    CHECK(isAnalyticOn(P("sin(x)*exp(x)"), Interval::closed(-2.0, 2.0), "x", &reason));
}

FCG_TEST(analysis_analytic_abs_false) {
    std::string reason;
    CHECK_FALSE(isAnalyticOn(P("abs(x)"), Interval::closed(-1.0, 1.0), "x", &reason));
    CHECK(reason.find("abs") != std::string::npos);
}

FCG_TEST(analysis_analytic_other_nonanalytic) {
    std::string reason;
    CHECK_FALSE(isAnalyticOn(P("floor(x)"), Interval::closed(0.0, 2.0), "x", &reason));
    CHECK_FALSE(isAnalyticOn(P("sign(x)"), Interval::closed(0.0, 2.0), "x", &reason));
    CHECK_FALSE(isAnalyticOn(P("min(x, 0)"), Interval::closed(0.0, 2.0), "x", &reason));
    CHECK_FALSE(isAnalyticOn(P("mod(x, 2)"), Interval::closed(0.0, 1.0), "x", &reason));
}

FCG_TEST(analysis_analytic_reciprocal) {
    std::string reason;
    CHECK(isAnalyticOn(P("1/x"), Interval::closed(1.0, 2.0), "x", &reason));
    CHECK_FALSE(isAnalyticOn(P("1/x"), Interval::closed(-1.0, 1.0), "x", &reason));
    CHECK(reason.size() > 0);
}

FCG_TEST(analysis_analytic_sqrt_endpoint) {
    std::string reason;
    CHECK(isAnalyticOn(P("sqrt(x)"), Interval::open(0.0, 1.0), "x", &reason));
    CHECK_FALSE(isAnalyticOn(P("sqrt(x)"), Interval::closed(0.0, 1.0), "x", &reason));
    CHECK(reason.size() > 0);
}

FCG_TEST(analysis_analytic_tan) {
    std::string reason;
    CHECK(isAnalyticOn(P("tan(x)"), Interval::open(0.0, 1.0), "x", &reason));
    CHECK_FALSE(isAnalyticOn(P("tan(x)"), Interval::closed(0.0, 2.0), "x", &reason));
}

FCG_TEST(analysis_analytic_ln) {
    std::string reason;
    CHECK(isAnalyticOn(P("ln(x)"), Interval::closed(1.0, kE), "x", &reason));
    CHECK_FALSE(isAnalyticOn(P("ln(x)"), Interval::closed(-1.0, 1.0), "x", &reason));
}

FCG_TEST(analysis_built_from_analytic_ops) {
    Op bad = Op::Num;
    CHECK(isBuiltFromAnalyticOps(P("sin(x) + x"), &bad));
    CHECK_FALSE(isBuiltFromAnalyticOps(P("abs(x)"), &bad));
    CHECK(bad == Op::Abs);
    bad = Op::Num;
    CHECK_FALSE(isBuiltFromAnalyticOps(P("floor(x) + 1"), &bad));
    CHECK(bad == Op::Floor);
}

// ==================================================================
// 奇点
// ==================================================================
FCG_TEST(analysis_singularities_reciprocal) {
    auto pts = singularitiesIn(P("1/x"), Interval::closed(-1.0, 1.0), "x");
    CHECK(!pts.empty());
    CHECK(nearAny(pts, 0.0, 1e-9));
}

FCG_TEST(analysis_singularities_tan) {
    auto pts = singularitiesIn(P("tan(x)"), Interval::closed(0.0, 2.0), "x");
    CHECK(!pts.empty());
    CHECK(nearAny(pts, kPi / 2.0, 1e-6));
}

FCG_TEST(analysis_singularities_ln) {
    auto pts = singularitiesIn(P("ln(x)"), Interval::closed(-1.0, 1.0), "x");
    CHECK(!pts.empty());
    CHECK(nearAny(pts, 0.0, 1e-9));
}

// ==================================================================
// 单调性
// ==================================================================
FCG_TEST(analysis_monotonic_quadratic) {
    MonotonicResult up = monotonicOn(P("x^2"), Interval::closed(1.0, 2.0), "x");
    CHECK(up.ok);
    CHECK(up.increasing);
    CHECK(up.strictly);
    CHECK(up.error.empty());

    MonotonicResult down = monotonicOn(P("x^2"), Interval::closed(-2.0, -1.0), "x");
    CHECK(down.ok);
    CHECK_FALSE(down.increasing);
    CHECK(down.strictly);
}

FCG_TEST(analysis_monotonic_sin_not) {
    MonotonicResult r = monotonicOn(P("sin(x)"), Interval::closed(0.0, 3.0), "x");
    CHECK_FALSE(r.ok);
    CHECK(r.error.size() > 0);
}

FCG_TEST(analysis_monotonic_constant) {
    MonotonicResult r = monotonicOn(mkNum(3.0), Interval::closed(0.0, 1.0), "x");
    CHECK(r.ok);
    CHECK(r.increasing);
    CHECK_FALSE(r.strictly);
}

FCG_TEST(analysis_monotonic_reciprocal) {
    MonotonicResult r = monotonicOn(P("1/x"), Interval::closed(1.0, 2.0), "x");
    CHECK(r.ok);
    CHECK_FALSE(r.increasing);
    CHECK(r.strictly);

    MonotonicResult bad = monotonicOn(P("1/x"), Interval::closed(-1.0, 1.0), "x");
    CHECK_FALSE(bad.ok);
}

FCG_TEST(analysis_monotonic_exp) {
    MonotonicResult r = monotonicOn(P("exp(x)"), Interval::closed(-5.0, 5.0), "x");
    CHECK(r.ok);
    CHECK(r.increasing);
    CHECK(r.strictly);
}

FCG_TEST(analysis_monotonic_cubic) {
    MonotonicResult r = monotonicOn(P("x^3"), Interval::closed(-1.0, 1.0), "x");
    CHECK(r.ok);
    CHECK(r.increasing);
    CHECK(r.strictly);
}

// ==================================================================
// 值域
// ==================================================================
FCG_TEST(analysis_range_sin) {
    RangeResult r = rangeOn(P("sin(x)"), Interval::closed(0.0, 2.0 * kPi), "x");
    CHECK(r.ok);
    CHECK_NEAR(r.lo, -1.0, 1e-4);
    CHECK_NEAR(r.hi, 1.0, 1e-4);
}

FCG_TEST(analysis_range_quadratic) {
    RangeResult r = rangeOn(P("x^2"), Interval::closed(-1.0, 2.0), "x");
    CHECK(r.ok);
    CHECK_NEAR(r.lo, 0.0, 1e-4);
    CHECK_NEAR(r.hi, 4.0, 1e-6);
}

FCG_TEST(analysis_range_reciprocal) {
    RangeResult r = rangeOn(P("1/x"), Interval::closed(1.0, 2.0), "x");
    CHECK(r.ok);
    CHECK_NEAR(r.lo, 0.5, 1e-4);
    CHECK_NEAR(r.hi, 1.0, 1e-4);
}

FCG_TEST(analysis_range_exp) {
    RangeResult r = rangeOn(P("exp(x)"), Interval::closed(0.0, 1.0), "x");
    CHECK(r.ok);
    CHECK_NEAR(r.lo, 1.0, 1e-4);
    CHECK_NEAR(r.hi, kE, 1e-4);
}

FCG_TEST(analysis_range_abs) {
    RangeResult r = rangeOn(P("abs(x)"), Interval::closed(-2.0, 1.0), "x");
    CHECK(r.ok);
    CHECK_NEAR(r.lo, 0.0, 1e-4);
    CHECK_NEAR(r.hi, 2.0, 1e-6);
}

FCG_TEST(analysis_range_unbounded) {
    RangeResult r = rangeOn(P("1/x"), Interval::closed(-1.0, 1.0), "x");
    CHECK(r.ok);
    CHECK(std::isinf(r.lo) && r.lo < 0);
    CHECK(std::isinf(r.hi) && r.hi > 0);
}

FCG_TEST(analysis_range_constant) {
    RangeResult r = rangeOn(mkNum(3.5), Interval::closed(0.0, 1.0), "x");
    CHECK(r.ok);
    CHECK_NEAR(r.lo, 3.5, 1e-9);
    CHECK_NEAR(r.hi, 3.5, 1e-9);
}

// ==================================================================
// 零点
// ==================================================================
FCG_TEST(analysis_zeros_quadratic) {
    auto z = zerosIn(P("x^2 - 1"), Interval::closed(-2.0, 2.0), "x");
    CHECK_EQ(z.size(), size_t(2));
    CHECK(nearAny(z, -1.0));
    CHECK(nearAny(z, 1.0));
}

FCG_TEST(analysis_zeros_sin_period) {
    auto z = zerosIn(P("sin(x)"), Interval::closed(0.0, 2.0 * kPi), "x");
    CHECK_EQ(z.size(), size_t(3));
    CHECK(nearAny(z, 0.0));
    CHECK(nearAny(z, kPi));
    CHECK(nearAny(z, 2.0 * kPi));
}

FCG_TEST(analysis_zeros_none) {
    auto z = zerosIn(P("exp(x)"), Interval::closed(-5.0, 5.0), "x");
    CHECK(z.empty());
}

FCG_TEST(analysis_zeros_touch) {
    auto z = zerosIn(P("(x-1)^2"), Interval::closed(0.0, 2.0), "x");
    CHECK_EQ(z.size(), size_t(1));
    CHECK(nearAny(z, 1.0, 1e-5));
}

FCG_TEST(analysis_zeros_ln) {
    auto z = zerosIn(P("ln(x)"), Interval::closed(-1.0, 3.0), "x");
    CHECK_EQ(z.size(), size_t(1));
    CHECK(nearAny(z, 1.0, 1e-6));
}

// ==================================================================
// 极限
// ==================================================================
FCG_TEST(analysis_limit_sinx_over_x) {
    LimitResult r = limitAt(P("sin(x)/x"), "x", 0.0, Direction::Both);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 1.0, 1e-6);
}

FCG_TEST(analysis_limit_sinx_over_x_left) {
    LimitResult r = limitAt(P("sin(x)/x"), "x", 0.0, Direction::Left);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 1.0, 1e-6);
}

FCG_TEST(analysis_limit_one_plus_inv_x_pow_x) {
    LimitResult r = limitAtInfinity(P("(1 + 1/x)^x"), "x", 1);
    CHECK(r.ok());
    CHECK_NEAR(r.value, kE, 1e-4);
}

FCG_TEST(analysis_limit_one_over_x_inf) {
    LimitResult r = limitAtInfinity(P("1/x"), "x", 1);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 0.0, 1e-9);
    LimitResult l = limitAtInfinity(P("1/x"), "x", -1);
    CHECK(l.ok());
    CHECK_NEAR(l.value, 0.0, 1e-9);
}

FCG_TEST(analysis_limit_x_squared_inf) {
    LimitResult r = limitAtInfinity(P("x^2"), "x", 1);
    CHECK(r.kind == LimitResult::Kind::PosInf);
    LimitResult l = limitAtInfinity(P("x^2"), "x", -1);
    CHECK(l.kind == LimitResult::Kind::PosInf);
}

FCG_TEST(analysis_limit_tan_pole) {
    LimitResult both = limitAt(P("tan(x)"), "x", kPi / 2.0, Direction::Both);
    CHECK(both.kind == LimitResult::Kind::Undefined);
    LimitResult left = limitAt(P("tan(x)"), "x", kPi / 2.0, Direction::Left);
    CHECK(left.kind == LimitResult::Kind::PosInf);
    LimitResult right = limitAt(P("tan(x)"), "x", kPi / 2.0, Direction::Right);
    CHECK(right.kind == LimitResult::Kind::NegInf);
}

FCG_TEST(analysis_limit_continuous) {
    LimitResult a = limitAt(P("sin(x)"), "x", 0.0, Direction::Both);
    CHECK(a.ok());
    CHECK_NEAR(a.value, 0.0, 1e-12);
    LimitResult b = limitAt(P("2x + 1"), "x", 3.0, Direction::Both);
    CHECK(b.ok());
    CHECK_NEAR(b.value, 7.0, 1e-12);
}

FCG_TEST(analysis_limit_zero_over_zero_cos) {
    LimitResult r = limitAt(P("(1 - cos(x))/x^2"), "x", 0.0, Direction::Both);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 0.5, 1e-4);
}

FCG_TEST(analysis_limit_one_over_x_at_zero) {
    LimitResult both = limitAt(P("1/x"), "x", 0.0, Direction::Both);
    CHECK(both.kind == LimitResult::Kind::Undefined);
    LimitResult left = limitAt(P("1/x"), "x", 0.0, Direction::Left);
    CHECK(left.kind == LimitResult::Kind::NegInf);
    LimitResult right = limitAt(P("1/x"), "x", 0.0, Direction::Right);
    CHECK(right.kind == LimitResult::Kind::PosInf);
}

FCG_TEST(analysis_limit_exp_inf) {
    LimitResult up = limitAtInfinity(P("exp(x)"), "x", 1);
    CHECK(up.kind == LimitResult::Kind::PosInf);
    LimitResult down = limitAtInfinity(P("exp(-x)"), "x", 1);
    CHECK(down.ok());
    CHECK_NEAR(down.value, 0.0, 1e-9);
}

FCG_TEST(analysis_limit_ln_at_zero_right) {
    LimitResult r = limitAt(P("ln(x)"), "x", 0.0, Direction::Right);
    CHECK(r.kind == LimitResult::Kind::NegInf);
}

FCG_TEST(analysis_limit_rational_inf) {
    LimitResult r = limitAtInfinity(P("(2x^2 + 1)/(x^2 - 3)"), "x", 1);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 2.0, 1e-9);
    LimitResult z = limitAtInfinity(P("x/(x^2 + 1)"), "x", 1);
    CHECK(z.ok());
    CHECK_NEAR(z.value, 0.0, 1e-9);
}

FCG_TEST(analysis_limit_x_cubed_neg_inf) {
    LimitResult p = limitAtInfinity(P("x^3"), "x", 1);
    CHECK(p.kind == LimitResult::Kind::PosInf);
    LimitResult n = limitAtInfinity(P("x^3"), "x", -1);
    CHECK(n.kind == LimitResult::Kind::NegInf);
}

FCG_TEST(analysis_limit_horizontal_asymptote) {
    double v = 0.0;
    CHECK(horizontalAsymptote(P("atan(x)"), "x", 1, &v));
    CHECK_NEAR(v, kPi / 2.0, 1e-6);
    CHECK(horizontalAsymptote(P("1/x"), "x", 1, &v));
    CHECK_NEAR(v, 0.0, 1e-9);
    CHECK_FALSE(horizontalAsymptote(P("x^2"), "x", 1, &v));
}

FCG_TEST(analysis_limit_str) {
    CHECK_EQ(limitAt(mkNum(1.0), "x", 0.0, Direction::Both).str(), std::string("1"));
    CHECK_EQ(limitAtInfinity(P("1/x"), "x", 1).str(), std::string("0"));
    CHECK_EQ(limitAt(P("tan(x)"), "x", kPi / 2.0, Direction::Both).str(), std::string("不存在"));
    CHECK_EQ(limitAtInfinity(P("x^2"), "x", 1).str(), std::string("+∞"));
    CHECK_EQ(limitAtInfinity(P("-x^2"), "x", 1).str(), std::string("-∞"));
}

FCG_TEST(analysis_limit_sqrt_at_zero) {
    LimitResult r = limitAt(P("sqrt(x)"), "x", 0.0, Direction::Both);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 0.0, 1e-12);
}

FCG_TEST(analysis_limit_abs_at_zero) {
    LimitResult r = limitAt(P("abs(x)"), "x", 0.0, Direction::Both);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 0.0, 1e-9);
}

FCG_TEST(analysis_limit_floor_discontinuity) {
    LimitResult half = limitAt(P("floor(x)"), "x", 0.5, Direction::Both);
    CHECK(half.ok());
    CHECK_NEAR(half.value, 0.0, 1e-9);
    LimitResult jump = limitAt(P("floor(x)"), "x", 1.0, Direction::Both);
    CHECK(jump.kind == LimitResult::Kind::Undefined);
}

FCG_TEST(analysis_limit_exponential_dominated) {
    LimitResult a = limitAtInfinity(P("exp(x)/x"), "x", 1);
    CHECK(a.kind == LimitResult::Kind::PosInf);
    LimitResult b = limitAtInfinity(P("x/exp(x)"), "x", 1);
    CHECK(b.ok());
    CHECK_NEAR(b.value, 0.0, 1e-6);
}

FCG_TEST(analysis_limit_ln_div_x_inf) {
    LimitResult r = limitAtInfinity(P("ln(x)/x"), "x", 1);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 0.0, 1e-6);
}

FCG_TEST(analysis_limit_sin_oscillation) {
    LimitResult r = limitAt(P("sin(1/x)"), "x", 0.0, Direction::Both);
    CHECK(r.kind == LimitResult::Kind::Undefined);
    LimitResult s = limitAtInfinity(P("sin(x)"), "x", 1);
    CHECK(s.kind == LimitResult::Kind::Undefined);
}

FCG_TEST(analysis_limit_exp_minus_one_over_x) {
    LimitResult r = limitAt(P("(exp(x)-1)/x"), "x", 0.0, Direction::Both);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 1.0, 1e-6);
}

FCG_TEST(analysis_limit_ln_over_x_minus_one) {
    LimitResult r = limitAt(P("ln(x)/(x-1)"), "x", 1.0, Direction::Both);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 1.0, 1e-6);
}

FCG_TEST(analysis_limit_sin3x_over_x) {
    LimitResult r = limitAt(P("sin(3x)/x"), "x", 0.0, Direction::Both);
    CHECK(r.ok());
    CHECK_NEAR(r.value, 3.0, 1e-6);
}

FCG_TEST(analysis_zeros_tan_no_pole_roots) {
    // tan 的极点不是零点，不能被误判
    auto z = zerosIn(P("tan(x)"), Interval::closed(-2.0, 2.0), "x");
    CHECK_EQ(z.size(), size_t(1));
    CHECK(nearAny(z, 0.0, 1e-6));
}

FCG_TEST(analysis_range_tan_unbounded) {
    RangeResult r = rangeOn(P("tan(x)"), Interval::open(-kPi / 2.0, kPi / 2.0), "x");
    CHECK(r.ok);
    CHECK(std::isinf(r.lo) && r.lo < 0);
    CHECK(std::isinf(r.hi) && r.hi > 0);
}

FCG_TEST(analysis_analytic_rational_smooth) {
    std::string reason;
    CHECK(isAnalyticOn(P("1/(x^2+1)"), Interval::closed(-1.0, 1.0), "x", &reason));
    CHECK(isAnalyticOn(P("exp(1/x)"), Interval::closed(1.0, 2.0), "x", &reason));
}
