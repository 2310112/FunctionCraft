// Constraint.cpp — 约束描述与逐项验证
//
// 说明：本文件是"玩法系统"的核心判定层。它依赖符号引擎的
//   Domain.h / Limit.h / Simplify.h / Diff.h 接口，但在那些模块尚未
//   完成（桩）时也提供独立的数值兜底路径，保证单文件可测、可自检：
//     * BoundaryValue / PositiveOn / Parity / Periodic / SymmetryAxis /
//       PointSymmetry / ComplexityLimit / AllowedFunctions —— 不依赖桩；
//     * DomainOn / AnalyticOn —— 以引擎结果为主，辅以数值采样；
//     * RangeOn / Monotonic / ZeroCount / Asymptote / Extremum —— 引擎
//       优先，失败时使用数值兜底，并统一走同一套判据保证"生成器自检"
//       与"玩家提交判定"行为一致。
#include "Constraint.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Diff.h"
#include "Domain.h"
#include "Limit.h"
#include "Numeric.h"
#include "Simplify.h"

namespace fcg {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// -----------------------------------------------------------------
// 数值格式化（约束描述用）：把 π 的整数倍写成 "π/2" 这类形式
// -----------------------------------------------------------------
std::string fmtPlain(double v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "+∞" : "-∞";
    if (v == 0.0) return "0";
    // 双精度浮点噪声：显示为 ≈0（如 sin(π) 算出 -2.4e-16）
    if (std::fabs(v) < 1e-9) return "≈0";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    std::string s(buf);
    if (s == "-0") s = "0";
    return s;
}

std::string fmtNum(double v) {
    if (!std::isfinite(v)) return fmtPlain(v);
    if (std::fabs(v) > 1e-12) {
        // 按分母 1/2/3/4/6 识别 π 的有理倍数
        for (int den : {1, 2, 3, 4, 6}) {
            const double unit = kPi / den;
            const double q = v / unit;
            const double rq = std::round(q);
            if (std::fabs(q - rq) <= 1e-9 && std::fabs(rq) <= 24.0) {
                int n = static_cast<int>(rq);
                int d = den;
                if (n == 0) return "0";
                const int g = std::gcd(std::abs(n), d);
                n /= g;
                d /= g;
                if (d == 1) {
                    if (n == 1) return "π";
                    if (n == -1) return "-π";
                    return std::to_string(n) + "π";
                }
                std::string num = n == 1 ? "" : (n == -1 ? "-" : std::to_string(n));
                return num + "π/" + std::to_string(d);
            }
        }
    }
    return fmtPlain(v);
}

std::string fmtInterval(const Interval& iv) {
    const std::string lo = iv.loInf ? "-∞" : fmtNum(iv.lo);
    const std::string hi = iv.hiInf ? "+∞" : fmtNum(iv.hi);
    return std::string(iv.loClosed ? "[" : "(") + lo + ", " + hi + (iv.hiClosed ? "]" : ")");
}

std::string fmtPoint(double x) {
    return "x = " + fmtNum(x);
}

// -----------------------------------------------------------------
// 复数 / 复平面区域格式化（描述用）
//
// 约定：与 ComplexRegion::str() 解耦，保证「约束面板的中文描述」在底层
// 复变分析模块尚未落地时也稳定可测。风格示例：
//   0.5、1+2i、-i、2πi、πi/2
//   |z| < 1、1 < |z| < 2、|z - 1| > 3、上半平面
// -----------------------------------------------------------------
bool cplxIsZero(Cplx v, double eps = 1e-12) {
    return std::fabs(v.re) <= eps && std::fabs(v.im) <= eps;
}

// 虚部（±非零）→ "i"、"-i"、"2i"、"2πi"、"3πi/4"
std::string fmtImagPart(double im) {
    const double a = std::fabs(im);
    std::string mag;
    if (std::fabs(a - 1.0) <= 1e-12) {
        mag = "i";
    } else {
        const std::string n = fmtNum(a);  // "2" / "π" / "2π" / "π/2" / "3π/4"
        const size_t slash = n.find('/');
        static const std::string kPiUtf8 = "\xCF\x80";  // UTF-8 的 π（U+03C0）
        const bool piNumer = slash != std::string::npos && slash >= kPiUtf8.size() &&
                             n.compare(slash - kPiUtf8.size(), kPiUtf8.size(), kPiUtf8) == 0;
        if (piNumer) {
            // 把 "π/2" 写成 "πi/2"，"3π/4" 写成 "3πi/4"
            mag = n.substr(0, slash) + "i" + n.substr(slash);
        } else {
            mag = n + "i";
        }
    }
    return im < 0.0 ? "-" + mag : mag;
}

std::string fmtComplex(Cplx v) {
    const bool reZero = std::fabs(v.re) <= 1e-12;
    const bool imZero = std::fabs(v.im) <= 1e-12;
    if (reZero && imZero) return "0";
    if (imZero) return fmtNum(v.re);
    if (reZero) return fmtImagPart(v.im);
    // 实部 + 虚部：1+2i、1-2i、π/2+i
    return fmtNum(v.re) + (v.im > 0.0 ? "+" : "-") + fmtImagPart(std::fabs(v.im));
}

// |z| 或 |z - z0|
std::string fmtDiskTerm(Cplx center) {
    if (cplxIsZero(center)) return "|z|";
    constexpr double kEps = 1e-12;
    const bool reZero = std::fabs(center.re) <= kEps;
    const bool imZero = std::fabs(center.im) <= kEps;
    if (imZero) {  // 实中心：|z - 2| / |z + 2|
        return center.re >= 0.0 ? "|z - " + fmtNum(center.re) + "|"
                                : "|z + " + fmtNum(-center.re) + "|";
    }
    if (reZero) {  // 纯虚中心：|z - i| / |z + i|
        return center.im >= 0.0 ? "|z - " + fmtImagPart(center.im) + "|"
                                : "|z + " + fmtImagPart(-center.im) + "|";
    }
    return "|z - (" + fmtComplex(center) + ")|";
}

std::string fmtRegion(const ComplexRegion& reg) {
    switch (reg.kind) {
        case ComplexRegion::Kind::Disk:
            return fmtDiskTerm(reg.center) + " < " + fmtNum(reg.radius);
        case ComplexRegion::Kind::Annulus:
            return fmtNum(reg.radius) + " < " + fmtDiskTerm(reg.center) + " < " +
                   fmtNum(reg.outerRadius);
        case ComplexRegion::Kind::Rectangle:
            return fmtNum(reg.x0) + " < Re z < " + fmtNum(reg.x1) + " 且 " + fmtNum(reg.y0) +
                   " < Im z < " + fmtNum(reg.y1);
        case ComplexRegion::Kind::HalfPlaneUpper:
            if (std::fabs(reg.yLine) <= 1e-12) return "上半平面";
            return "Im z > " + fmtNum(reg.yLine);
        case ComplexRegion::Kind::HalfPlaneLower:
            if (std::fabs(reg.yLine) <= 1e-12) return "下半平面";
            return "Im z < " + fmtNum(reg.yLine);
        case ComplexRegion::Kind::HalfPlaneRight:
            if (std::fabs(reg.xLine) <= 1e-12) return "右半平面";
            return "Re z > " + fmtNum(reg.xLine);
        case ComplexRegion::Kind::ExteriorOfDisk:
            return fmtDiskTerm(reg.center) + " > " + fmtNum(reg.radius);
        case ComplexRegion::Kind::Plane:
            return "整个复平面";
    }
    return "未知区域";
}

// 区域前缀：「在 |z| < 1 上」/「在上半平面上」——中文区域名前后不加空格更自然
std::string fmtInRegion(const ComplexRegion& reg) {
    const std::string s = fmtRegion(reg);
    const bool cjk = !s.empty() && static_cast<unsigned char>(s[0]) >= 0xE0;
    return cjk ? ("在" + s + "上") : ("在 " + s + " 上");
}

// 「在 |z| < 1 内」/「在上半平面内」
std::string fmtInsideRegion(const ComplexRegion& reg) {
    const std::string s = fmtRegion(reg);
    const bool cjk = !s.empty() && static_cast<unsigned char>(s[0]) >= 0xE0;
    return cjk ? ("在" + s + "内") : ("在 " + s + " 内");
}

// 相对误差 |got-want| / max(1, |want|)
double cplxRelError(Cplx got, Cplx want) {
    const double scale = std::max(1.0, want.abs());
    const double err = (got - want).abs() / scale;
    if (!std::isfinite(err)) return std::numeric_limits<double>::infinity();
    return err;
}

// -----------------------------------------------------------------
// 基础工具
// -----------------------------------------------------------------
bool evalFinite(const ExprP& f, const std::string& var, double x, double* out) {
    if (!f) return false;
    const RealResult r = evalReal(f, var, x);
    if (!r.ok || !std::isfinite(r.value)) return false;
    if (out) *out = r.value;
    return true;
}

// 有限区间（两端都有限）→ 取出上下界；否则返回 false
bool finiteBounds(const Interval& iv, double* a, double* b) {
    if (iv.loInf || iv.hiInf) return false;
    if (!(iv.hi > iv.lo)) return false;
    *a = iv.lo;
    *b = iv.hi;
    return true;
}

// 用于恒等式判定（奇偶性/周期性/对称性）的一组采样点
const std::vector<double>& identitySamples() {
    static const std::vector<double> xs = {
        -3.07, -2.71, -2.39, -1.93, -1.57, -1.13, -0.71, -0.31, 0.0,
        0.17,  0.41,  0.73,  1.09,  1.49,  1.87,  2.31,  2.71,  3.07};
    return xs;
}

// 相对容差下的"接近零"
bool nearZeroDelta(double delta, double scaleA, double scaleB) {
    const double tol = 1e-8 * (1.0 + std::fabs(scaleA) + std::fabs(scaleB));
    return std::fabs(delta) <= tol;
}

std::string joinStrings(const std::vector<std::string>& items, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += sep;
        out += items[i];
    }
    return out;
}

// 约束里 Op 的可读标签（用于中文描述与"可用函数"提示）
std::string opLabel(Op op) {
    switch (op) {
        case Op::Neg: return "负号(-)";
        case Op::Sub: return "减法(-)";
        case Op::Mul: return "×";
        case Op::Div: return "÷";
        case Op::Pow: return "^";
        case Op::Mod: return "mod";
        default: return opDisplay(op);
    }
}

std::string describeAllowedOps(const std::vector<Op>& ops) {
    // 常见情况：四则运算齐全时合并成一句，避免列表冗长
    std::set<int> present;
    for (Op op : ops) present.insert(static_cast<int>(op));
    const bool basicArithmetic = present.count(static_cast<int>(Op::Add)) &&
                                 present.count(static_cast<int>(Op::Sub)) &&
                                 present.count(static_cast<int>(Op::Mul)) &&
                                 present.count(static_cast<int>(Op::Div)) &&
                                 present.count(static_cast<int>(Op::Pow));
    std::vector<std::string> labels;
    if (basicArithmetic) labels.push_back("四则运算（+ - × ÷ ^）");
    for (Op op : ops) {
        if (basicArithmetic && (op == Op::Add || op == Op::Sub || op == Op::Mul ||
                                op == Op::Div || op == Op::Pow))
            continue;
        const std::string l = opLabel(op);
        if (std::find(labels.begin(), labels.end(), l) == labels.end()) labels.push_back(l);
    }
    if (labels.empty()) return "不限";
    return joinStrings(labels, "、");
}

// 判断表达式是否只由解析函数构成（结构化检查，不依赖 Domain.cpp）
bool checkAnalyticOps(const ExprP& f, Op* firstBad) {
    bool ok = true;
    visit(f, [&](const ExprP& n) {
        if (!ok || !n) return;
        if (!opIsAnalytic(n->op)) {
            ok = false;
            if (firstBad) *firstBad = n->op;
        }
    });
    return ok;
}

// -----------------------------------------------------------------
// 数值兜底：单调性
// -----------------------------------------------------------------
struct MonoProbe {
    bool known = false;       // 是否得出结论
    bool monotonic = false;   // 是否单调
    bool increasing = false;  // 单调递增？
    bool strictly = false;    // 严格单调？
    double witness = kNaN;
    std::string reason;
};

MonoProbe numericMonotonic(const ExprP& f, const std::string& var, const Interval& iv) {
    MonoProbe r;
    double a = -10.0, b = 10.0;
    finiteBounds(iv, &a, &b);
    if (!(b > a)) {
        r.known = true;
        r.monotonic = false;
        r.reason = "区间为空";
        return r;
    }
    const int n = 512;
    std::vector<double> vs(static_cast<size_t>(n) + 1);
    double maxAbs = 0.0;
    for (int i = 0; i <= n; ++i) {
        const double x = a + (b - a) * static_cast<double>(i) / n;
        double v = 0.0;
        if (!evalFinite(f, var, x, &v)) {
            r.known = true;
            r.monotonic = false;
            r.witness = x;
            r.reason = "在 " + fmtPoint(x) + " 处无法求值";
            return r;
        }
        vs[static_cast<size_t>(i)] = v;
        maxAbs = std::max(maxAbs, std::fabs(v));
    }
    const double tol = 1e-9 * (1.0 + maxAbs);
    int pos = 0, neg = 0, flat = 0;
    double witness = kNaN;
    for (int i = 0; i < n; ++i) {
        const double d = vs[static_cast<size_t>(i) + 1] - vs[static_cast<size_t>(i)];
        if (d > tol) {
            ++pos;
        } else if (d < -tol) {
            ++neg;
            if (std::isnan(witness)) witness = a + (b - a) * static_cast<double>(i) / n;
        } else {
            ++flat;
        }
    }
    const bool incOk = (neg == 0);
    const bool decOk = (pos == 0);
    r.known = true;
    if (incOk && decOk) {
        // 常数函数：对递增/递减（非严格）都成立
        r.monotonic = true;
        r.increasing = true;
        r.strictly = false;
        return r;
    }
    if (!incOk && !decOk) {
        r.monotonic = false;
        r.witness = witness;
        r.reason = "在 x ≈ " + fmtNum(witness) + " 附近函数增减方向发生变化，并非单调";
        return r;
    }
    r.monotonic = true;
    r.increasing = incOk;
    r.strictly = (flat == 0);
    return r;
}

// -----------------------------------------------------------------
// 数值兜底：取值区间
// -----------------------------------------------------------------
struct RangeProbe {
    bool ok = false;
    double lo = 0.0;
    double hi = 0.0;
    double badX = kNaN;
    std::string error;
};

RangeProbe numericRange(const ExprP& f, const std::string& var, const Interval& iv) {
    RangeProbe r;
    double a = -10.0, b = 10.0;
    const bool finite = finiteBounds(iv, &a, &b);
    if (!finite) {
        a = -10.0;
        b = 10.0;
    }
    const int n = 512;
    int okCount = 0;
    double lo = 0.0, hi = 0.0;
    for (int i = 0; i <= n; ++i) {
        const double x = a + (b - a) * static_cast<double>(i) / n;
        if (i == 0 && !iv.loClosed && finite) continue;
        if (i == n && !iv.hiClosed && finite) continue;
        double v = 0.0;
        if (!evalFinite(f, var, x, &v)) {
            r.badX = x;
            r.error = "在 " + fmtPoint(x) + " 处无法求值";
            return r;
        }
        if (okCount == 0) {
            lo = hi = v;
        } else {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        ++okCount;
    }
    if (okCount < 8) {
        r.error = "区间内可求值的采样点太少";
        return r;
    }
    r.ok = true;
    r.lo = lo;
    r.hi = hi;
    return r;
}

// -----------------------------------------------------------------
// 数值兜底：零点
// -----------------------------------------------------------------
void addZeroCandidate(std::vector<double>& zeros, double z, double span) {
    const double tol = 1e-7 * std::max(1.0, span);
    for (double& e : zeros) {
        if (std::fabs(e - z) <= tol) return;
    }
    zeros.push_back(z);
}

double bisectZero(const ExprP& f, const std::string& var, double lo, double hi, double flo,
                  double fhi) {
    double a = lo, b = hi, fa = flo, fb = fhi;
    (void)fb;
    for (int it = 0; it < 80; ++it) {
        const double mid = 0.5 * (a + b);
        double fm = 0.0;
        if (!evalFinite(f, var, mid, &fm)) break;
        if ((fa <= 0.0 && fm <= 0.0) || (fa >= 0.0 && fm >= 0.0)) {
            a = mid;
            fa = fm;
        } else {
            b = mid;
        }
        if (std::fabs(b - a) <= 1e-14 * std::max(1.0, std::fabs(mid))) break;
    }
    return 0.5 * (a + b);
}

std::vector<double> numericZeros(const ExprP& f, const std::string& var, const Interval& iv) {
    std::vector<double> zeros;
    double a = -10.0, b = 10.0;
    const bool finite = finiteBounds(iv, &a, &b);
    if (!finite) {
        a = -10.0;
        b = 10.0;
    }
    const int n = 512;
    std::vector<std::pair<double, double>> pts;
    pts.reserve(static_cast<size_t>(n) + 1);
    double maxAbs = 0.0;
    for (int i = 0; i <= n; ++i) {
        const double x = a + (b - a) * static_cast<double>(i) / n;
        if (finite && i == 0 && !iv.loClosed) continue;
        if (finite && i == n && !iv.hiClosed) continue;
        double v = 0.0;
        if (!evalFinite(f, var, x, &v)) continue;
        pts.emplace_back(x, v);
        maxAbs = std::max(maxAbs, std::fabs(v));
    }
    const double nearTol = 1e-12 * (1.0 + maxAbs);
    for (const auto& p : pts) {
        if (std::fabs(p.second) <= nearTol) addZeroCandidate(zeros, p.first, b - a);
    }
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const double v1 = pts[i].second, v2 = pts[i + 1].second;
        if ((v1 < 0.0 && v2 > 0.0) || (v1 > 0.0 && v2 < 0.0)) {
            const double z = bisectZero(f, var, pts[i].first, pts[i + 1].first, v1, v2);
            addZeroCandidate(zeros, z, b - a);
        }
    }
    std::sort(zeros.begin(), zeros.end());
    return zeros;
}

// -----------------------------------------------------------------
// 数值兜底：x→±∞ 极限（水平渐近线）
// -----------------------------------------------------------------
bool numericLimitAtInfinity(const ExprP& f, const std::string& var, int sign, double* out) {
    static const double ms[] = {1e3, 1e4, 1e5, 1e6};
    double v[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        if (!evalFinite(f, var, sign * ms[i], &v[i])) return false;
    }
    if (std::fabs(v[3] - v[2]) > 1e-3 * (1.0 + std::fabs(v[3]))) return false;
    if (std::fabs(v[2] - v[1]) > 1e-2 * (1.0 + std::fabs(v[2]))) return false;
    if (out) *out = v[3];
    return true;
}

// -----------------------------------------------------------------
// 恒等式判定：f(-x) 与 ±f(x)、f(x+T) 与 f(x)、镜像点
// -----------------------------------------------------------------
bool checkParityIdentity(const ExprP& f, const std::string& var, bool even,
                         std::string* detail) {
    const ExprP fNeg = substitute(f, var, mkNeg(mkVar(var)));
    const ExprP g = even ? mkSub(fNeg, f) : mkAdd(fNeg, f);
    if (g && symbolicallyEqual(g, mkNum(0.0))) return true;
    if (g && isZeroSimplified(simplify(g))) return true;

    int tested = 0;
    for (double x : identitySamples()) {
        double a = 0.0, b = 0.0;
        if (!evalFinite(fNeg, var, x, &a) || !evalFinite(f, var, x, &b)) continue;
        const double delta = even ? (a - b) : (a + b);
        if (!nearZeroDelta(delta, a, b)) {
            if (detail) {
                *detail = "在 " + fmtPoint(x) + " 处 f(" + fmtNum(-x) + ") = " + fmtNum(a) +
                          "，f(" + fmtNum(x) + ") = " + fmtNum(b) + "，" +
                          (even ? "不满足 f(-x) = f(x)" : "不满足 f(-x) = -f(x)");
            }
            return false;
        }
        ++tested;
    }
    if (tested < 4) {
        if (detail) *detail = "无法在足够多的采样点上验证奇偶性";
        return false;
    }
    return true;
}

bool checkPeriodicIdentity(const ExprP& f, const std::string& var, double period,
                           std::string* detail) {
    const ExprP fShift = substitute(f, var, mkAdd(mkVar(var), mkNum(period)));
    const ExprP g = mkSub(fShift, f);
    if (g && symbolicallyEqual(g, mkNum(0.0))) return true;
    if (g && isZeroSimplified(simplify(g))) return true;

    int tested = 0;
    for (double x : identitySamples()) {
        double a = 0.0, b = 0.0;
        if (!evalFinite(fShift, var, x, &a) || !evalFinite(f, var, x, &b)) continue;
        if (!nearZeroDelta(a - b, a, b)) {
            if (detail) {
                *detail = "在 " + fmtPoint(x) + " 处 f(x + T) = " + fmtNum(a) +
                          "，f(x) = " + fmtNum(b) + "，两者不相等";
            }
            return false;
        }
        ++tested;
    }
    if (tested < 4) {
        if (detail) *detail = "无法在足够多的采样点上验证周期性";
        return false;
    }
    return true;
}

bool checkSymmetryIdentity(const ExprP& f, const std::string& var, double axis, double centerValue,
                           bool pointSymmetry, std::string* detail) {
    // 镜像表达式 f(2*axis - x)
    const ExprP mirror = substitute(f, var, mkSub(mkNum(2.0 * axis), mkVar(var)));
    // 快速符号路径
    const ExprP g = pointSymmetry ? mkSub(mkAdd(mirror, f), mkNum(2.0 * centerValue))
                                  : mkSub(mirror, f);
    if (g && symbolicallyEqual(g, mkNum(0.0))) return true;
    if (g && isZeroSimplified(simplify(g))) return true;

    int tested = 0;
    for (double x : identitySamples()) {
        double a = 0.0, b = 0.0;
        if (!evalFinite(mirror, var, x, &a) || !evalFinite(f, var, x, &b)) continue;
        const double delta =
            pointSymmetry ? (a + b - 2.0 * centerValue) : (a - b);
        const double tol =
            1e-8 * (1.0 + std::fabs(a) + std::fabs(b) + std::fabs(centerValue));
        if (std::fabs(delta) > tol) {
            if (detail) {
                *detail = "在 " + fmtPoint(x) + " 处 f(" + fmtNum(2.0 * axis - x) +
                          ") = " + fmtNum(a) + "，f(" + fmtNum(x) + ") = " + fmtNum(b) +
                          "，对称关系不成立";
            }
            return false;
        }
        ++tested;
    }
    if (tested < 4) {
        if (detail) *detail = "无法在足够多的采样点上验证对称性";
        return false;
    }
    return true;
}

}  // namespace

// -----------------------------------------------------------------
// 元信息
// -----------------------------------------------------------------
const char* constraintKindName(ConstraintKind k) {
    switch (k) {
        case ConstraintKind::AllowedFunctions: return "allowed_functions";
        case ConstraintKind::DomainOn: return "domain";
        case ConstraintKind::AnalyticOn: return "analytic";
        case ConstraintKind::BoundaryValue: return "boundary";
        case ConstraintKind::RangeOn: return "range";
        case ConstraintKind::PositiveOn: return "positive";
        case ConstraintKind::Monotonic: return "monotonic";
        case ConstraintKind::Parity: return "parity";
        case ConstraintKind::Periodic: return "periodic";
        case ConstraintKind::Asymptote: return "asymptote";
        case ConstraintKind::Extremum: return "extremum";
        case ConstraintKind::SymmetryAxis: return "symmetry_axis";
        case ConstraintKind::ZeroCount: return "zero_count";
        case ConstraintKind::ComplexityLimit: return "complexity_limit";
        case ConstraintKind::PeriodicSymmetry: return "point_symmetry";
        // ---------- 复变 ----------
        case ConstraintKind::HolomorphicOn: return "holomorphic";
        case ConstraintKind::NoSingularityIn: return "no_singularity";
        case ConstraintKind::PoleOrderAt: return "pole_order";
        case ConstraintKind::ResidueAt: return "residue";
        case ConstraintKind::ContourIntegralValue: return "contour_integral";
        case ConstraintKind::ZeroCountInRegion: return "zero_count_region";
        case ConstraintKind::ModulusRangeOn: return "modulus_range";
        case ConstraintKind::ComplexBoundaryValue: return "complex_value";
    }
    return "unknown";
}

const char* constraintKindDisplay(ConstraintKind k) {
    switch (k) {
        case ConstraintKind::AllowedFunctions: return "函数库限制";
        case ConstraintKind::DomainOn: return "定义域";
        case ConstraintKind::AnalyticOn: return "解析性";
        case ConstraintKind::BoundaryValue: return "边界值";
        case ConstraintKind::RangeOn: return "值域";
        case ConstraintKind::PositiveOn: return "恒正";
        case ConstraintKind::Monotonic: return "单调性";
        case ConstraintKind::Parity: return "奇偶性";
        case ConstraintKind::Periodic: return "周期性";
        case ConstraintKind::Asymptote: return "水平渐近线";
        case ConstraintKind::Extremum: return "极值";
        case ConstraintKind::SymmetryAxis: return "对称轴";
        case ConstraintKind::ZeroCount: return "零点个数";
        case ConstraintKind::ComplexityLimit: return "复杂度上限";
        case ConstraintKind::PeriodicSymmetry: return "中心对称";
        // ---------- 复变 ----------
        case ConstraintKind::HolomorphicOn: return "全纯性";
        case ConstraintKind::NoSingularityIn: return "无奇点";
        case ConstraintKind::PoleOrderAt: return "极点阶数";
        case ConstraintKind::ResidueAt: return "留数";
        case ConstraintKind::ContourIntegralValue: return "围道积分";
        case ConstraintKind::ZeroCountInRegion: return "区域内零点数";
        case ConstraintKind::ModulusRangeOn: return "模的范围";
        case ConstraintKind::ComplexBoundaryValue: return "复数点取值";
    }
    return "未知约束";
}

bool constraintKindSupported(ConstraintKind k) {
    switch (k) {
        case ConstraintKind::AllowedFunctions:
        case ConstraintKind::DomainOn:
        case ConstraintKind::AnalyticOn:
        case ConstraintKind::BoundaryValue:
        case ConstraintKind::RangeOn:
        case ConstraintKind::PositiveOn:
        case ConstraintKind::Monotonic:
        case ConstraintKind::Parity:
        case ConstraintKind::Periodic:
        case ConstraintKind::Asymptote:
        case ConstraintKind::Extremum:
        case ConstraintKind::SymmetryAxis:
        case ConstraintKind::ZeroCount:
        case ConstraintKind::ComplexityLimit:
        case ConstraintKind::PeriodicSymmetry:
            return true;
        // ---------- 复变（判定依赖 Complex.h 的全纯/留数/积分/零点分析） ----------
        case ConstraintKind::HolomorphicOn:
        case ConstraintKind::NoSingularityIn:
        case ConstraintKind::PoleOrderAt:
        case ConstraintKind::ResidueAt:
        case ConstraintKind::ContourIntegralValue:
        case ConstraintKind::ZeroCountInRegion:
        case ConstraintKind::ModulusRangeOn:
        case ConstraintKind::ComplexBoundaryValue:
            return true;
    }
    return false;
}

std::string Constraint::describe() const {
    if (!label.empty()) return label;
    switch (kind) {
        case ConstraintKind::AllowedFunctions:
            if (allowedOps.empty()) return "函数库：不限";
            return "只能使用：" + describeAllowedOps(allowedOps);
        case ConstraintKind::DomainOn:
            return "在 " + fmtInterval(interval) + " 上有定义";
        case ConstraintKind::AnalyticOn:
            return "在 " + fmtInterval(interval) + " 上解析";
        case ConstraintKind::BoundaryValue:
            return "f(" + fmtNum(point) + ") = " + fmtNum(value);
        case ConstraintKind::RangeOn:
            return "在 " + fmtInterval(interval) + " 上 f(x) ∈ [" + fmtNum(lo) + ", " +
                   fmtNum(hi) + "]";
        case ConstraintKind::PositiveOn:
            return "在 " + fmtInterval(interval) + " 上 f(x) > 0";
        case ConstraintKind::Monotonic:
            return "在 " + fmtInterval(interval) + " 上" + (strict ? "严格" : "") +
                   (increasing ? "单调递增" : "单调递减");
        case ConstraintKind::Parity:
            return even ? "偶函数：f(-x) = f(x)" : "奇函数：f(-x) = -f(x)";
        case ConstraintKind::Periodic:
            return "周期为 " + fmtNum(value);
        case ConstraintKind::Asymptote: {
            std::string side = "x→±∞";
            if (asymptoteSign > 0) side = "x→+∞";
            else if (asymptoteSign < 0) side = "x→-∞";
            return side + " 时水平渐近线 y = " + fmtNum(value);
        }
        case ConstraintKind::Extremum:
            return "在 x = " + fmtNum(point) + " 处取" + (isMax ? "极大值" : "极小值");
        case ConstraintKind::SymmetryAxis:
            return "关于直线 x = " + fmtNum(point) + " 对称";
        case ConstraintKind::ZeroCount:
            return "在 " + fmtInterval(interval) + " 内恰有 " + std::to_string(count) + " 个零点";
        case ConstraintKind::ComplexityLimit:
            return "函数调用数不超过 " + std::to_string(maxFuncCalls);
        case ConstraintKind::PeriodicSymmetry:
            return "关于点 (" + fmtNum(point) + ", " + fmtNum(value) + ") 中心对称";
        // ---------- 复变 ----------
        case ConstraintKind::HolomorphicOn:
            return fmtInRegion(region) + "解析";
        case ConstraintKind::NoSingularityIn:
            return fmtInsideRegion(region) + "无奇点";
        case ConstraintKind::PoleOrderAt:
            return "在 z = " + fmtComplex(cpoint) + " 处有 " + std::to_string(order) + " 阶极点";
        case ConstraintKind::ResidueAt:
            return "在 z = " + fmtComplex(cpoint) + " 处的留数为 " + fmtComplex(cvalue);
        case ConstraintKind::ContourIntegralValue:
            return "沿 " + fmtDiskTerm(cpoint) + " = " + fmtNum(radius) + " 逆时针积分等于 " +
                   fmtComplex(cvalue);
        case ConstraintKind::ZeroCountInRegion:
            return fmtInsideRegion(region) + "恰有 " + std::to_string(count) + " 个零点";
        case ConstraintKind::ModulusRangeOn:
            return "在 " + fmtRegion(region) + " 上 |f(z)| ∈ [" + fmtNum(lo) + ", " + fmtNum(hi) +
                   "]";
        case ConstraintKind::ComplexBoundaryValue:
            return "f(" + fmtComplex(cpoint) + ") = " + fmtComplex(cvalue);
    }
    return "未知约束";
}

// -----------------------------------------------------------------
// 可用函数校验
// -----------------------------------------------------------------
ConstraintReport checkAllowedFunctions(const std::vector<Op>& allowed, const ExprP& f) {
    ConstraintReport r;
    r.kind = ConstraintKind::AllowedFunctions;
    r.label = "只能使用给定函数";
    if (!f) {
        r.ok = false;
        r.detail = "函数表达式为空";
        return r;
    }
    if (allowed.empty()) {
        r.ok = true;
        return r;
    }
    std::set<int> as;
    for (Op op : allowed) as.insert(static_cast<int>(op));

    std::vector<std::string> bad;
    visit(f, [&](const ExprP& n) {
        if (!n) return;
        const Op op = n->op;
        if (op == Op::Num || op == Op::Var || op == Op::Pi || op == Op::E || op == Op::I)
            return;
        if (as.count(static_cast<int>(op))) return;
        const std::string d = opLabel(op);
        if (std::find(bad.begin(), bad.end(), d) == bad.end()) bad.push_back(d);
    });
    if (bad.empty()) {
        r.ok = true;
        return r;
    }
    r.ok = false;
    r.detail = "使用了不允许的 " + joinStrings(bad, "、") + "；本关只允许：" +
               describeAllowedOps(allowed);
    return r;
}

// -----------------------------------------------------------------
// 约束验证主体
// -----------------------------------------------------------------
ConstraintReport checkConstraint(const Constraint& c, const ExprP& f) {
    ConstraintReport r;
    r.kind = c.kind;
    r.label = c.describe();

    if (!f) {
        r.ok = false;
        r.detail = "函数表达式为空（无法解析）";
        return r;
    }
    if (!constraintKindSupported(c.kind)) {
        r.ok = false;
        r.detail = "暂不支持该约束类型：" + std::string(constraintKindDisplay(c.kind));
        return r;
    }

    switch (c.kind) {
        // ---------- 函数库 ----------
        case ConstraintKind::AllowedFunctions: {
            ConstraintReport ar = checkAllowedFunctions(c.allowedOps, f);
            ar.kind = c.kind;
            ar.label = r.label;
            ar.detail = ar.detail;
            return ar;
        }

        // ---------- 定义域 ----------
        case ConstraintKind::DomainOn: {
            const DomainSet dom = domainOf(f, c.var);
            const bool primaryOk = dom.containsAll(c.interval);
            // 数值复核：在区间（无限区间取 [-20,20]）上采样，检查是否处处可求值
            double a = -20.0, b = 20.0;
            finiteBounds(c.interval, &a, &b);
            const int n = 128;
            double bad = kNaN;
            for (int i = 0; i <= n; ++i) {
                const double x = a + (b - a) * static_cast<double>(i) / n;
                if (!evalFinite(f, c.var, x, nullptr)) {
                    bad = x;
                    break;
                }
            }
            if (!std::isnan(bad)) {
                r.ok = false;
                r.detail = "在 " + fmtPoint(bad) + " 处无定义（" + fmtInterval(c.interval) +
                           " 内存在不在定义域中的点）";
                return r;
            }
            if (!primaryOk) {
                r.ok = false;
                r.detail = "函数的定义域无法覆盖 " + fmtInterval(c.interval) + "（定义域：" +
                           dom.str() + "）";
                return r;
            }
            r.ok = true;
            return r;
        }

        // ---------- 解析性 ----------
        case ConstraintKind::AnalyticOn: {
            Op badOp = Op::COUNT;
            if (!checkAnalyticOps(f, &badOp)) {
                r.ok = false;
                r.detail = "包含非解析函数 " + std::string(opDisplay(badOp)) +
                           "（" + std::string(opDisplay(badOp)) +
                           " 不可导），因此不满足解析性要求";
                return r;
            }
            std::string reason;
            const bool ok = isAnalyticOn(f, c.interval, c.var, &reason);
            r.ok = ok;
            if (!ok) {
                r.detail = reason.empty() ? ("在 " + fmtInterval(c.interval) + " 内存在奇点，不解析")
                                          : reason;
            }
            return r;
        }

        // ---------- 边界值 ----------
        case ConstraintKind::BoundaryValue: {
            // 复数关卡（变量为 z）：取复数点/复数值。Level.cpp 优先把 JSON 的数组形式
            // point/value 填到 cpoint/cvalue；若只写了实数 point/value 则做兼容回退。
            if (c.var != "x") {
                Cplx z0 = c.cpoint;
                Cplx w0 = c.cvalue;
                if (cplxIsZero(z0) && cplxIsZero(w0) && (c.point != 0.0 || c.value != 0.0)) {
                    z0 = Cplx(c.point, 0.0);
                    w0 = Cplx(c.value, 0.0);
                }
                VarMap vars;
                vars[c.var] = z0;
                const EvalResult ev = evaluate(f, vars);
                if (!ev.ok || !ev.value.isFinite()) {
                    r.ok = false;
                    r.detail = "在 z = " + fmtComplex(z0) + " 处无定义，无法验证该点取值" +
                               (ev.error.empty() ? std::string() : "（" + ev.error + "）");
                    return r;
                }
                const double relErr = cplxRelError(ev.value, w0);
                r.ok = relErr <= 1e-6;
                if (!r.ok) {
                    r.detail = "f(" + fmtComplex(z0) + ") = " + fmtComplex(ev.value) + "，要求等于 " +
                               fmtComplex(w0) + "（相对误差 " + fmtNum(relErr) + "）";
                }
                return r;
            }
            double v = 0.0;
            if (!evalFinite(f, c.var, c.point, &v)) {
                r.ok = false;
                r.detail = "在 " + fmtPoint(c.point) + " 处无定义，无法验证边界值";
                return r;
            }
            const double scale = std::max(1.0, std::fabs(c.value));
            const bool numericOk = std::fabs(v - c.value) <= 1e-9 * scale;
            bool symbolicOk = false;
            if (!numericOk) {
                const ExprP at = substitute(f, c.var, mkNum(c.point));
                const ExprP simp = simplify(at);
                if (isNum(simp)) {
                    symbolicOk = std::fabs(asNum(simp) - c.value) <= 1e-12 * scale;
                }
            }
            r.ok = numericOk || symbolicOk;
            if (!r.ok) {
                r.detail = "f(" + fmtNum(c.point) + ") = " + fmtNum(v) + "，要求等于 " +
                           fmtNum(c.value) + "（相差 " + fmtNum(std::fabs(v - c.value)) + "）";
            }
            return r;
        }

        // ---------- 值域 ----------
        case ConstraintKind::RangeOn: {
            if (c.lo > c.hi) {
                r.ok = false;
                r.detail = "约束本身无效：下界大于上界";
                return r;
            }
            bool have = false;
            double lo = 0.0, hi = 0.0;
            std::string err;
            const RangeResult rr = rangeOn(f, c.interval, c.var);
            if (rr.ok) {
                have = true;
                lo = rr.lo;
                hi = rr.hi;
            } else {
                const RangeProbe p = numericRange(f, c.var, c.interval);
                if (p.ok) {
                    have = true;
                    lo = p.lo;
                    hi = p.hi;
                } else {
                    err = p.error.empty() ? rr.error : p.error;
                }
            }
            if (!have) {
                r.ok = false;
                r.detail = "无法判定值域" + (err.empty() ? std::string() : "（" + err + "）");
                return r;
            }
            const double tol = 1e-6 * (1.0 + std::fabs(c.lo) + std::fabs(c.hi));
            if (lo < c.lo - tol) {
                r.ok = false;
                r.detail = "在 " + fmtInterval(c.interval) + " 上最小值为 " + fmtNum(lo) +
                           "，小于下界 " + fmtNum(c.lo);
                return r;
            }
            if (hi > c.hi + tol) {
                r.ok = false;
                r.detail = "在 " + fmtInterval(c.interval) + " 上最大值为 " + fmtNum(hi) +
                           "，超过上界 " + fmtNum(c.hi);
                return r;
            }
            r.ok = true;
            return r;
        }

        // ---------- 恒正 ----------
        case ConstraintKind::PositiveOn: {
            double a = -10.0, b = 10.0;
            const bool finite = finiteBounds(c.interval, &a, &b);
            if (!finite) {
                a = -10.0;
                b = 10.0;
            }
            const int n = 512;
            double minV = 0.0, maxAbs = 0.0, witness = kNaN;
            for (int i = 0; i < n; ++i) {
                // 只取开区间内部点（约束语义为 (a,b) 上恒正）
                const double t = (static_cast<double>(i) + 0.5) / n;
                const double x = a + (b - a) * t;
                double v = 0.0;
                if (!evalFinite(f, c.var, x, &v)) {
                    r.ok = false;
                    r.detail = "在 " + fmtPoint(x) + " 处无定义，无法满足恒正";
                    return r;
                }
                if (i == 0 || v < minV) {
                    minV = v;
                    witness = x;
                }
                maxAbs = std::max(maxAbs, std::fabs(v));
            }
            const double tol = 1e-5 * (1.0 + maxAbs);
            if (minV <= tol) {
                r.ok = false;
                r.detail = "在 " + fmtPoint(witness) + " 处 f(x) = " + fmtNum(minV) +
                           "，没有严格大于 0";
                return r;
            }
            r.ok = true;
            return r;
        }

        // ---------- 单调性 ----------
        case ConstraintKind::Monotonic: {
            MonotonicResult mr = monotonicOn(f, c.interval, c.var);
            bool known = mr.ok;
            bool mono = mr.ok;
            bool inc = mr.increasing;
            bool strictly = mr.strictly;
            std::string why = mr.error;
            if (!known) {
                const MonoProbe p = numericMonotonic(f, c.var, c.interval);
                if (p.known) {
                    known = true;
                    mono = p.monotonic;
                    inc = p.increasing;
                    strictly = p.strictly;
                    why = p.reason;
                }
            }
            if (!known) {
                r.ok = false;
                r.detail = "无法判定单调性" + (why.empty() ? std::string() : "（" + why + "）");
                return r;
            }
            if (!mono) {
                r.ok = false;
                r.detail = why.empty()
                               ? ("在 " + fmtInterval(c.interval) + " 上并不单调")
                               : ("在 " + fmtInterval(c.interval) + " 上并不单调：" + why);
                return r;
            }
            if (inc != c.increasing) {
                r.ok = false;
                r.detail = "在 " + fmtInterval(c.interval) + " 上实际是" +
                           (inc ? "单调递增" : "单调递减") + "，要求" +
                           (c.increasing ? "单调递增" : "单调递减");
                return r;
            }
            if (c.strict && !strictly) {
                r.ok = false;
                r.detail = "在 " + fmtInterval(c.interval) + " 上只是单调（存在平坦段），要求严格单调";
                return r;
            }
            r.ok = true;
            return r;
        }

        // ---------- 奇偶性 ----------
        case ConstraintKind::Parity: {
            std::string detail;
            r.ok = checkParityIdentity(f, c.var, c.even, &detail);
            r.detail = detail;
            return r;
        }

        // ---------- 周期性 ----------
        case ConstraintKind::Periodic: {
            if (!(c.value > 0.0)) {
                r.ok = false;
                r.detail = "周期必须为正数";
                return r;
            }
            std::string detail;
            r.ok = checkPeriodicIdentity(f, c.var, c.value, &detail);
            r.detail = detail;
            return r;
        }

        // ---------- 水平渐近线 ----------
        case ConstraintKind::Asymptote: {
            const int signs[2] = {+1, -1};
            const int count = (c.asymptoteSign == 0) ? 2 : 1;
            double firstLimit = 0.0;
            bool haveFirst = false;
            for (int si = 0; si < count; ++si) {
                const int sgn = (c.asymptoteSign == 0) ? signs[si] : c.asymptoteSign;
                const std::string side = sgn > 0 ? "x→+∞" : "x→-∞";
                bool got = false;
                double lim = 0.0;
                double engineLim = 0.0;
                if (horizontalAsymptote(f, c.var, sgn, &engineLim)) {
                    got = true;
                    lim = engineLim;
                } else if (numericLimitAtInfinity(f, c.var, sgn, &lim)) {
                    got = true;
                }
                if (!got) {
                    r.ok = false;
                    r.detail = side + " 时函数不收敛到水平渐近线";
                    return r;
                }
                const double tol = 1e-4 * (1.0 + std::fabs(c.value));
                if (!std::isfinite(lim) || std::fabs(lim - c.value) > tol) {
                    r.ok = false;
                    r.detail = side + " 时极限为 " + fmtNum(lim) + "，要求水平渐近线 y = " +
                               fmtNum(c.value);
                    return r;
                }
                if (!haveFirst) {
                    firstLimit = lim;
                    haveFirst = true;
                } else if (std::fabs(lim - firstLimit) > tol) {
                    r.ok = false;
                    r.detail = "左右两侧极限不同（" + fmtNum(firstLimit) + " 与 " + fmtNum(lim) +
                               "），不存在双侧水平渐近线";
                    return r;
                }
            }
            r.ok = true;
            return r;
        }

        // ---------- 极值 ----------
        case ConstraintKind::Extremum: {
            const double h = 1e-2 * (1.0 + std::fabs(c.point));
            double f0 = 0.0;
            if (!evalFinite(f, c.var, c.point, &f0)) {
                r.ok = false;
                r.detail = "在 " + fmtPoint(c.point) + " 处无定义，无法取得极值";
                return r;
            }
            double vals[6] = {0, 0, 0, 0, 0, 0};
            const double offsets[6] = {-3 * h, -2 * h, -h, h, 2 * h, 3 * h};
            for (int i = 0; i < 6; ++i) {
                if (!evalFinite(f, c.var, c.point + offsets[i], &vals[i])) {
                    r.ok = false;
                    r.detail = "在 x = " + fmtNum(c.point + offsets[i]) +
                               " 处无定义，无法在附近比较函数值";
                    return r;
                }
            }
            const double nearTol = 1e-9 * (1.0 + std::fabs(f0));
            bool allBelow = true, allAbove = true;
            bool strictBelow = false, strictAbove = false;
            for (int i = 0; i < 6; ++i) {
                if (vals[i] > f0 + nearTol) allBelow = false;
                if (vals[i] < f0 - nearTol) allAbove = false;
                if (vals[i] < f0 - nearTol) strictBelow = true;
                if (vals[i] > f0 + nearTol) strictAbove = true;
            }
            const bool isLocalMax = allBelow && strictBelow;
            const bool isLocalMin = allAbove && strictAbove;
            if (c.isMax ? !isLocalMax : !isLocalMin) {
                r.ok = false;
                r.detail = "在 " + fmtPoint(c.point) + " 处 f = " + fmtNum(f0) + "，附近点 f = " +
                           fmtNum(vals[2]) + " / " + fmtNum(vals[3]) + "，并非" +
                           (c.isMax ? "极大值点" : "极小值点");
                return r;
            }
            // 若符号求导可用（diff 不再是桩），追加一阶导数为零的必要检查
            const ExprP d = diff(f, c.var);
            if (d && !(structurallyEqual(d, f) && hasVar(f))) {
                double dp = 0.0;
                if (evalFinite(d, c.var, c.point, &dp)) {
                    if (std::fabs(dp) > 1e-3 * (1.0 + std::fabs(f0))) {
                        r.ok = false;
                        r.detail = "在 " + fmtPoint(c.point) + " 处 f'(x) = " + fmtNum(dp) +
                                   " ≠ 0，该点不是极值点";
                        return r;
                    }
                }
            }
            r.ok = true;
            return r;
        }

        // ---------- 对称轴 ----------
        case ConstraintKind::SymmetryAxis: {
            std::string detail;
            r.ok = checkSymmetryIdentity(f, c.var, c.point, 0.0, false, &detail);
            r.detail = detail;
            return r;
        }

        // ---------- 零点个数 ----------
        case ConstraintKind::ZeroCount: {
            std::vector<double> zeros = zerosIn(f, c.interval, c.var);
            if (zeros.empty()) {
                zeros = numericZeros(f, c.var, c.interval);
            }
            const int got = static_cast<int>(zeros.size());
            if (got == c.count) {
                r.ok = true;
                return r;
            }
            std::string where;
            for (size_t i = 0; i < zeros.size() && i < 6; ++i) {
                if (i) where += "、";
                where += fmtNum(zeros[i]);
            }
            r.ok = false;
            r.detail = "在 " + fmtInterval(c.interval) + " 内应有 " + std::to_string(c.count) +
                       " 个零点，实际检测到 " + std::to_string(got) + " 个" +
                       (where.empty() ? std::string() : "（约在 x = " + where + "）");
            return r;
        }

        // ---------- 复杂度上限 ----------
        case ConstraintKind::ComplexityLimit: {
            const int used = funcCallCount(f);
            if (used > c.maxFuncCalls) {
                r.ok = false;
                r.detail = "函数调用数 " + std::to_string(used) + " 超过上限 " +
                           std::to_string(c.maxFuncCalls);
                return r;
            }
            r.ok = true;
            return r;
        }

        // ---------- 中心对称 ----------
        case ConstraintKind::PeriodicSymmetry: {
            std::string detail;
            r.ok = checkSymmetryIdentity(f, c.var, c.point, c.value, true, &detail);
            r.detail = detail;
            return r;
        }

        // =========================================================
        // 复变约束（依赖 Complex.h 的数值分析；底层未实现时会给出明确原因）
        // =========================================================

        // ---------- 在区域上全纯（解析） ----------
        case ConstraintKind::HolomorphicOn: {
            if (c.region.isEmpty()) {
                r.ok = false;
                r.detail = "约束本身无效：解析区域为空";
                return r;
            }
            const ComplexCheck cc = isHolomorphicOn(f, c.var, c.region);
            r.ok = cc.ok;
            if (!cc.ok) {
                r.detail = cc.reason.empty() ? (fmtInRegion(c.region) + "不是解析函数")
                                             : cc.reason;
            }
            return r;
        }

        // ---------- 区域内无奇点 ----------
        case ConstraintKind::NoSingularityIn: {
            if (c.region.isEmpty()) {
                r.ok = false;
                r.detail = "约束本身无效：检查区域为空";
                return r;
            }
            const std::vector<SingularPoint> sings = singularitiesIn(f, c.var, c.region);
            if (sings.empty()) {
                r.ok = true;
                return r;
            }
            std::vector<std::string> where;
            for (size_t i = 0; i < sings.size() && i < 4; ++i) {
                std::string desc = "z = " + fmtComplex(sings[i].z);
                if (sings[i].essential) {
                    desc += "（本质奇点）";
                } else if (sings[i].poleOrder > 0) {
                    desc += "（" + std::to_string(sings[i].poleOrder) + " 阶极点）";
                } else {
                    desc += "（可去奇点）";
                }
                where.push_back(desc);
            }
            r.ok = false;
            r.detail = fmtInsideRegion(c.region) + "检测到 " +
                       std::to_string(sings.size()) + " 个奇点：" + joinStrings(where, "、");
            return r;
        }

        // ---------- z0 处极点阶数 ----------
        case ConstraintKind::PoleOrderAt: {
            if (c.order <= 0) {
                r.ok = false;
                r.detail = "约束本身无效：极点阶数必须为正整数";
                return r;
            }
            // 在 z0 的小邻域内数值搜索孤立奇点，取离 z0 最近的一个
            static constexpr double kSearchRadius = 0.25;
            const std::vector<SingularPoint> sings =
                singularitiesIn(f, c.var, ComplexRegion::disk(c.cpoint, kSearchRadius));
            if (sings.empty()) {
                r.ok = false;
                r.detail = "在 z = " + fmtComplex(c.cpoint) +
                           " 附近没有检测到极点（该点可能是解析点，而非极点）";
                return r;
            }
            const SingularPoint* best = &sings.front();
            for (const SingularPoint& s : sings) {
                if ((s.z - c.cpoint).abs() < (best->z - c.cpoint).abs()) best = &s;
            }
            if ((best->z - c.cpoint).abs() > 0.05) {
                r.ok = false;
                r.detail = "在 z = " + fmtComplex(c.cpoint) + " 处没有极点（最近的奇点在 z = " +
                           fmtComplex(best->z) + "）";
                return r;
            }
            if (best->essential) {
                r.ok = false;
                r.detail = "z = " + fmtComplex(c.cpoint) + " 处是本质奇点，不是 " +
                           std::to_string(c.order) + " 阶极点";
                return r;
            }
            if (best->poleOrder <= 0) {
                r.ok = false;
                r.detail = "z = " + fmtComplex(c.cpoint) + " 处不是极点（是解析点或可去奇点），要求 " +
                           std::to_string(c.order) + " 阶极点";
                return r;
            }
            if (best->poleOrder != c.order) {
                r.ok = false;
                r.detail = "z = " + fmtComplex(c.cpoint) + " 处实测为 " +
                           std::to_string(best->poleOrder) + " 阶极点，要求 " +
                           std::to_string(c.order) + " 阶";
                return r;
            }
            r.ok = true;
            return r;
        }

        // ---------- z0 处留数 ----------
        case ConstraintKind::ResidueAt: {
            const CplxResult rr = residueAt(f, c.var, c.cpoint);
            if (!rr.ok || !rr.value.isFinite()) {
                r.ok = false;
                r.detail = "无法计算 z = " + fmtComplex(c.cpoint) + " 处的留数" +
                           (rr.error.empty() ? std::string() : "（" + rr.error + "）");
                return r;
            }
            const double relErr = cplxRelError(rr.value, c.cvalue);
            r.ok = relErr <= 1e-6;
            if (!r.ok) {
                r.detail = "在 z = " + fmtComplex(c.cpoint) + " 处实测留数为 " +
                           fmtComplex(rr.value) + "，要求 " + fmtComplex(c.cvalue) +
                           "（相对误差 " + fmtNum(relErr) + "）";
            }
            return r;
        }

        // ---------- 圆周围道积分值 ----------
        case ConstraintKind::ContourIntegralValue: {
            if (!(c.radius > 0.0)) {
                r.ok = false;
                r.detail = "约束本身无效：围道半径必须为正数";
                return r;
            }
            const CplxResult ir = contourIntegral(f, c.var, c.cpoint, c.radius);
            if (!ir.ok || !ir.value.isFinite()) {
                r.ok = false;
                r.detail = "无法计算沿 " + fmtDiskTerm(c.cpoint) + " = " + fmtNum(c.radius) +
                           " 的围道积分" + (ir.error.empty() ? std::string() : "（" + ir.error + "）");
                return r;
            }
            const double relErr = cplxRelError(ir.value, c.cvalue);
            r.ok = relErr <= 1e-6;
            if (!r.ok) {
                r.detail = "沿 " + fmtDiskTerm(c.cpoint) + " = " + fmtNum(c.radius) +
                           " 逆时针积分实测为 " + fmtComplex(ir.value) + "，要求 " +
                           fmtComplex(c.cvalue) + "（相对误差 " + fmtNum(relErr) + "）";
            }
            return r;
        }

        // ---------- 区域内零点个数 ----------
        case ConstraintKind::ZeroCountInRegion: {
            if (c.region.isEmpty()) {
                r.ok = false;
                r.detail = "约束本身无效：零点统计区域为空";
                return r;
            }
            if (c.count < 0) {
                r.ok = false;
                r.detail = "约束本身无效：零点个数不能为负";
                return r;
            }
            const IntResult zr = zeroCountIn(f, c.var, c.region);
            if (!zr.ok) {
                r.ok = false;
                r.detail = "无法统计 " + fmtRegion(c.region) + " 内的零点个数" +
                           (zr.error.empty() ? std::string() : "（" + zr.error + "）");
                return r;
            }
            if (zr.value != c.count) {
                r.ok = false;
                r.detail = fmtInsideRegion(c.region) + "实测有 " +
                           std::to_string(zr.value) + " 个零点（计重数），要求恰好 " +
                           std::to_string(c.count) + " 个";
                return r;
            }
            r.ok = true;
            return r;
        }

        // ---------- 区域内模的范围 ----------
        case ConstraintKind::ModulusRangeOn: {
            if (c.lo > c.hi) {
                r.ok = false;
                r.detail = "约束本身无效：下界大于上界";
                return r;
            }
            const ModRangeResult mr = modulusRangeOn(f, c.var, c.region);
            if (!mr.ok) {
                r.ok = false;
                r.detail = "无法判定 |f(z)| 在 " + fmtRegion(c.region) + " 上的取值范围" +
                           (mr.error.empty() ? std::string() : "（" + mr.error + "）");
                return r;
            }
            const double tol = 1e-6 * (1.0 + std::fabs(c.lo) + std::fabs(c.hi));
            if (mr.lo < c.lo - tol) {
                r.ok = false;
                r.detail = "在 " + fmtRegion(c.region) + " 上 |f(z)| 的最小值为 " + fmtNum(mr.lo) +
                           "，小于下界 " + fmtNum(c.lo);
                return r;
            }
            if (mr.hi > c.hi + tol) {
                r.ok = false;
                r.detail = "在 " + fmtRegion(c.region) + " 上 |f(z)| 的最大值为 " + fmtNum(mr.hi) +
                           "，超过上界 " + fmtNum(c.hi);
                return r;
            }
            r.ok = true;
            return r;
        }

        // ---------- 复数点取值 ----------
        case ConstraintKind::ComplexBoundaryValue: {
            const CplxResult ev = evalComplex(f, c.var, c.cpoint);
            if (!ev.ok || !ev.value.isFinite()) {
                r.ok = false;
                r.detail = "在 z = " + fmtComplex(c.cpoint) + " 处无定义，无法验证该点取值" +
                           (ev.error.empty() ? std::string() : "（" + ev.error + "）");
                return r;
            }
            const double relErr = cplxRelError(ev.value, c.cvalue);
            r.ok = relErr <= 1e-6;
            if (!r.ok) {
                r.detail = "f(" + fmtComplex(c.cpoint) + ") = " + fmtComplex(ev.value) +
                           "，要求等于 " + fmtComplex(c.cvalue) + "（相对误差 " +
                           fmtNum(relErr) + "）";
            }
            return r;
        }
    }

    r.ok = false;
    r.detail = "未知约束类型";
    return r;
}

}  // namespace fcg
