// test_complex.cpp — 复数域 / 复变函数分析的测试
//
// 覆盖：ComplexRegion 几何、全纯性（结构 + 柯西-黎曼）、奇点与极点阶数、
//       留数、围道积分、区域零点计数（辐角原理）、|f| 范围、复算子检测。
// 数值断言都写明容差。
#include "test_framework.h"

#include <chrono>
#include <string>

#include "core/Complex.h"
#include "core/Parser.h"

using namespace fcg;

namespace {

ExprP P(const std::string& s) {
    ParseError err;
    ExprP e = parseExpr(s, &err);
    if (!e) ::fcgtest::fail("解析失败: " + s + " -> " + err.message, __FILE__, __LINE__);
    return e;
}

bool nearC(Cplx a, Cplx b, double tol) { return (a - b).abs() <= tol; }

void expectNearC(Cplx a, Cplx b, double tol, const char* what) {
    if (!nearC(a, b, tol)) {
        ::fcgtest::fail(std::string(what) + " 期望 " + std::to_string(b.re) + (b.im >= 0 ? "+" : "") +
                            std::to_string(b.im) + "i，实际 " + std::to_string(a.re) +
                            (a.im >= 0 ? "+" : "") + std::to_string(a.im) + "i",
                        __FILE__, __LINE__);
    }
}

}  // namespace

// =================================================================
// 1) 区域几何
// =================================================================
FCG_TEST(complex_region_disk) {
    ComplexRegion d = ComplexRegion::disk(Cplx(0, 0), 1.0);
    CHECK(d.contains(Cplx(0.5, 0.5)));
    CHECK(d.contains(Cplx(0.0, 0.0)));
    CHECK(!d.contains(Cplx(1.5, 0.0)));
    // 开圆盘：tol=0 时边界不在区域内
    CHECK(!d.contains(Cplx(1.0, 0.0), 0.0));
    // tol 表示向外容差
    CHECK(d.contains(Cplx(1.0 + 1e-4, 0.0), 1e-3));
    CHECK(!d.contains(Cplx(1.0 + 1e-4, 0.0), 1e-6));
    CHECK(!d.isEmpty());
    CHECK_EQ(d.str(), std::string("|z| < 1"));

    ComplexRegion d2 = ComplexRegion::disk(Cplx(1, 0), 2.0);
    CHECK(d2.contains(Cplx(2.5, 0.0)));
    CHECK(!d2.contains(Cplx(3.5, 0.0)));
    CHECK_EQ(d2.str(), std::string("|z - 1| < 2"));

    ComplexRegion di = ComplexRegion::disk(Cplx(0, 1), 1.0);
    CHECK_EQ(di.str(), std::string("|z - i| < 1"));
    ComplexRegion dni = ComplexRegion::disk(Cplx(0, -1), 1.0);
    CHECK_EQ(dni.str(), std::string("|z + i| < 1"));
}

FCG_TEST(complex_region_annulus) {
    ComplexRegion a = ComplexRegion::annulus(Cplx(0, 0), 1.0, 2.0);
    CHECK(!a.contains(Cplx(0.5, 0.0)));
    CHECK(a.contains(Cplx(1.5, 0.0)));
    CHECK(!a.contains(Cplx(2.5, 0.0)));
    CHECK(!a.contains(Cplx(0.0, 0.0)));
    CHECK(!a.contains(Cplx(1.0, 0.0), 0.0));  // 内边界
    CHECK(!a.contains(Cplx(2.0, 0.0), 0.0));  // 外边界
    CHECK_EQ(a.str(), std::string("1 < |z| < 2"));
    CHECK(!a.isEmpty());
    CHECK(ComplexRegion::annulus(Cplx(0, 0), 2.0, 1.0).isEmpty());
}

FCG_TEST(complex_region_rectangle) {
    ComplexRegion r = ComplexRegion::rectangle(-1.0, 1.0, -1.0, 1.0);
    CHECK(r.contains(Cplx(0.5, -0.5)));
    CHECK(!r.contains(Cplx(1.5, 0.0)));
    CHECK(!r.contains(Cplx(0.0, 1.5)));
    CHECK(!r.contains(Cplx(1.0, 0.0), 0.0));
    CHECK_EQ(r.str(), std::string("-1 < Re z < 1, -1 < Im z < 1"));
    CHECK(!r.isEmpty());
    CHECK(ComplexRegion::rectangle(1.0, -1.0, -1.0, 1.0).isEmpty());
    CHECK(ComplexRegion::rectangle(-1.0, 1.0, 0.0, 0.0).isEmpty());
}

FCG_TEST(complex_region_halfplanes) {
    ComplexRegion up = ComplexRegion::disk(Cplx(0, 0), 1.0);
    up.kind = ComplexRegion::Kind::HalfPlaneUpper;
    up.yLine = 0.0;
    CHECK(up.contains(Cplx(100.0, 1.0)));
    CHECK(up.contains(Cplx(-5.0, 0.1)));
    CHECK(!up.contains(Cplx(0.0, -1.0)));
    CHECK(!up.contains(Cplx(0.0, 0.0), 0.0));
    CHECK_EQ(up.str(), std::string("上半平面 Im z > 0"));
    CHECK(!up.isEmpty());

    ComplexRegion low = up;
    low.kind = ComplexRegion::Kind::HalfPlaneLower;
    CHECK(low.contains(Cplx(0.0, -1.0)));
    CHECK(!low.contains(Cplx(0.0, 1.0)));
    CHECK_EQ(low.str(), std::string("下半平面 Im z < 0"));

    ComplexRegion right = up;
    right.kind = ComplexRegion::Kind::HalfPlaneRight;
    CHECK(right.contains(Cplx(1.0, 0.0)));
    CHECK(!right.contains(Cplx(-1.0, 0.0)));
    CHECK_EQ(right.str(), std::string("右半平面 Re z > 0"));

    ComplexRegion upShift = up;
    upShift.yLine = 1.0;
    CHECK(upShift.contains(Cplx(0.0, 1.5)));
    CHECK(!upShift.contains(Cplx(0.0, 0.5)));
}

FCG_TEST(complex_region_exterior_and_plane) {
    ComplexRegion ext;
    ext.kind = ComplexRegion::Kind::ExteriorOfDisk;
    ext.center = Cplx(0, 0);
    ext.radius = 1.0;
    CHECK(!ext.contains(Cplx(0.5, 0.0)));
    CHECK(ext.contains(Cplx(2.0, 0.0)));
    CHECK(!ext.contains(Cplx(1.0, 0.0), 0.0));
    CHECK_EQ(ext.str(), std::string("|z| > 1"));
    CHECK(!ext.isEmpty());

    ComplexRegion pl = ComplexRegion::wholePlane();
    CHECK(pl.contains(Cplx(123.0, -456.0)));
    CHECK(!pl.isEmpty());
    CHECK_EQ(pl.str(), std::string("整个复平面"));

    CHECK(ComplexRegion::disk(Cplx(0, 0), 0.0).isEmpty());
    CHECK(ComplexRegion::disk(Cplx(0, 0), -1.0).isEmpty());
}

// =================================================================
// 2) 全纯性：结构检查 + 柯西-黎曼
// =================================================================
FCG_TEST(complex_op_is_holomorphic) {
    CHECK(opIsHolomorphic(Op::Add));
    CHECK(opIsHolomorphic(Op::Mul));
    CHECK(opIsHolomorphic(Op::Div));
    CHECK(opIsHolomorphic(Op::Pow));
    CHECK(opIsHolomorphic(Op::Sin));
    CHECK(opIsHolomorphic(Op::Exp));
    CHECK(opIsHolomorphic(Op::Ln));
    CHECK(opIsHolomorphic(Op::Sqrt));
    CHECK(opIsHolomorphic(Op::I));
    // 非全纯
    CHECK(!opIsHolomorphic(Op::Re));
    CHECK(!opIsHolomorphic(Op::Im));
    CHECK(!opIsHolomorphic(Op::Conj));
    CHECK(!opIsHolomorphic(Op::Arg));
    CHECK(!opIsHolomorphic(Op::Abs));  // 实轴解析，复平面不解析
    CHECK(!opIsHolomorphic(Op::Min));
    CHECK(!opIsHolomorphic(Op::Max));
    CHECK(!opIsHolomorphic(Op::Floor));
    CHECK(!opIsHolomorphic(Op::Ceil));
    CHECK(!opIsHolomorphic(Op::Sign));
    CHECK(!opIsHolomorphic(Op::Mod));
}

FCG_TEST(complex_built_from_holomorphic_ops) {
    Op bad = Op::COUNT;
    CHECK(builtFromHolomorphicOps(P("z^2 + 1"), &bad));
    CHECK(builtFromHolomorphicOps(P("exp(z)*sin(z)"), &bad));
    CHECK(builtFromHolomorphicOps(P("1/(z - 1)"), &bad));

    bad = Op::COUNT;
    CHECK(!builtFromHolomorphicOps(P("conj(z)"), &bad));
    CHECK(bad == Op::Conj);
    bad = Op::COUNT;
    CHECK(!builtFromHolomorphicOps(P("abs(z)^2"), &bad));
    CHECK(bad == Op::Abs);
    bad = Op::COUNT;
    CHECK(!builtFromHolomorphicOps(P("z*conj(z)"), &bad));
    CHECK(bad == Op::Conj);
    bad = Op::COUNT;
    CHECK(!builtFromHolomorphicOps(P("Re(z) + Im(z)"), &bad));
    CHECK(bad == Op::Re);  // 返回前序第一个非全纯算子
}

FCG_TEST(complex_cauchy_riemann_residual) {
    ComplexRegion d = ComplexRegion::disk(Cplx(0, 0), 1.0);
    CHECK(cauchyRiemannResidual(P("z^2"), "z", d, 32) < 1e-6);
    CHECK(cauchyRiemannResidual(P("exp(z)"), "z", d, 32) < 1e-5);
    CHECK(cauchyRiemannResidual(P("sin(z)"), "z", d, 32) < 1e-5);
    CHECK(cauchyRiemannResidual(P("1/(z + 3)"), "z", d, 32) < 1e-5);
    // 非全纯：残差量级 O(1)
    CHECK(cauchyRiemannResidual(P("conj(z)"), "z", d, 32) > 1e-2);
    CHECK(cauchyRiemannResidual(P("Re(z)"), "z", d, 32) > 1e-2);
    CHECK(cauchyRiemannResidual(P("z*conj(z)"), "z", d, 32) > 1e-2);
    CHECK(cauchyRiemannResidual(P("abs(z)"), "z", d, 32) > 1e-2);
}

FCG_TEST(complex_holomorphic_true_cases) {
    ComplexRegion d = ComplexRegion::disk(Cplx(0, 0), 1.0);
    CHECK(isHolomorphicOn(P("z^2"), "z", d).ok);
    CHECK(isHolomorphicOn(P("z^3 - 2z + 1"), "z", d).ok);
    CHECK(isHolomorphicOn(P("exp(z)"), "z", d).ok);
    CHECK(isHolomorphicOn(P("sin(z)"), "z", d).ok);
    CHECK(isHolomorphicOn(P("cos(z)*exp(z)"), "z", d).ok);
    // 极点在区域外 → 区域内全纯
    CHECK(isHolomorphicOn(P("1/(z - 3)"), "z", d).ok);
    // 1/z 在圆环 1<|z|<2 上全纯（0 不在区域里）
    CHECK(isHolomorphicOn(P("1/z"), "z", ComplexRegion::annulus(Cplx(0, 0), 1.0, 2.0)).ok);
    // 常数函数
    CHECK(isHolomorphicOn(P("1 + 2"), "z", d).ok);
}

FCG_TEST(complex_holomorphic_pole_case) {
    ComplexRegion d = ComplexRegion::disk(Cplx(0, 0), 1.0);
    auto r = isHolomorphicOn(P("1/z"), "z", d);
    CHECK(!r.ok);
    CHECK(r.reason.find("极点") != std::string::npos);
    auto r2 = isHolomorphicOn(P("1/(z - 1)"), "z", d);  // 极点在边界上，不在开圆盘内
    CHECK(r2.ok);
    auto r3 = isHolomorphicOn(P("1/(z*z + 1)"), "z", d);  // 极点 ±i 在边界上
    CHECK(r3.ok);
}

FCG_TEST(complex_holomorphic_nonanalytic_cases) {
    ComplexRegion d = ComplexRegion::disk(Cplx(0, 0), 1.0);
    auto r1 = isHolomorphicOn(P("conj(z)"), "z", d);
    CHECK(!r1.ok);
    CHECK(r1.reason.find("conj") != std::string::npos);

    auto r2 = isHolomorphicOn(P("abs(z)^2"), "z", d);
    CHECK(!r2.ok);
    CHECK(r2.reason.find("abs") != std::string::npos);

    auto r3 = isHolomorphicOn(P("Re(z)"), "z", d);
    CHECK(!r3.ok);

    auto r4 = isHolomorphicOn(P("z*conj(z)"), "z", d);
    CHECK(!r4.ok);
    CHECK(r4.reason.find("conj") != std::string::npos);
}

FCG_TEST(complex_holomorphic_removable_singularity) {
    // sin(z)/z 在 0 处是可去奇点，解析延拓后全纯
    ComplexRegion d = ComplexRegion::disk(Cplx(0, 0), 1.0);
    auto r = isHolomorphicOn(P("sin(z)/z"), "z", d);
    CHECK(r.ok);
    // 但 singularitiesIn 依然会报告这个可去奇点
    auto sing = singularitiesIn(P("sin(z)/z"), "z", d);
    CHECK_EQ(sing.size(), size_t(1));
    if (!sing.empty()) {
        CHECK(sing[0].z.abs() < 1e-9);
        CHECK_EQ(sing[0].poleOrder, 0);
        CHECK(!sing[0].essential);
    }
}

FCG_TEST(complex_holomorphic_branch_cut) {
    // ln(z) 只用全纯算子，且柯西-黎曼方程在割线两侧各自都成立；
    // 但区域跨过负实轴（分支割线）时函数不连续 → 不是区域上的解析函数
    CHECK(builtFromHolomorphicOps(P("ln(z)"), nullptr));
    ComplexRegion right = ComplexRegion::disk(Cplx(1, 0), 0.5);  // 不跨割线
    CHECK(isHolomorphicOn(P("ln(z)"), "z", right).ok);
    CHECK(isHolomorphicOn(P("sqrt(z)"), "z", right).ok);
    ComplexRegion cross = ComplexRegion::rectangle(-2.0, -0.5, -1.0, 1.0);  // 跨过负实轴
    auto r = isHolomorphicOn(P("ln(z)"), "z", cross);
    CHECK(!r.ok);
    CHECK(r.reason.find("割线") != std::string::npos);
    auto r2 = isHolomorphicOn(P("sqrt(z)"), "z", cross);
    CHECK(!r2.ok);
}

// =================================================================
// 3) 奇点 / 极点阶数
// =================================================================
FCG_TEST(complex_singularity_simple_pole) {
    ComplexRegion d = ComplexRegion::disk(Cplx(0, 0), 2.0);
    auto s = singularitiesIn(P("1/z"), "z", d);
    CHECK_EQ(s.size(), size_t(1));
    if (!s.empty()) {
        CHECK(s[0].z.abs() < 1e-9);
        CHECK_EQ(s[0].poleOrder, 1);
        CHECK(!s[0].essential);
    }
}

FCG_TEST(complex_singularity_pole_orders) {
    ComplexRegion d = ComplexRegion::disk(Cplx(0, 0), 2.0);
    auto s2 = singularitiesIn(P("1/z^2"), "z", d);
    CHECK_EQ(s2.size(), size_t(1));
    if (!s2.empty()) CHECK_EQ(s2[0].poleOrder, 2);

    auto s3 = singularitiesIn(P("1/z^3"), "z", d);
    CHECK_EQ(s3.size(), size_t(1));
    if (!s3.empty()) {
        CHECK_EQ(s3[0].poleOrder, 3);
        CHECK(s3[0].z.abs() < 1e-9);
    }

    // 分母是乘积时仍正确
    auto s4 = singularitiesIn(P("1/(z*(z - 1)^2)"), "z", ComplexRegion::disk(Cplx(0, 0), 3.0));
    CHECK_EQ(s4.size(), size_t(2));
    if (s4.size() == 2) {
        // 按实部排序：0 在前，1 在后
        CHECK(s4[0].z.abs() < 1e-9);
        CHECK_EQ(s4[0].poleOrder, 1);
        CHECK_NEAR(s4[1].z.re, 1.0, 1e-9);
        CHECK_EQ(s4[1].poleOrder, 2);
    }
}

FCG_TEST(complex_singularity_two_poles_and_outside) {
    auto s = singularitiesIn(P("1/(z - 1) + 1/(z + 1)"), "z",
                             ComplexRegion::disk(Cplx(0, 0), 2.0));
    CHECK_EQ(s.size(), size_t(2));
    if (s.size() == 2) {
        CHECK_NEAR(s[0].z.re, -1.0, 1e-8);
        CHECK_NEAR(s[1].z.re, 1.0, 1e-8);
        CHECK_EQ(s[0].poleOrder, 1);
        CHECK_EQ(s[1].poleOrder, 1);
    }
    // 极点不在区域里 → 空
    auto s2 = singularitiesIn(P("1/(z - 3)"), "z", ComplexRegion::disk(Cplx(0, 0), 1.0));
    CHECK(s2.empty());
    // 极点在边界上不算"区域内"
    auto s3 = singularitiesIn(P("1/(z - 1)"), "z", ComplexRegion::disk(Cplx(0, 0), 1.0));
    CHECK(s3.empty());
}

FCG_TEST(complex_singularity_removable_and_essential) {
    ComplexRegion d = ComplexRegion::disk(Cplx(0, 0), 1.0);
    // 可去奇点：sin(z)/z
    auto r = singularitiesIn(P("sin(z)/z"), "z", d);
    CHECK_EQ(r.size(), size_t(1));
    if (!r.empty()) CHECK_EQ(r[0].poleOrder, 0);
    // 另一类可去：z^2/z = z
    auto r2 = singularitiesIn(P("z^2/z"), "z", d);
    CHECK_EQ(r2.size(), size_t(1));
    if (!r2.empty()) {
        CHECK_EQ(r2[0].poleOrder, 0);
        CHECK(!r2[0].essential);
    }
    // 本质奇点：exp(1/z)
    auto r3 = singularitiesIn(P("exp(1/z)"), "z", d);
    CHECK_EQ(r3.size(), size_t(1));
    if (!r3.empty()) {
        CHECK(r3[0].essential);
        CHECK_EQ(r3[0].poleOrder, -1);
    }
}

FCG_TEST(complex_singularity_tan_poles) {
    // tan(z) 的极点是 cos(z) 的零点：±π/2 在 |z|<2 内
    auto s = singularitiesIn(P("tan(z)"), "z", ComplexRegion::disk(Cplx(0, 0), 2.0));
    CHECK_EQ(s.size(), size_t(2));
    if (s.size() == 2) {
        CHECK_NEAR(std::fabs(s[0].z.re), 1.5707963267948966, 1e-6);
        CHECK_NEAR(std::fabs(s[1].z.re), 1.5707963267948966, 1e-6);
        CHECK(std::fabs(s[0].z.im) < 1e-6);
        CHECK(std::fabs(s[1].z.im) < 1e-6);
        CHECK_EQ(s[0].poleOrder, 1);
        CHECK_EQ(s[1].poleOrder, 1);
    }
}

// =================================================================
// 4) 留数
// =================================================================
FCG_TEST(complex_residue_classic) {
    auto r1 = residueAt(P("1/z"), "z", Cplx(0, 0));
    CHECK(r1.ok);
    expectNearC(r1.value, Cplx(1.0, 0.0), 1e-9, "Res(1/z, 0)");

    auto r2 = residueAt(P("1/z^2"), "z", Cplx(0, 0));
    CHECK(r2.ok);
    CHECK(r2.value.abs() < 1e-8);

    auto r3 = residueAt(P("1/(z - 1)"), "z", Cplx(1, 0));
    CHECK(r3.ok);
    expectNearC(r3.value, Cplx(1.0, 0.0), 1e-9, "Res(1/(z-1), 1)");

    auto r4 = residueAt(P("(z + 1)/(z - 1)"), "z", Cplx(1, 0));
    CHECK(r4.ok);
    expectNearC(r4.value, Cplx(2.0, 0.0), 1e-9, "Res((z+1)/(z-1), 1)");

    auto r5 = residueAt(P("exp(z)/z"), "z", Cplx(0, 0));
    CHECK(r5.ok);
    expectNearC(r5.value, Cplx(1.0, 0.0), 1e-9, "Res(e^z/z, 0)");
}

FCG_TEST(complex_residue_more) {
    // Res(1/(z^2+1), i) = 1/(2i) = -i/2
    auto r1 = residueAt(P("1/(z^2 + 1)"), "z", Cplx(0, 1));
    CHECK(r1.ok);
    expectNearC(r1.value, Cplx(0.0, -0.5), 1e-8, "Res(1/(z^2+1), i)");

    // 解析点处的"留数"为 0
    auto r2 = residueAt(P("z^2"), "z", Cplx(1, 0));
    CHECK(r2.ok);
    CHECK(r2.value.abs() < 1e-8);

    // 1/z^3 的留数（z^(-1) 系数）为 0
    auto r3 = residueAt(P("1/z^3"), "z", Cplx(0, 0));
    CHECK(r3.ok);
    CHECK(r3.value.abs() < 1e-8);

    // 2/z 的留数 = 2
    auto r4 = residueAt(P("2/z"), "z", Cplx(0, 0));
    CHECK(r4.ok);
    expectNearC(r4.value, Cplx(2.0, 0.0), 1e-9, "Res(2/z, 0)");
}

// =================================================================
// 5) 围道积分
// =================================================================
FCG_TEST(complex_contour_integral_classic) {
    auto i1 = contourIntegral(P("1/z"), "z", Cplx(0, 0), 2.0);
    CHECK(i1.ok);
    expectNearC(i1.value, Cplx(0.0, 2.0 * 3.14159265358979323846), 1e-7, "∮1/z dz");

    auto i2 = contourIntegral(P("1/z^2"), "z", Cplx(0, 0), 2.0);
    CHECK(i2.ok);
    CHECK(i2.value.abs() < 1e-7);

    auto i3 = contourIntegral(P("1/(z - 1)"), "z", Cplx(0, 0), 2.0);
    CHECK(i3.ok);
    expectNearC(i3.value, Cplx(0.0, 2.0 * 3.14159265358979323846), 1e-7, "∮1/(z-1) dz");
}

FCG_TEST(complex_contour_integral_more) {
    // 全纯函数沿闭围道积分为 0
    auto i1 = contourIntegral(P("z^3"), "z", Cplx(1, 1), 1.5);
    CHECK(i1.ok);
    CHECK(i1.value.abs() < 1e-8);

    // 2/z 沿 |z-0|=3 积分 = 4πi
    auto i2 = contourIntegral(P("2/z"), "z", Cplx(0, 0), 3.0);
    CHECK(i2.ok);
    expectNearC(i2.value, Cplx(0.0, 4.0 * 3.14159265358979323846), 1e-7, "∮2/z dz");

    // 围道内有极点才贡献：1/(z-1) 沿 |z-0|=0.5（极点在外）积分 = 0
    auto i3 = contourIntegral(P("1/(z - 1)"), "z", Cplx(0, 0), 0.5);
    CHECK(i3.ok);
    CHECK(i3.value.abs() < 1e-7);

    // 围道上有极点 → error
    auto bad = contourIntegral(P("1/(z - 1)"), "z", Cplx(0, 0), 1.0);
    CHECK(!bad.ok);
    CHECK(bad.error.find("围道") != std::string::npos);
}

// =================================================================
// 6) 零点计数（辐角原理）
// =================================================================
FCG_TEST(complex_zero_count_polynomials) {
    auto r1 = zeroCountIn(P("(z - 1)*(z - 2)"), "z", ComplexRegion::disk(Cplx(0, 0), 3.0));
    CHECK(r1.ok);
    CHECK_EQ(r1.value, 2);

    auto r2 = zeroCountIn(P("z^2 - 1"), "z", ComplexRegion::disk(Cplx(0, 0), 2.0));
    CHECK(r2.ok);
    CHECK_EQ(r2.value, 2);

    // 重数计入：z^3 有 3 个零点（0 是三重零点）
    auto r3 = zeroCountIn(P("z^3"), "z", ComplexRegion::disk(Cplx(0, 0), 2.0));
    CHECK(r3.ok);
    CHECK_EQ(r3.value, 3);
}

FCG_TEST(complex_zero_count_transcendental) {
    auto r1 = zeroCountIn(P("sin(z)"), "z", ComplexRegion::disk(Cplx(0, 0), 1.5));
    CHECK(r1.ok);
    CHECK_EQ(r1.value, 1);

    // exp(z) 没有零点
    auto r2 = zeroCountIn(P("exp(z)"), "z", ComplexRegion::disk(Cplx(0, 0), 1.0));
    CHECK(r2.ok);
    CHECK_EQ(r2.value, 0);

    // sin(z) 在 |z|<4 内有 0, ±π 三个零点
    auto r3 = zeroCountIn(P("sin(z)"), "z", ComplexRegion::disk(Cplx(0, 0), 4.0));
    CHECK(r3.ok);
    CHECK_EQ(r3.value, 3);
}

FCG_TEST(complex_zero_count_with_poles) {
    // (z-1)/z 在 |z|<2：1 个零点、1 个极点，辐角原理 N-P = 0，故 N = 1
    auto r1 = zeroCountIn(P("(z - 1)/z"), "z", ComplexRegion::disk(Cplx(0, 0), 2.0));
    CHECK(r1.ok);
    CHECK_EQ(r1.value, 1);

    // 1/z 在 |z|<2：没有零点
    auto r2 = zeroCountIn(P("1/z"), "z", ComplexRegion::disk(Cplx(0, 0), 2.0));
    CHECK(r2.ok);
    CHECK_EQ(r2.value, 0);
}

FCG_TEST(complex_zero_count_regions) {
    // 圆环 0.5<|z|<2：z^2-1 的两个零点 ±1 都在环内
    auto r1 = zeroCountIn(P("z^2 - 1"), "z",
                          ComplexRegion::annulus(Cplx(0, 0), 0.5, 2.0));
    CHECK(r1.ok);
    CHECK_EQ(r1.value, 2);

    // 矩形 (-2,2)x(-2,2)：(z-1)(z+1) 两个零点
    auto r2 = zeroCountIn(P("(z - 1)*(z + 1)"), "z",
                          ComplexRegion::rectangle(-2.0, 2.0, -2.0, 2.0));
    CHECK(r2.ok);
    CHECK_EQ(r2.value, 2);

    // 上半平面：z^2+1 的两个零点是 ±i，只有 i 在上半平面
    ComplexRegion up;
    up.kind = ComplexRegion::Kind::HalfPlaneUpper;
    auto r3 = zeroCountIn(P("z^2 + 1"), "z", up);
    CHECK(r3.ok);
    CHECK_EQ(r3.value, 1);
}

FCG_TEST(complex_zero_count_on_contour_error) {
    // z^2-4 的零点是 ±2，正好在 |z|=2 上 → 必须返回 error，不能给错误数字
    auto r = zeroCountIn(P("z^2 - 4"), "z", ComplexRegion::disk(Cplx(0, 0), 2.0));
    CHECK(!r.ok);
    CHECK(!r.error.empty());

    // 围道上有极点同样 error
    auto r2 = zeroCountIn(P("1/(z - 2)"), "z", ComplexRegion::disk(Cplx(0, 0), 2.0));
    CHECK(!r2.ok);

    // 非全纯函数不能做辐角原理
    auto r3 = zeroCountIn(P("conj(z)"), "z", ComplexRegion::disk(Cplx(0, 0), 1.0));
    CHECK(!r3.ok);
}

// =================================================================
// 7) |f| 范围
// =================================================================
FCG_TEST(complex_modulus_range_disk) {
    auto r = modulusRangeOn(P("z^2"), "z", ComplexRegion::disk(Cplx(0, 0), 1.0));
    CHECK(r.ok);
    CHECK(r.lo < 1e-6);        // 0 处取到 0
    CHECK_NEAR(r.hi, 1.0, 1e-6);  // |z|=1 处取到 1
}

FCG_TEST(complex_modulus_range_annulus) {
    // 1/z 在 1<|z|<2 上 |f| ∈ (0.5, 1]（含边界时下确界 0.5、上确界 1）
    auto r = modulusRangeOn(P("1/z"), "z", ComplexRegion::annulus(Cplx(0, 0), 1.0, 2.0));
    CHECK(r.ok);
    CHECK_NEAR(r.lo, 0.5, 1e-3);
    CHECK_NEAR(r.hi, 1.0, 1e-3);
}

FCG_TEST(complex_modulus_range_unbounded) {
    // 区域内有极点 → |f| 无界 → error
    auto r = modulusRangeOn(P("1/z"), "z", ComplexRegion::disk(Cplx(0, 0), 1.0));
    CHECK(!r.ok);
    CHECK(r.error.find("无界") != std::string::npos || r.error.find("奇点") != std::string::npos);

    // 常数函数
    auto r2 = modulusRangeOn(P("3"), "z", ComplexRegion::disk(Cplx(0, 0), 1.0));
    CHECK(r2.ok);
    CHECK_NEAR(r2.lo, 3.0, 1e-9);
    CHECK_NEAR(r2.hi, 3.0, 1e-9);
}

// =================================================================
// 8) 单点求值 / 复算子检测 / 性能
// =================================================================
FCG_TEST(complex_eval_point) {
    auto r1 = evalComplex(P("z^2 + 1"), "z", Cplx(0, 1));
    CHECK(r1.ok);
    expectNearC(r1.value, Cplx(0.0, 0.0), 1e-9, "f(i)");

    auto r2 = evalComplex(P("1/z"), "z", Cplx(0, 1));
    CHECK(r2.ok);
    expectNearC(r2.value, Cplx(0.0, -1.0), 1e-9, "1/i");

    auto r3 = evalComplex(P("z"), "z", Cplx(0, 1));
    CHECK(r3.ok);
    expectNearC(r3.value, Cplx(0.0, 1.0), 1e-12, "f(i)=i");

    // 求值失败（1/0）
    auto r4 = evalComplex(P("1/z"), "z", Cplx(0, 0));
    CHECK(!r4.ok);
    CHECK(!r4.error.empty());
}

FCG_TEST(complex_uses_complex_ops) {
    CHECK(usesComplexOps(P("i*z")));
    CHECK(usesComplexOps(P("conj(z)")));
    CHECK(usesComplexOps(P("Re(z)")));
    CHECK(usesComplexOps(P("Im(z)")));
    CHECK(usesComplexOps(P("arg(z)")));
    CHECK(usesComplexOps(P("sqrt(-1)")));  // 常量结果是复数
    CHECK(!usesComplexOps(P("1 + 2")));
    CHECK(!usesComplexOps(P("2")));
    // 按头文件约定"对复变量求值后非实数"也算复数表达式：
    // z*z+1 在 z = 0.6+0.8i 处等于 -0.28+0.96i（非实数）→ true
    CHECK(usesComplexOps(P("z*z + 1")));
}

FCG_TEST(complex_performance_budget) {
    // 目标：单次判定 < 50ms。这里跑 10 次取平均，给宽松上限避免慢机器误报
    ExprP f = P("z^2 + exp(z)");
    ComplexRegion d = ComplexRegion::disk(Cplx(0, 0), 1.0);
    CHECK(isHolomorphicOn(f, "z", d).ok);

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) {
        auto r = isHolomorphicOn(f, "z", d);
        CHECK(r.ok);
    }
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 500.0);  // 10 次共 500ms → 平均 < 50ms

    // 零点计数目标 < 200ms
    auto t1 = std::chrono::steady_clock::now();
    auto zc = zeroCountIn(P("sin(z)"), "z", ComplexRegion::disk(Cplx(0, 0), 1.5));
    auto ms2 =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    CHECK(zc.ok);
    CHECK_EQ(zc.value, 1);
    CHECK(ms2 < 200.0);
}
