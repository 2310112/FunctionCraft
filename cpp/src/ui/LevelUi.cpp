// LevelUi.cpp — 约束描述 / 数值格式化 / 内置兜底关卡
#include "LevelUi.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Expr.h"

namespace fcg {
namespace ui {

// ---------------------------------------------------------------
// 数字 / 区间格式化
// ---------------------------------------------------------------
namespace {
void TrimZeros(std::string* s) {
    if (s->find('.') == std::string::npos) return;
    while (!s->empty() && s->back() == '0') s->pop_back();
    if (!s->empty() && s->back() == '.') s->pop_back();
}
}  // namespace

std::string FormatNumber(double v) {
    if (std::isnan(v)) return "?";
    if (std::isinf(v)) return v > 0 ? "+∞" : "-∞";
    if (std::fabs(v) < 1e-12) return "0";

    // π 的有理倍数：π, 2π, π/2, 3π/4 ...
    const double pi = 3.14159265358979323846;
    double r = v / pi;
    static const int dens[] = {1, 2, 3, 4, 6};
    for (int d : dens) {
        double n = r * d;
        if (std::fabs(n - std::round(n)) < 1e-9) {
            int ni = static_cast<int>(std::lround(n));
            if (ni == 0) break;
            std::string num;
            if (std::abs(ni) == 1)
                num = "";
            else
                num = std::to_string(std::abs(ni));
            std::string s;
            if (ni < 0) s += "-";
            s += num + "π";
            if (d != 1) s += "/" + std::to_string(d);
            return s;
        }
    }

    char buf[64];
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) {
        std::snprintf(buf, sizeof(buf), "%.0f", v);
        return buf;
    }
    if (std::fabs(v) >= 1000.0 || std::fabs(v) < 0.001) {
        std::snprintf(buf, sizeof(buf), "%.4g", v);
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s(buf);
    TrimZeros(&s);
    return s;
}

std::string FormatInterval(const Interval& iv) {
    std::string s;
    s += iv.loInf ? "(" : (iv.loClosed ? "[" : "(");
    s += iv.loInf ? "-∞" : FormatNumber(iv.lo);
    s += ", ";
    s += iv.hiInf ? "+∞" : FormatNumber(iv.hi);
    s += iv.hiInf ? ")" : (iv.hiClosed ? "]" : ")");
    return s;
}

std::string FormatRange(double lo, double hi) {
    return "[" + FormatNumber(lo) + ", " + FormatNumber(hi) + "]";
}

std::string FormatComplex(const Cplx& z) {
    const double eps = 1e-9;
    bool hasRe = std::fabs(z.re) > eps;
    bool hasIm = std::fabs(z.im) > eps;
    if (!hasRe && !hasIm) return "0";

    auto fmtImag = [](double v) -> std::string {
        // 1i / -1i 简化为 i / -i
        if (std::fabs(std::fabs(v) - 1.0) < 1e-9) return v < 0 ? "-i" : "i";
        return FormatNumber(v) + "i";
    };

    if (!hasIm) return FormatNumber(z.re);
    if (!hasRe) return fmtImag(z.im);
    std::string imPart = fmtImag(z.im);
    if (z.im > 0) return FormatNumber(z.re) + "+" + imPart;
    return FormatNumber(z.re) + imPart;  // im 负数自带 '-'，FormatNumber 会输出 -x
}

std::string FormatRegion(const ComplexRegion& r) {
    using K = ComplexRegion::Kind;
    switch (r.kind) {
        case K::Disk: {
            if (std::fabs(r.center.re) < 1e-9 && std::fabs(r.center.im) < 1e-9)
                return "|z| < " + FormatNumber(r.radius);
            return "|z - (" + FormatComplex(r.center) + ")| < " + FormatNumber(r.radius);
        }
        case K::Annulus: {
            std::string ring = FormatNumber(r.radius) + " < |z";
            if (std::fabs(r.center.re) > 1e-9 || std::fabs(r.center.im) > 1e-9)
                ring += " - (" + FormatComplex(r.center) + ")";
            ring += "| < " + FormatNumber(r.outerRadius);
            return ring;
        }
        case K::Rectangle:
            return "Re z ∈ " + FormatRange(r.x0, r.x1) + " 且 Im z ∈ " + FormatRange(r.y0, r.y1);
        case K::HalfPlaneUpper: return "上半平面 Im z > " + FormatNumber(r.yLine);
        case K::HalfPlaneLower: return "下半平面 Im z < " + FormatNumber(r.yLine);
        case K::HalfPlaneRight: return "右半平面 Re z > " + FormatNumber(r.xLine);
        case K::ExteriorOfDisk: {
            if (std::fabs(r.center.re) < 1e-9 && std::fabs(r.center.im) < 1e-9)
                return "|z| > " + FormatNumber(r.radius);
            return "|z - (" + FormatComplex(r.center) + ")| > " + FormatNumber(r.radius);
        }
        case K::Plane: return "整个复平面";
        default: return "复平面区域";
    }
}

// ---------------------------------------------------------------
// 约束描述
// ---------------------------------------------------------------
std::string ConstraintDesc(const Constraint& c) {
    if (!c.label.empty()) return c.label;
    std::string d = c.describe();
    if (!d.empty()) return d;

    switch (c.kind) {
        case ConstraintKind::AllowedFunctions: {
            std::string s = "只能使用函数库：";
            bool first = true;
            for (Op op : c.allowedOps) {
                if (!first) s += "、";
                s += opDisplay(op);
                first = false;
            }
            if (first) s += "（基础）";
            return s;
        }
        case ConstraintKind::DomainOn: return "在 " + FormatInterval(c.interval) + " 上处处有定义";
        case ConstraintKind::AnalyticOn: return "在 " + FormatInterval(c.interval) + " 内解析";
        case ConstraintKind::BoundaryValue:
            return "f(" + FormatNumber(c.point) + ") = " + FormatNumber(c.value);
        case ConstraintKind::RangeOn:
            return "在 " + FormatInterval(c.interval) + " 上 f(x) ∈ " + FormatRange(c.lo, c.hi);
        case ConstraintKind::PositiveOn:
            return "在 " + FormatInterval(c.interval) + " 上 f(x) > 0";
        case ConstraintKind::Monotonic:
            return "在 " + FormatInterval(c.interval) + " 上" +
                   (c.increasing ? "单调递增" : "单调递减") + (c.strict ? "（严格）" : "");
        case ConstraintKind::Parity:
            return c.even ? "是偶函数：f(-x) = f(x)" : "是奇函数：f(-x) = -f(x)";
        case ConstraintKind::Periodic:
            return "是周期函数，周期为 " + FormatNumber(c.value);
        case ConstraintKind::Asymptote: {
            const char* dir = c.asymptoteSign > 0 ? "+∞" : (c.asymptoteSign < 0 ? "-∞" : "±∞");
            return std::string("x→") + dir + " 时 y→" + FormatNumber(c.value) +
                   "（水平渐近线）";
        }
        case ConstraintKind::Extremum:
            return "在 x = " + FormatNumber(c.point) + " 处取" + (c.isMax ? "极大" : "极小") +
                   "值 " + FormatNumber(c.value);
        case ConstraintKind::SymmetryAxis:
            return "图像关于直线 x = " + FormatNumber(c.point) + " 对称";
        case ConstraintKind::ZeroCount:
            return "在 " + FormatInterval(c.interval) + " 内恰有 " +
                   std::to_string(c.count) + " 个零点";
        case ConstraintKind::ComplexityLimit:
            return "函数调用次数不超过 " + std::to_string(c.maxFuncCalls) + " 次";
        case ConstraintKind::PeriodicSymmetry:
            return "图像关于点 (" + FormatNumber(c.point) + ", " + FormatNumber(c.value) +
                   ") 中心对称";

        // ---------- 复变约束（str()/describe 未就绪时的兜底） ----------
        case ConstraintKind::HolomorphicOn:
            return "在 " + FormatRegion(c.region) + " 内解析（全纯）";
        case ConstraintKind::NoSingularityIn:
            return "在 " + FormatRegion(c.region) + " 内没有奇点";
        case ConstraintKind::PoleOrderAt:
            return "f(z) 在 z = " + FormatComplex(c.cpoint) + " 处是 " +
                   std::to_string(c.order) + " 阶极点";
        case ConstraintKind::ResidueAt:
            return "留数 Res(f, " + FormatComplex(c.cpoint) + ") = " + FormatComplex(c.cvalue);
        case ConstraintKind::ContourIntegralValue: {
            std::string path = "|z";
            if (std::fabs(c.region.center.re) > 1e-9 || std::fabs(c.region.center.im) > 1e-9)
                path += " - (" + FormatComplex(c.region.center) + ")";
            path += "| = " + FormatNumber(c.region.radius);
            return "围道积分 ∮_{" + path + "} f dz = " + FormatComplex(c.cvalue);
        }
        case ConstraintKind::ZeroCountInRegion:
            return "在 " + FormatRegion(c.region) + " 内恰有 " + std::to_string(c.count) +
                   " 个零点";
        case ConstraintKind::ModulusRangeOn:
            return "在 " + FormatRegion(c.region) + " 内 |f(z)| ∈ " + FormatRange(c.lo, c.hi);
        case ConstraintKind::ComplexBoundaryValue:
            return "f(" + FormatComplex(c.cpoint) + ") = " + FormatComplex(c.cvalue);
        default: return "未知约束";
    }
}

// ---------------------------------------------------------------
// 兜底关卡
// ---------------------------------------------------------------
namespace {

constexpr double kPi = 3.14159265358979323846;

Constraint MakeC(ConstraintKind kind, const char* label) {
    Constraint c;
    c.kind = kind;
    c.label = label ? label : "";
    return c;
}

LevelDef MakeLevel(const char* id, const char* title, int chapter, const char* intro,
                   const char* hint, const char* reference, double parScore) {
    LevelDef d;
    d.id = id;
    d.title = title;
    d.chapter = chapter;
    d.intro = intro ? intro : "";
    d.hint = hint ? hint : "";
    d.reference = reference ? reference : "";
    d.parScore = parScore;
    d.timeLimit = 0.0;
    return d;
}

}  // namespace

std::vector<LevelEntry> MakeFallbackLevels() {
    std::vector<LevelEntry> out;
    auto add = [&](LevelDef d) {
        LevelEntry e;
        e.def = std::move(d);
        out.push_back(std::move(e));
    };

    // ---------- 第一章：基础 ----------
    {
        LevelDef d = MakeLevel("lv-1-1", "初识正弦", 1,
                               "函数图像是约束的镜子。先用最熟悉的 sin(x) 感受一下奇函数。",
                               "sin(x) 是奇函数，并且在 [-π, π] 上处处有定义，f(0) = 0。",
                               "sin(x)", 80);
        d.constraints.push_back(MakeC(ConstraintKind::Parity, "是奇函数：f(-x) = -f(x)"));
        d.constraints.back().even = false;
        d.constraints.push_back(MakeC(ConstraintKind::BoundaryValue, "f(0) = 0"));
        d.constraints.back().point = 0;
        d.constraints.back().value = 0;
        d.constraints.push_back(MakeC(ConstraintKind::DomainOn, "在 [-π, π] 上处处有定义"));
        d.constraints.back().interval = Interval::closed(-kPi, kPi);
        add(d);
    }
    {
        LevelDef d = MakeLevel("lv-1-2", "偶函数之约", 1,
                               "把左右对称的函数找出来，别忘了顶点处的取值。",
                               "cos(x) 是偶函数，并且 cos(0) = 1。",
                               "cos(x)", 80);
        d.constraints.push_back(MakeC(ConstraintKind::Parity, "是偶函数：f(-x) = f(x)"));
        d.constraints.back().even = true;
        d.constraints.push_back(MakeC(ConstraintKind::BoundaryValue, "f(0) = 1"));
        d.constraints.back().point = 0;
        d.constraints.back().value = 1;
        add(d);
    }
    {
        LevelDef d = MakeLevel("lv-1-3", "峰值的约定", 1,
                               "奇偶性之外，再约定一个点上的取值。",
                               "sin(x) 是奇函数，且在 π/2 处取得 1。",
                               "sin(x)", 82);
        d.constraints.push_back(MakeC(ConstraintKind::Parity, "是奇函数：f(-x) = -f(x)"));
        d.constraints.back().even = false;
        d.constraints.push_back(MakeC(ConstraintKind::BoundaryValue, "f(π/2) = 1"));
        d.constraints.back().point = kPi / 2;
        d.constraints.back().value = 1;
        add(d);
    }

    // ---------- 第二章：组合 ----------
    {
        LevelDef d = MakeLevel("lv-2-1", "单调的函数", 2,
                               "一次函数是最好用的积木：两点确定一条直线。",
                               "f(x) = x/(2π) 在 [0, 2π] 上从 0 一路升到 1。",
                               "x/(2*pi)", 85);
        d.constraints.push_back(MakeC(ConstraintKind::Monotonic, "在 [0, 2π] 上严格单调递增"));
        d.constraints.back().interval = Interval::closed(0, 2 * kPi);
        d.constraints.back().increasing = true;
        d.constraints.back().strict = true;
        d.constraints.push_back(MakeC(ConstraintKind::BoundaryValue, "f(0) = 0"));
        d.constraints.back().point = 0;
        d.constraints.back().value = 0;
        d.constraints.push_back(MakeC(ConstraintKind::BoundaryValue, "f(2π) = 1"));
        d.constraints.back().point = 2 * kPi;
        d.constraints.back().value = 1;
        add(d);
    }
    {
        LevelDef d = MakeLevel("lv-2-2", "值域的控制", 2,
                               "值域是函数能触碰到的高度范围。",
                               "把 sin 的输入缩小一半：sin(x/2) 在 [0, π] 上恰好从 0 升到 1。",
                               "sin(x/2)", 86);
        d.constraints.push_back(MakeC(ConstraintKind::RangeOn, "在 [0, π] 上 f(x) ∈ [0, 1]"));
        d.constraints.back().interval = Interval::closed(0, kPi);
        d.constraints.back().lo = 0;
        d.constraints.back().hi = 1;
        d.constraints.push_back(MakeC(ConstraintKind::BoundaryValue, "f(0) = 0"));
        d.constraints.back().point = 0;
        d.constraints.back().value = 0;
        d.constraints.push_back(MakeC(ConstraintKind::BoundaryValue, "f(π) = 1"));
        d.constraints.back().point = kPi;
        d.constraints.back().value = 1;
        d.constraints.push_back(MakeC(ConstraintKind::Monotonic, "在 [0, π] 上单调递增"));
        d.constraints.back().interval = Interval::closed(0, kPi);
        d.constraints.back().increasing = true;
        add(d);
    }
    {
        LevelDef d = MakeLevel("lv-2-3", "周期之美", 2,
                               "周期函数会不断重复自己，偶函数还会左右对称。",
                               "cos(x) 的周期是 2π，是偶函数，并且 cos(0) = 1。",
                               "cos(x)", 88);
        d.constraints.push_back(MakeC(ConstraintKind::Periodic, "是周期函数，周期为 2π"));
        d.constraints.back().value = 2 * kPi;
        d.constraints.push_back(MakeC(ConstraintKind::Parity, "是偶函数：f(-x) = f(x)"));
        d.constraints.back().even = true;
        d.constraints.push_back(MakeC(ConstraintKind::BoundaryValue, "f(0) = 1"));
        d.constraints.back().point = 0;
        d.constraints.back().value = 1;
        add(d);
    }

    // ---------- 第三章：进阶 ----------
    {
        LevelDef d = MakeLevel("lv-3-1", "解析入门", 3,
                               "解析性意味着函数光滑、没有尖角与断点。多项式是最安全的答案。",
                               "f(x) = x 在 (-1, 1) 内处处解析，且在 [-1, 1] 上单调。",
                               "x", 90);
        d.constraints.push_back(MakeC(ConstraintKind::AnalyticOn, "在 (-1, 1) 内解析"));
        d.constraints.back().interval = Interval::open(-1, 1);
        d.constraints.push_back(MakeC(ConstraintKind::BoundaryValue, "f(0) = 0"));
        d.constraints.back().point = 0;
        d.constraints.back().value = 0;
        d.constraints.push_back(MakeC(ConstraintKind::RangeOn, "在 [-1, 1] 上 f(x) ∈ [-1, 1]"));
        d.constraints.back().interval = Interval::closed(-1, 1);
        d.constraints.back().lo = -1;
        d.constraints.back().hi = 1;
        d.constraints.push_back(MakeC(ConstraintKind::Monotonic, "在 [-1, 1] 上单调递增"));
        d.constraints.back().interval = Interval::closed(-1, 1);
        d.constraints.back().increasing = true;
        add(d);
    }
    {
        LevelDef d = MakeLevel("lv-3-2", "渐近之形", 3,
                               "指数函数会把变化推向无穷远的方向，也把曲线拉向一条水平线。",
                               "f(x) = 1 - exp(-x) 在 x→+∞ 时趋近 1，且始终为正。",
                               "1-exp(-x)", 92);
        d.constraints.push_back(
            MakeC(ConstraintKind::Asymptote, "x→+∞ 时 y→1（水平渐近线）"));
        d.constraints.back().value = 1;
        d.constraints.back().asymptoteSign = 1;
        d.constraints.push_back(MakeC(ConstraintKind::PositiveOn, "在 (0, +∞) 上 f(x) > 0"));
        d.constraints.back().interval = Interval::open(0, 1e308);
        d.constraints.back().interval.hiInf = true;
        d.constraints.push_back(MakeC(ConstraintKind::DomainOn, "在 [-10, 10] 上处处有定义"));
        d.constraints.back().interval = Interval::closed(-10, 10);
        add(d);
    }
    {
        LevelDef d = MakeLevel("lv-3-3", "大师之约", 3,
                               "把所有要求叠加在一起：对称、解析、正值、两端归于零。",
                               "高斯钟形 f(x) = exp(-x²) 是偶函数、处处解析，在 ±∞ 处趋于 0。",
                               "exp(-x*x)", 95);
        d.constraints.push_back(MakeC(ConstraintKind::AnalyticOn, "在 (-3, 3) 内解析"));
        d.constraints.back().interval = Interval::open(-3, 3);
        d.constraints.push_back(MakeC(ConstraintKind::Parity, "是偶函数：f(-x) = f(x)"));
        d.constraints.back().even = true;
        d.constraints.push_back(MakeC(ConstraintKind::BoundaryValue, "f(0) = 1"));
        d.constraints.back().point = 0;
        d.constraints.back().value = 1;
        d.constraints.push_back(MakeC(ConstraintKind::PositiveOn, "在 (-3, 3) 上 f(x) > 0"));
        d.constraints.back().interval = Interval::open(-3, 3);
        d.constraints.push_back(MakeC(ConstraintKind::Asymptote, "x→±∞ 时 y→0"));
        d.constraints.back().value = 0;
        d.constraints.back().asymptoteSign = 0;
        add(d);
    }

    return out;
}

LevelDef MakeComplexTestLevel(const std::string& idHint) {
    LevelDef d;
    d.domain = DomainKind::Complex;
    d.vars = {"z"};
    d.parScore = 85.0;
    d.chapter = 5;

    const bool zerosLevel = (idHint.find("5-2") != std::string::npos ||
                             idHint.find("zero") != std::string::npos);
    const bool residueLevel = (idHint.find("6-") != std::string::npos ||
                               idHint.find("residue") != std::string::npos);

    if (zerosLevel) {
        d.id = "test-c-zeros";
        d.title = "零点星芒";
        d.reference = "z*z - 1";
        d.intro = "复平面着色图中，零点处色相环绕一周，会形成四射的星芒。";
        d.hint = "试一个在单位圆附近有两个零点的多项式，例如 z*z - 1。";
        d.chapter = 5;
        Constraint c;
        c.kind = ConstraintKind::ZeroCountInRegion;
        c.region = ComplexRegion::disk(Cplx(0.0, 0.0), 1.5);
        c.count = 2;
        c.var = "z";
        c.label = "在 |z| < 1.5 内恰有 2 个零点";
        d.constraints.push_back(c);
        Constraint c2;
        c2.kind = ConstraintKind::ComplexBoundaryValue;
        c2.cpoint = Cplx(1.0, 0.0);
        c2.cvalue = Cplx(0.0, 0.0);
        c2.var = "z";
        c2.label = "f(1) = 0";
        d.constraints.push_back(c2);
    } else if (residueLevel) {
        d.id = "test-c-residue";
        d.title = "留数与积分";
        d.reference = "1/(z*z + 1)";
        d.intro = "极点处色相绕一圈、明度骤亮，形成漩涡；围道积分可由留数算出。";
        d.hint = "试试 1/(z*z+1)：它在 ±i 各有一个一阶极点。";
        d.chapter = 6;
        Constraint c;
        c.kind = ConstraintKind::ResidueAt;
        c.cpoint = Cplx(0.0, 1.0);
        c.cvalue = Cplx(0.0, -0.5);
        c.var = "z";
        c.label = "留数 Res(f, i) = -i/2";
        d.constraints.push_back(c);
        Constraint c2;
        c2.kind = ConstraintKind::ContourIntegralValue;
        c2.region = ComplexRegion::disk(Cplx(0.0, 0.0), 2.0);
        c2.cvalue = Cplx(0.0, 0.0);
        c2.var = "z";
        c2.label = "围道积分 ∮_{|z| = 2} f dz = 0";
        d.constraints.push_back(c2);
        Constraint c3;
        c3.kind = ConstraintKind::HolomorphicOn;
        c3.region = ComplexRegion::rectangle(-2.0, 2.0, -2.0, 2.0);
        c3.var = "z";
        c3.label = "在矩形 Re z ∈ [-2, 2] 且 Im z ∈ [-2, 2] 内除极点外解析";
        d.constraints.push_back(c3);
    } else {
        d.id = "test-c-pole";
        d.title = "复平面初探";
        d.reference = "1/(z - 1)";
        d.intro = "复平面上的着色彩图：色相表示相位，明度表示模的大小。";
        d.hint = "试试 1/(z - 1)：它在 z = 1 处有一个一阶极点，会形成漩涡。";
        d.chapter = 5;
        Constraint c;
        c.kind = ConstraintKind::ResidueAt;
        c.cpoint = Cplx(1.0, 0.0);
        c.cvalue = Cplx(1.0, 0.0);
        c.var = "z";
        c.label = "留数 Res(f, 1) = 1";
        d.constraints.push_back(c);
        Constraint c2;
        c2.kind = ConstraintKind::ComplexBoundaryValue;
        c2.cpoint = Cplx(0.0, 0.0);
        c2.cvalue = Cplx(-1.0, 0.0);
        c2.var = "z";
        c2.label = "f(0) = -1";
        d.constraints.push_back(c2);
        Constraint c3;
        c3.kind = ConstraintKind::PoleOrderAt;
        c3.cpoint = Cplx(1.0, 0.0);
        c3.order = 1;
        c3.var = "z";
        c3.label = "在 z = 1 处是一阶极点";
        d.constraints.push_back(c3);
    }
    return d;
}

LevelDef MakeEndlessFallbackLevel(int index, const std::vector<LevelDef>& pool) {
    if (index < 1) index = 1;
    LevelDef d;
    if (pool.empty()) {
        d = MakeLevel("endless", "无尽试炼", 1, "", "满足全部约束即可进入下一关。", "sin(x)",
                      90);
    } else {
        d = pool[static_cast<size_t>((index - 1) % pool.size())];
    }
    d.id = "endless-" + std::to_string(index);
    d.title = "无尽试炼 · 第 " + std::to_string(index) + " 关";
    d.chapter = 1 + (index - 1) / 10;
    d.intro = "无尽模式：满足全部约束即可推进。每 10 关会遭遇 Boss 挑战。";
    d.timeLimit = 0.0;
    d.parScore = 85.0;

    // 难度递增：高关数追加复杂度限制
    if (index >= 6) {
        Constraint c = MakeC(ConstraintKind::ComplexityLimit, "");
        c.maxFuncCalls = std::max(2, 4 - std::min(2, index / 12));
        d.constraints.push_back(c);
    }
    if (index % 10 == 0) {
        // Boss 关：额外加点对称约束
        Constraint c = MakeC(ConstraintKind::SymmetryAxis, "图像关于 y 轴对称");
        c.point = 0.0;
        d.constraints.push_back(c);
    }
    return d;
}

const char* FallbackDifficultyLabel(int index) {
    if (index <= 10) return "入门";
    if (index <= 20) return "熟练";
    if (index <= 30) return "进阶";
    if (index <= 40) return "困难";
    if (index <= 50) return "专家";
    return "大师";
}

bool FallbackIsBoss(int index) { return index > 0 && index % 10 == 0; }

int GradeRank(char g) {
    switch (g) {
        case 'S': return 5;
        case 'A': return 4;
        case 'B': return 3;
        case 'C': return 2;
        case 'D': return 1;
        default: return 0;
    }
}

bool GradeBetter(char a, char b) { return GradeRank(a) > GradeRank(b); }

}  // namespace ui
}  // namespace fcg
