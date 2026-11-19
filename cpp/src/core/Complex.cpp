// Complex.cpp — 复数域与复变函数分析（全纯性 / 极点 / 留数 / 围道积分 / 零点计数）
//
// 数值方法总览（对应任务要求，取舍写在实现里）：
//   · 全纯性：四层判定
//       1) 结构检查：Re/Im/conj/arg/abs/min/max/floor/ceil/sign/mod 在复平面上不是全纯算子；
//       2) 柯西-黎曼方程数值验证：中心差分 h = 1e-6，相对残差阈值 1e-5；
//       3) 分支割线检查：ln/sqrt/反三角/非整数幂的主值函数跨割线不连续，而局部 CR 方程
//          在割线两侧各自成立，因此单独用"跳变是否随步长缩小而消失"来判定（见
//          crossesBranchCut）；
//       4) 孤立奇点检查：解析区域内不应有极点/本质奇点。可去奇点（如 sin(z)/z 在 0）
//          解析延拓后全纯，不判为"不解析"。
//   · 极点阶数：环形采样比值法。在 z0 周围取两个半径 r 与 r/8 的圆周，
//       A_k(r) = r^k · max|f(z0 + r e^{iθ})|；真正的极点阶数 k 满足 A_k 与 r 无关
//       （比值 ≈ 1），而 k 每偏差 1 阶比值就乘/除 8。这比直接对 log|f| 做斜率拟合
//       更抗"解析部分"的干扰，也完全不需要高阶数值求导。
//   · 留数 / 围道积分：圆周梯形公式（被积函数沿圆周是周期解析函数，梯形公式谱精度），
//       N 从 256/512 起自适应加倍，只有连续两次结果一致才接受；半径改变结果不稳定
//       （附近有其它奇点、或围道离奇点太近）就返回 error。
//   · 零点计数：辐角原理 (1/2πi)∮ f'/f dz，f' 用符号求导（Diff.h）；
//       区域边界按类型离散化（圆 / 双圆 / 矩形 / 大半圆）；极点个数 P 由
//       singularitiesIn 统计后扣除；结果不接近整数（围道上有零点或极点）返回 error。
//   · 奇点搜索：先做"结构性候选"——分母零点、负整数幂底数的零点、tan 的极点、
//       log/sqrt/非整数幂的分支点；多项式系数可提取时用 Durand-Kerner 精确求根，
//       否则用网格局部极小 + 复牛顿法兜底；最后合并近邻候选点。
//
// 已知限制：
//   · 分支割线只在区域"明显跨过割线"时被检测（相邻采样点跳变）；恰好贴着割线
//     且采样点全部落在一侧时可能漏判；
//   · 本质奇点（如 exp(1/z)）的留数数值上病态，通常返回 error 而不是错误数值；
//   · 极点阶数判定支持 1..20 阶；更高阶或极点在 1e-7 内相互重叠时可能退化为本质奇点；
//   · 无界区域（半平面 / 全平面）的零点计数用大围道近似，只在大围道结果收敛时给答案。

#include "Complex.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Diff.h"

namespace fcg {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kE = 2.718281828459045235360287471352662497757;

// -----------------------------------------------------------------
// 格式化
// -----------------------------------------------------------------
std::string fmtReal(double v) {
    if (!std::isfinite(v)) return v > 0 ? "+∞" : "-∞";
    if (std::fabs(v) < 1e-12) return "0";
    double r = std::round(v);
    if (std::fabs(v - r) < 1e-12 && std::fabs(r) < 1e12) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", r);
        return buf;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

// 复数格式化："0"、"1"、"i"、"-i"、"1 + 2i"、"0.5 - 3i"
std::string fmtCplx(Cplx v) {
    if (std::fabs(v.im) < 1e-12) return fmtReal(v.re);
    if (std::fabs(v.re) < 1e-12) {
        if (std::fabs(v.im - 1.0) < 1e-12) return "i";
        if (std::fabs(v.im + 1.0) < 1e-12) return "-i";
        return fmtReal(v.im) + "i";
    }
    std::string re = fmtReal(v.re);
    std::string imPart;
    if (std::fabs(std::fabs(v.im) - 1.0) < 1e-12)
        imPart = "";
    else
        imPart = fmtReal(std::fabs(v.im));
    return re + (v.im >= 0 ? " + " : " - ") + imPart + "i";
}

// "z"、"z - 1"、"z + i"、"z - (1 + 2i)"：注意：返回不带 |·|
std::string centerExpr(Cplx c) {
    if (c.abs() < 1e-12) return "z";
    std::string s = fmtCplx(c);
    if (s.empty()) return "z";
    if (s[0] == '-' && s.find(' ') == std::string::npos && s.find('+') == std::string::npos)
        return "z + " + s.substr(1);
    if (s.find(' ') == std::string::npos && s.find('+') == std::string::npos &&
        s.find('-') == std::string::npos)
        return "z - " + s;
    return "z - (" + s + ")";
}

std::string pointStr(Cplx z) { return "z = " + fmtCplx(z); }

// -----------------------------------------------------------------
// 复数求值 / 常量判定
// -----------------------------------------------------------------
bool evalAt(const ExprP& f, const std::string& var, Cplx z, Cplx* out) {
    if (!f || !z.isFinite()) return false;
    VarMap vars;
    vars[var] = z;
    EvalResult r = evaluate(f, vars);
    if (!r.ok || !r.value.isFinite()) return false;
    *out = r.value;
    return true;
}

// 常数表达式（不含变量）求值成复数；失败返回 false
bool constComplex(const ExprP& e, Cplx* out) {
    if (!e || !isConst(e)) return false;
    VarMap empty;
    EvalResult r = evaluate(e, empty);
    if (!r.ok || !r.value.isFinite()) return false;
    *out = r.value;
    return true;
}

bool cplxClose(Cplx a, Cplx b, double relTol, double absTol = 0.0) {
    double tol = std::max(absTol, relTol * std::max(1.0, std::max(a.abs(), b.abs())));
    return (a - b).abs() <= tol;
}

Cplx polar(double r, double th) { return Cplx(r * std::cos(th), r * std::sin(th)); }

// -----------------------------------------------------------------
// 区域几何
// -----------------------------------------------------------------
double regionScale(const ComplexRegion& r) {
    switch (r.kind) {
        case ComplexRegion::Kind::Disk:
            return std::max(1e-9, std::fabs(r.radius));
        case ComplexRegion::Kind::Annulus:
            return std::max(1e-9, std::max(std::fabs(r.outerRadius), std::fabs(r.radius)));
        case ComplexRegion::Kind::Rectangle:
            return std::max(1e-9, std::max(std::fabs(r.x1 - r.x0), std::fabs(r.y1 - r.y0)) * 0.5);
        case ComplexRegion::Kind::ExteriorOfDisk:
            return std::max(1e-9, std::fabs(r.radius));
        default:
            return 1.0;
    }
}

// 区域的外接矩形（无界区域用启发式的有限框，用于采样）
bool boundingBox(const ComplexRegion& r, double* xmin, double* xmax, double* ymin, double* ymax) {
    switch (r.kind) {
        case ComplexRegion::Kind::Disk: {
            if (!(r.radius > 0)) return false;
            *xmin = r.center.re - r.radius;
            *xmax = r.center.re + r.radius;
            *ymin = r.center.im - r.radius;
            *ymax = r.center.im + r.radius;
            return true;
        }
        case ComplexRegion::Kind::Annulus: {
            if (!(r.outerRadius > 0)) return false;
            *xmin = r.center.re - r.outerRadius;
            *xmax = r.center.re + r.outerRadius;
            *ymin = r.center.im - r.outerRadius;
            *ymax = r.center.im + r.outerRadius;
            return true;
        }
        case ComplexRegion::Kind::Rectangle: {
            *xmin = std::min(r.x0, r.x1);
            *xmax = std::max(r.x0, r.x1);
            *ymin = std::min(r.y0, r.y1);
            *ymax = std::max(r.y0, r.y1);
            return (*xmax > *xmin) && (*ymax > *ymin);
        }
        case ComplexRegion::Kind::HalfPlaneUpper: {
            *xmin = -2.0;
            *xmax = 2.0;
            *ymin = r.yLine;
            *ymax = r.yLine + 2.0;
            return true;
        }
        case ComplexRegion::Kind::HalfPlaneLower: {
            *xmin = -2.0;
            *xmax = 2.0;
            *ymin = r.yLine - 2.0;
            *ymax = r.yLine;
            return true;
        }
        case ComplexRegion::Kind::HalfPlaneRight: {
            *xmin = r.xLine;
            *xmax = r.xLine + 2.0;
            *ymin = -2.0;
            *ymax = 2.0;
            return true;
        }
        case ComplexRegion::Kind::ExteriorOfDisk: {
            double rr = std::fabs(r.radius) + 2.0;
            *xmin = r.center.re - rr;
            *xmax = r.center.re + rr;
            *ymin = r.center.im - rr;
            *ymax = r.center.im + rr;
            return true;
        }
        case ComplexRegion::Kind::Plane: {
            *xmin = -2.0;
            *xmax = 2.0;
            *ymin = -2.0;
            *ymax = 2.0;
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------
// 柯西-黎曼数值验证
// -----------------------------------------------------------------
struct CrScanResult {
    double residual = 0.0;  // 最大相对残差
    Cplx worst;             // 残差最大的采样点
    bool any = false;       // 是否有有效的采样点
    int count = 0;
    bool aborted = false;
};

// abortAbove >= 0 时，一旦残差超过它就提前返回（用于快速否定非全纯函数）
CrScanResult scanCauchyRiemann(const ExprP& f, const std::string& var, const ComplexRegion& region,
                               int samples, double abortAbove = -1.0) {
    CrScanResult out;
    if (!f) return out;
    if (samples < 2) samples = 2;
    if (samples > 400) samples = 400;
    double bx0, bx1, by0, by1;
    if (!boundingBox(region, &bx0, &bx1, &by0, &by1)) return out;
    if (!(bx1 > bx0)) bx1 = bx0 + 1.0;
    if (!(by1 > by0)) by1 = by0 + 1.0;
    const double h = 1e-6;
    // 采样点：一般用外接矩形均匀网格；薄圆环改用极坐标网格（否则网格点几乎都落在环外）
    std::vector<Cplx> pts;
    pts.reserve((size_t)samples * samples);
    if (region.kind == ComplexRegion::Kind::Annulus && region.radius > 0.0 &&
        region.outerRadius > region.radius) {
        for (int i = 0; i < samples; ++i) {
            double rr = region.radius + (region.outerRadius - region.radius) *
                                            ((double)i + 0.5) / samples;
            for (int j = 0; j < samples; ++j) {
                double th = kTwoPi * ((double)j + 0.37) / samples;
                pts.push_back(region.center + polar(rr, th));
            }
        }
    } else {
        for (int iy = 0; iy < samples; ++iy) {
            double y = by0 + (by1 - by0) * ((double)iy + 0.5) / samples;
            for (int ix = 0; ix < samples; ++ix) {
                double x = bx0 + (bx1 - bx0) * ((double)ix + 0.5) / samples;
                pts.push_back(Cplx(x, y));
            }
        }
    }
    for (const Cplx& z : pts) {
        if (!region.contains(z, 0.0)) continue;
        double x = z.re;
        double y = z.im;
        Cplx fz;
        if (!evalAt(f, var, z, &fz)) continue;
        Cplx fxp, fxm, fyp, fym;
        if (!evalAt(f, var, Cplx(x + h, y), &fxp)) continue;
        if (!evalAt(f, var, Cplx(x - h, y), &fxm)) continue;
        if (!evalAt(f, var, Cplx(x, y + h), &fyp)) continue;
        if (!evalAt(f, var, Cplx(x, y - h), &fym)) continue;
        Cplx fx((fxp.re - fxm.re) / (2.0 * h), (fxp.im - fxm.im) / (2.0 * h));
        Cplx fy((fyp.re - fym.re) / (2.0 * h), (fyp.im - fym.im) / (2.0 * h));
        // ∂f/∂x 应当等于 (1/i)·∂f/∂y，即 fy/i = (fy.im, -fy.re)
        Cplx fyOverI(fy.im, -fy.re);
        Cplx d = fx - fyOverI;
        double denom = std::max(1.0, fx.abs() + fy.abs());
        double res = d.abs() / denom;
        if (!std::isfinite(res)) continue;
        ++out.count;
        out.any = true;
        if (res > out.residual) {
            out.residual = res;
            out.worst = z;
        }
        if (abortAbove >= 0.0 && res > abortAbove) {
            out.aborted = true;
            return out;
        }
    }
    return out;
}

// -----------------------------------------------------------------
// 分支割线检测
//
// 全纯性有两层结构检查 + 柯西-黎曼数值验证；但 ln / sqrt / 反三角 / 非整数幂
// 这类主值函数有分支割线：局部柯西-黎曼方程在割线两侧各自都成立，只是函数跨过
// 割线不连续。这里用"相邻采样点间的跳变是否随步长缩小而保持"来判定：
// 解析函数的跳变 ∝ 步长（缩小 8 倍跳变小 8 倍），跨分支割线的跳变不随步长缩小。
// -----------------------------------------------------------------
bool expressionHasBranchCuts(const ExprP& f) {
    if (!f) return false;
    bool risk = false;
    visit(f, [&](const ExprP& e) {
        if (risk || !e) return;
        switch (e->op) {
            case Op::Ln:
            case Op::Log2:
            case Op::Log10:
            case Op::Sqrt:
            case Op::Cbrt:
            case Op::Asin:
            case Op::Acos:
            case Op::Atan:
            case Op::Asinh:
            case Op::Acosh:
            case Op::Atanh:
                risk = true;
                break;
            case Op::Pow: {
                if (hasVar(e->b)) break;
                Cplx ex;
                if (!constComplex(e->b, &ex)) break;
                if (std::fabs(ex.im) > 1e-12) {
                    risk = true;  // 复指数幂走主值分支
                } else {
                    double p = ex.re;
                    if (std::fabs(p - std::round(p)) > 1e-12) risk = true;  // 非整数幂
                }
                break;
            }
            default:
                break;
        }
    });
    return risk;
}

// 线段 z1→z2 上是否存在"不随细分消失"的跳变（即跨分支割线的迹象）
bool segmentCrossesCut(const ExprP& f, const std::string& var, Cplx z1, Cplx z2) {
    const int sub = 8;
    Cplx vals[sub + 1];
    for (int k = 0; k <= sub; ++k) {
        Cplx z = z1 + (z2 - z1) * ((double)k / sub);
        if (!evalAt(f, var, z, &vals[k])) return true;  // 段上不可求值
    }
    double parentJump = (vals[sub] - vals[0]).abs();
    if (parentJump <= 1e-9) return false;
    double maxJump = 0.0;
    for (int k = 0; k < sub; ++k) {
        double j = (vals[k + 1] - vals[k]).abs();
        if (j > maxJump) maxJump = j;
    }
    return maxJump > 0.5 * parentJump;
}

// 区域是否跨过分支割线（只在此类表达式上调用，避免无谓开销）
bool crossesBranchCut(const ExprP& f, const std::string& var, const ComplexRegion& region,
                      int samples = 32) {
    if (!f || region.isEmpty()) return false;
    if (samples < 8) samples = 8;
    if (samples > 48) samples = 48;
    double bx0, bx1, by0, by1;
    if (!boundingBox(region, &bx0, &bx1, &by0, &by1)) return false;
    if (!(bx1 > bx0) || !(by1 > by0)) return false;
    const int n = samples;
    std::vector<Cplx> vals((size_t)n * n);
    std::vector<char> ok((size_t)n * n, 0);
    std::vector<double> xs((size_t)n), ys((size_t)n);
    double dx = (bx1 - bx0) / n;
    double dy = (by1 - by0) / n;
    for (int iy = 0; iy < n; ++iy) {
        ys[(size_t)iy] = by0 + dy * ((double)iy + 0.5);
        for (int ix = 0; ix < n; ++ix) {
            xs[(size_t)ix] = bx0 + dx * ((double)ix + 0.5);
            Cplx z(xs[(size_t)ix], ys[(size_t)iy]);
            if (!region.contains(z, 0.0)) continue;
            Cplx v;
            if (!evalAt(f, var, z, &v)) continue;
            size_t idx = (size_t)iy * n + ix;
            vals[idx] = v;
            ok[idx] = 1;
        }
    }
    auto suspect = [&](size_t a, size_t b) {
        double denom = vals[a].abs() + vals[b].abs() + 0.3;
        double jump = (vals[a] - vals[b]).abs();
        // 解析函数的局部斜率有限：跳变远大于两侧量级时才值得进一步确认
        return jump > 0.5 * denom;
    };
    for (int iy = 0; iy < n; ++iy) {
        for (int ix = 0; ix < n; ++ix) {
            size_t idx = (size_t)iy * n + ix;
            if (!ok[idx]) continue;
            if (ix + 1 < n) {
                size_t nb = idx + 1;
                if (ok[nb] && suspect(idx, nb)) {
                    Cplx z1(xs[(size_t)ix], ys[(size_t)iy]);
                    Cplx z2(xs[(size_t)ix + 1], ys[(size_t)iy]);
                    if (segmentCrossesCut(f, var, z1, z2)) return true;
                }
            }
            if (iy + 1 < n) {
                size_t nb = idx + (size_t)n;
                if (ok[nb] && suspect(idx, nb)) {
                    Cplx z1(xs[(size_t)ix], ys[(size_t)iy]);
                    Cplx z2(xs[(size_t)ix], ys[(size_t)iy + 1]);
                    if (segmentCrossesCut(f, var, z1, z2)) return true;
                }
            }
        }
    }
    return false;
}

// -----------------------------------------------------------------
// 多项式系数提取（用于分母零点的精确求解）
// -----------------------------------------------------------------
using Poly = std::vector<Cplx>;  // coeff[i] 是 z^i 的系数

bool polyMul(const Poly& a, const Poly& b, int maxDeg, Poly* out) {
    if (a.empty() || b.empty()) return false;
    int deg = (int)a.size() + (int)b.size() - 2;
    if (deg > maxDeg) return false;
    Poly r((size_t)deg + 1, Cplx(0.0, 0.0));
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = 0; j < b.size(); ++j) r[i + j] = r[i + j] + a[i] * b[j];
    *out = std::move(r);
    return true;
}

bool extractPoly(const ExprP& e, const std::string& var, Poly* out, int maxDeg) {
    out->clear();
    if (!e) return false;
    if (!hasVar(e)) {
        Cplx c;
        if (!constComplex(e, &c)) return false;
        out->assign(1, c);
        return true;
    }
    switch (e->op) {
        case Op::Var:
            if (e->var != var) return false;
            out->assign(2, Cplx(0.0, 0.0));
            (*out)[1] = Cplx(1.0, 0.0);
            return true;
        case Op::Neg: {
            if (!extractPoly(e->a, var, out, maxDeg)) return false;
            for (auto& c : *out) c = -c;
            return true;
        }
        case Op::Add:
        case Op::Sub: {
            Poly a, b;
            if (!extractPoly(e->a, var, &a, maxDeg)) return false;
            if (!extractPoly(e->b, var, &b, maxDeg)) return false;
            if (a.size() < b.size()) a.resize(b.size(), Cplx(0.0, 0.0));
            if (b.size() < a.size()) b.resize(a.size(), Cplx(0.0, 0.0));
            for (size_t i = 0; i < a.size(); ++i)
                a[i] = (e->op == Op::Add) ? (a[i] + b[i]) : (a[i] - b[i]);
            if ((int)a.size() - 1 > maxDeg) return false;
            *out = std::move(a);
            return true;
        }
        case Op::Mul: {
            Poly a, b;
            if (!extractPoly(e->a, var, &a, maxDeg)) return false;
            if (!extractPoly(e->b, var, &b, maxDeg)) return false;
            return polyMul(a, b, maxDeg, out);
        }
        case Op::Div: {
            Poly a, b;
            if (!extractPoly(e->a, var, &a, maxDeg)) return false;
            if (!extractPoly(e->b, var, &b, maxDeg)) return false;
            if (b.size() != 1) return false;  // 分母含变量 → 不是多项式
            if (b[0].abs() < 1e-300) return false;
            for (auto& c : a) c = c / b[0];
            *out = std::move(a);
            return true;
        }
        case Op::Pow: {
            if (hasVar(e->b)) return false;
            Cplx ex;
            if (!constComplex(e->b, &ex)) return false;
            if (std::fabs(ex.im) > 1e-12) return false;
            double p = ex.re;
            double rp = std::round(p);
            if (std::fabs(p - rp) > 1e-12 || rp < 0 || rp > maxDeg) return false;
            Poly base;
            if (!extractPoly(e->a, var, &base, maxDeg)) return false;
            Poly acc(1, Cplx(1.0, 0.0));
            int n = (int)rp;
            for (int i = 0; i < n; ++i) {
                Poly t;
                if (!polyMul(acc, base, maxDeg, &t)) return false;
                acc = std::move(t);
            }
            *out = std::move(acc);
            return true;
        }
        default:
            return false;  // 超越函数：不是多项式
    }
}

// Durand-Kerner 求全部复根（含重根，精度 ~1e-12）
std::vector<Cplx> polynomialRoots(Poly c) {
    while (c.size() > 1 && c.back().abs() <= 1e-300) c.pop_back();
    int n = (int)c.size() - 1;
    if (n < 1) return {};
    if (n == 1) {
        Cplx r = -c[0] / c[1];
        if (!r.isFinite()) return {};
        return {r};
    }
    Cplx lead = c[(size_t)n];
    for (auto& v : c) v = v / lead;
    double R = 1.0;
    for (int i = 0; i < n; ++i) R = std::max(R, 1.0 + c[(size_t)i].abs());
    auto peval = [&c, n](Cplx z) {
        Cplx s = c[(size_t)n];
        for (int i = n - 1; i >= 0; --i) s = s * z + c[(size_t)i];
        return s;
    };
    auto pderiv = [&c, n](Cplx z) {
        Cplx s = c[(size_t)n] * Cplx((double)n);
        for (int i = n - 1; i >= 1; --i) s = s * z + c[(size_t)i] * Cplx((double)i);
        return s;
    };
    std::vector<Cplx> z((size_t)n);
    for (int i = 0; i < n; ++i) z[(size_t)i] = polar(R, kTwoPi * ((double)i + 0.5) / n);
    for (int iter = 0; iter < 1000; ++iter) {
        double maxRel = 0.0;
        for (int i = 0; i < n; ++i) {
            Cplx den(1.0, 0.0);
            bool singular = false;
            for (int j = 0; j < n; ++j) {
                if (j == i) continue;
                Cplx d = z[(size_t)i] - z[(size_t)j];
                if (d.abs() < 1e-300) {
                    singular = true;
                    break;
                }
                den = den * d;
            }
            if (singular) {
                z[(size_t)i] = z[(size_t)i] + Cplx(1e-6, 1.3e-6);
                continue;
            }
            Cplx delta = peval(z[(size_t)i]) / den;
            if (!delta.isFinite()) continue;
            z[(size_t)i] = z[(size_t)i] - delta;
            double rel = delta.abs() / (1.0 + z[(size_t)i].abs());
            if (rel > maxRel) maxRel = rel;
        }
        if (maxRel < 1e-15) break;
    }
    // 牛顿抛光（重根处线性收敛，多迭代几十次）
    for (int iter = 0; iter < 120; ++iter) {
        double maxDelta = 0.0;
        for (int i = 0; i < n; ++i) {
            Cplx d = pderiv(z[(size_t)i]);
            if (d.abs() < 1e-300) continue;
            Cplx delta = peval(z[(size_t)i]) / d;
            if (!delta.isFinite()) continue;
            z[(size_t)i] = z[(size_t)i] - delta;
            double rel = delta.abs() / (1.0 + z[(size_t)i].abs());
            if (rel > maxDelta) maxDelta = rel;
        }
        if (maxDelta < 1e-16) break;
    }
    std::vector<Cplx> out;
    for (auto& root : z) {
        if (!root.isFinite()) continue;
        double mag = peval(root).abs();
        Cplx pw(1.0, 0.0);
        double bound = 0.0;
        for (int i = 0; i <= n; ++i) {
            bound += c[(size_t)i].abs() * pw.abs();
            pw = pw * root;
        }
        if (mag <= 1e-8 * std::max(1.0, bound)) out.push_back(root);
    }
    return out;
}

// -----------------------------------------------------------------
// 零点 / 奇点候选搜索
// -----------------------------------------------------------------
struct RawCandidate {
    Cplx z;
    bool branchPoint = false;
};

bool newtonRefineZero(const ExprP& g, const std::string& var, Cplx z0, Cplx* out) {
    Cplx z = z0;
    for (int iter = 0; iter < 100; ++iter) {
        Cplx gz;
        if (!evalAt(g, var, z, &gz)) return false;
        if (gz.abs() < 1e-14) {
            *out = z;
            return true;
        }
        double h = 1e-6 * (1.0 + z.abs());
        Cplx gp, gm;
        if (!evalAt(g, var, Cplx(z.re + h, z.im), &gp)) return false;
        if (!evalAt(g, var, Cplx(z.re - h, z.im), &gm)) return false;
        Cplx dg((gp.re - gm.re) / (2.0 * h), (gp.im - gm.im) / (2.0 * h));
        if (dg.abs() < 1e-14) return false;
        Cplx step = gz / dg;
        if (!step.isFinite()) return false;
        z = z - step;
        if (!z.isFinite()) return false;
        if (step.abs() <= 1e-14 * (1.0 + z.abs())) {
            *out = z;
            return true;
        }
    }
    return false;
}

// 无多项式结构时的通用复零点搜索：网格局部极小 + 牛顿法
std::vector<Cplx> transcendentalZeros(const ExprP& g, const std::string& var,
                                      const ComplexRegion& region) {
    std::vector<Cplx> out;
    double bx0, bx1, by0, by1;
    if (!boundingBox(region, &bx0, &bx1, &by0, &by1)) return out;
    const int N = 48;
    std::vector<double> absv((size_t)N * N, 0.0);
    std::vector<char> valid((size_t)N * N, 0);
    for (int iy = 0; iy < N; ++iy) {
        double y = by0 + (by1 - by0) * ((double)iy + 0.5) / N;
        for (int ix = 0; ix < N; ++ix) {
            double x = bx0 + (bx1 - bx0) * ((double)ix + 0.5) / N;
            Cplx z(x, y);
            if (!region.contains(z, 0.0)) continue;
            Cplx v;
            if (!evalAt(g, var, z, &v)) continue;
            absv[(size_t)iy * N + ix] = v.abs();
            valid[(size_t)iy * N + ix] = 1;
        }
    }
    struct Node {
        int ix, iy;
        double a;
    };
    std::vector<Node> cands;
    for (int iy = 0; iy < N; ++iy) {
        for (int ix = 0; ix < N; ++ix) {
            size_t idx = (size_t)iy * N + ix;
            if (!valid[idx]) continue;
            double a = absv[idx];
            bool localMin = true;
            for (int dy = -1; dy <= 1 && localMin; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = ix + dx, ny = iy + dy;
                    if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                    size_t nidx = (size_t)ny * N + nx;
                    if (!valid[nidx]) continue;
                    if (absv[nidx] < a) {
                        localMin = false;
                        break;
                    }
                }
            }
            if (localMin) cands.push_back({ix, iy, a});
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Node& a, const Node& b) { return a.a < b.a; });
    if (cands.size() > 16) cands.resize(16);
    for (auto& cn : cands) {
        double x = bx0 + (bx1 - bx0) * ((double)cn.ix + 0.5) / N;
        double y = by0 + (by1 - by0) * ((double)cn.iy + 0.5) / N;
        Cplx root;
        if (!newtonRefineZero(g, var, Cplx(x, y), &root)) continue;
        if (!root.isFinite()) continue;
        if (!region.contains(root, 1e-9 * (1.0 + root.abs()))) continue;
        bool dup = false;
        for (auto& r : out)
            if ((r - root).abs() <= 1e-6 * (1.0 + root.abs())) {
                dup = true;
                break;
            }
        if (!dup) out.push_back(root);
        if (out.size() >= 8) break;
    }
    return out;
}

std::vector<Cplx> findZerosInRegion(const ExprP& g, const std::string& var,
                                    const ComplexRegion& region) {
    std::vector<Cplx> roots;
    Poly coeffs;
    bool polyOk = extractPoly(g, var, &coeffs, 20);
    if (polyOk) {
        int deg = (int)coeffs.size() - 1;
        while (deg > 0 && coeffs[(size_t)deg].abs() <= 1e-300) --deg;
        if (deg <= 0) return {};  // 常数分母，无零点
        roots = polynomialRoots(coeffs);
        if ((int)roots.size() < deg) roots = transcendentalZeros(g, var, region);  // 求根失败兜底
    } else {
        roots = transcendentalZeros(g, var, region);
    }
    std::vector<Cplx> out;
    for (auto& r : roots) {
        if (!r.isFinite()) continue;
        if (!region.contains(r, 1e-9 * (1.0 + r.abs()))) continue;
        bool dup = false;
        for (auto& o : out)
            if ((o - r).abs() <= 1e-6 * (1.0 + r.abs())) {
                dup = true;
                break;
            }
        if (!dup) out.push_back(r);
        if (out.size() > 40) break;
    }
    return out;
}

// 收集"结构性奇点候选"：分母零点 / 负整数幂底数零点 / tan 极点 / log、非整数幂分支点
std::vector<RawCandidate> collectCandidatePoints(const ExprP& f, const std::string& var,
                                                 const ComplexRegion& region) {
    std::vector<RawCandidate> out;
    if (!f || region.isEmpty()) return out;
    std::vector<std::pair<ExprP, bool>> targets;  // (子表达式, 是否分支点)
    std::vector<uint64_t> seenHash;
    auto addTarget = [&](const ExprP& t, bool branch) {
        if (!t || !hasVar(t)) return;  // 常数没有零点（z 依赖才有）
        uint64_t h = hashOf(t);
        for (auto s : seenHash)
            if (s == h) return;
        seenHash.push_back(h);
        targets.push_back({t, branch});
    };
    visit(f, [&](const ExprP& e) {
        switch (e->op) {
            case Op::Div:
                addTarget(e->b, false);  // 分母零点 = 可能的极点
                break;
            case Op::Tan: {
                // tan(u) 的极点是 cos(u) 的零点
                addTarget(mkFunc1(Op::Cos, e->a), false);
                break;
            }
            case Op::Ln:
            case Op::Log2:
            case Op::Log10:
            case Op::Sqrt:
                addTarget(e->a, true);  // log / sqrt 在参数为 0 处有分支点
                break;
            case Op::Pow: {
                if (hasVar(e->b)) break;
                Cplx ex;
                if (!constComplex(e->b, &ex)) break;
                if (std::fabs(ex.im) > 1e-12) {
                    addTarget(e->a, true);  // 复指数幂 → 分支点
                    break;
                }
                double p = ex.re;
                bool isInt = std::fabs(p - std::round(p)) < 1e-12;
                if (!isInt) {
                    addTarget(e->a, true);  // 非整数幂 → 分支点
                } else if (p < 0) {
                    addTarget(e->a, false);  // 负整数幂 → 极点
                }
                // 非负整数幂：只产生零点，不产生奇点
                break;
            }
            default:
                break;
        }
    });
    if (targets.size() > 8) targets.resize(8);
    for (auto& t : targets) {
        std::vector<Cplx> roots = findZerosInRegion(t.first, var, region);
        for (auto& z : roots) {
            bool merged = false;
            for (auto& c : out) {
                if ((c.z - z).abs() <= 1e-6 * (1.0 + z.abs())) {
                    // 同一点既是极点来源又是分支来源时，优先按极点分类
                    c.branchPoint = c.branchPoint && t.second;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                RawCandidate rc;
                rc.z = z;
                rc.branchPoint = t.second;
                out.push_back(rc);
            }
        }
        if (out.size() > 24) break;
    }
    if (out.size() > 24) out.resize(24);
    return out;
}

// -----------------------------------------------------------------
// 奇点分类：环形采样比值法
// -----------------------------------------------------------------
SingularPoint classifySingularity(const ExprP& f, const std::string& var, Cplx z0,
                                  bool branchPoint, double isolateRadius) {
    SingularPoint sp;
    sp.z = z0;
    if (branchPoint) {
        sp.essential = true;
        sp.poleOrder = -1;
        return sp;
    }
    const int kMaxOrder = 20;
    const int nAng = 48;
    double baseR = 1e-2;
    if (isolateRadius > 0) baseR = std::min(baseR, isolateRadius);
    if (baseR < 1e-7) baseR = 1e-7;

    struct RingStat {
        bool valid = false;
        double maxAbs = 0.0;
    };
    auto sampleRing = [&](double r, RingStat* out) {
        double mx = 0.0;
        int valid = 0;
        for (int i = 0; i < nAng; ++i) {
            double th = kTwoPi * ((double)i + 0.473) / nAng;
            Cplx z = z0 + polar(r, th);
            Cplx v;
            if (!evalAt(f, var, z, &v)) continue;
            ++valid;
            double a = v.abs();
            if (std::isfinite(a) && a > mx) mx = a;
        }
        if (valid < nAng / 2) return false;
        out->valid = true;
        out->maxAbs = mx;
        return true;
    };

    double r1 = baseR;
    RingStat ring1, ring2;
    bool got = false;
    for (int attempt = 0; attempt < 8 && !got; ++attempt) {
        RingStat a, b;
        if (sampleRing(r1, &a) && sampleRing(r1 / 8.0, &b)) {
            ring1 = a;
            ring2 = b;
            got = true;
            break;
        }
        r1 /= 8.0;
        if (r1 < 1e-12) break;
    }
    if (!got) {
        sp.essential = true;
        sp.poleOrder = -1;
        return sp;
    }
    double r2 = r1 / 8.0;
    double A0a = ring1.maxAbs, A0b = ring2.maxAbs;
    // f 在环上恒为 0 → 有界，可去奇点
    if (A0a == 0.0 && A0b == 0.0) {
        sp.poleOrder = 0;
        return sp;
    }
    // |f| 不随半径缩小而放大 → 有界（可去奇点，或抵消后的零点）
    if (A0a > 0.0 && A0b > 0.0) {
        double ratio0 = A0a / A0b;
        if (std::isfinite(ratio0) && ratio0 >= 0.35) {
            sp.poleOrder = 0;
            return sp;
        }
    }
    // A_k(r) = r^k · max|f|：真阶数 k 时 A_k(r) 与 r 无关
    int bestK = -1;
    double bestDev = std::numeric_limits<double>::infinity();
    double rk1 = 1.0, rk2 = 1.0;
    for (int k = 0; k <= kMaxOrder; ++k) {
        if (k > 0) {
            rk1 *= r1;
            rk2 *= r2;
        }
        double A1 = A0a * rk1;
        double A2 = A0b * rk2;
        if (!std::isfinite(A1) || !std::isfinite(A2) || A2 <= 0.0) continue;
        double ratio = A1 / A2;
        if (!std::isfinite(ratio) || ratio <= 0.0) continue;
        double dev = std::fabs(std::log(ratio));
        if (dev < bestDev) {
            bestDev = dev;
            bestK = k;
        }
    }
    if (bestK >= 0 && bestDev < 0.9) {
        sp.poleOrder = bestK;
        sp.essential = false;
        return sp;
    }
    sp.essential = true;
    sp.poleOrder = -1;
    return sp;
}

// 对一组候选点分类（重复用它来给 singularitiesIn / zeroCountIn 复用）
std::vector<SingularPoint> classifyCandidates(const ExprP& f, const std::string& var,
                                              const std::vector<RawCandidate>& cands) {
    std::vector<SingularPoint> out;
    if (cands.empty()) return out;
    for (size_t i = 0; i < cands.size(); ++i) {
        double dmin = std::numeric_limits<double>::infinity();
        for (size_t j = 0; j < cands.size(); ++j) {
            if (i == j) continue;
            double d = (cands[j].z - cands[i].z).abs();
            if (d > 1e-12) dmin = std::min(dmin, d);
        }
        double isolate = std::isfinite(dmin) ? dmin / 3.0 : 0.0;
        SingularPoint sp = classifySingularity(f, var, cands[i].z, cands[i].branchPoint, isolate);
        out.push_back(sp);
    }
    return out;
}

// -----------------------------------------------------------------
// 围道参数化与积分
// -----------------------------------------------------------------
struct CurveSeg {
    std::function<Cplx(double)> pos;  // t ∈ [0,1] 上的位置
    std::function<Cplx(double)> dpos; // dz/dt
    bool periodic = false;            // 整圆：用周期梯形公式
};

CurveSeg makeCircle(Cplx c, double r, double th0, double th1) {
    CurveSeg s;
    s.pos = [c, r, th0, th1](double t) {
        double th = th0 + (th1 - th0) * t;
        return c + polar(r, th);
    };
    s.dpos = [r, th0, th1](double t) {
        double th = th0 + (th1 - th0) * t;
        return Cplx(-r * std::sin(th), r * std::cos(th)) * (th1 - th0);
    };
    s.periodic = std::fabs((th1 - th0) - kTwoPi) < 1e-9;
    return s;
}

CurveSeg makeLine(Cplx p0, Cplx p1) {
    CurveSeg s;
    s.pos = [p0, p1](double t) { return p0 + (p1 - p0) * t; };
    s.dpos = [p0, p1](double t) {
        (void)t;
        return p1 - p0;
    };
    return s;
}

// ∫₀¹ g(z(t))·z'(t) dt（梯形公式；整圆用周期梯形公式，谱精度）
bool integrateSeg(const ExprP& g, const std::string& var, const CurveSeg& seg, int n, Cplx* out,
                  std::string* err) {
    if (n < 2) n = 2;
    Cplx sum;
    int count = seg.periodic ? n : (n + 1);
    for (int i = 0; i < count; ++i) {
        double t = (double)i / (double)n;
        Cplx z = seg.pos(t);
        Cplx dz = seg.dpos(t);
        Cplx gv;
        if (!evalAt(g, var, z, &gv)) {
            if (err) *err = "围道上有不可求值点（可能有奇点）";
            return false;
        }
        double w = 1.0;
        if (!seg.periodic && (i == 0 || i == count - 1)) w = 0.5;
        sum = sum + (gv * dz) * w;
    }
    *out = sum / Cplx((double)n);
    return true;
}

// 沿整条边界求 ∮ g dz，并自适应加密直到两次结果一致
bool computeWinding(const ExprP& g, const std::string& var, const std::vector<CurveSeg>& segs,
                    Cplx* out, std::string* err) {
    Cplx prev;
    bool havePrev = false;
    const int sizes[3] = {512, 1024, 2048};
    for (int si = 0; si < 3; ++si) {
        int n = sizes[si];
        Cplx total;
        for (const auto& seg : segs) {
            Cplx part;
            if (!integrateSeg(g, var, seg, n, &part, err)) return false;
            total = total + part;
        }
        Cplx W = total / Cplx(0.0, kTwoPi);
        if (havePrev && (W - prev).abs() <= 1e-6 * std::max(1.0, W.abs())) {
            *out = W;
            return true;
        }
        prev = W;
        havePrev = true;
    }
    if (err) *err = "围道积分数值不稳定（围道离奇点太近）";
    return false;
}

// 单个圆周上的 ∮ f dz（梯形公式，不做收敛判断）
bool circleIntegralOnce(const ExprP& f, const std::string& var, Cplx c, double r, int n, Cplx* out,
                        std::string* err) {
    std::vector<CurveSeg> segs;
    segs.push_back(makeCircle(c, r, 0.0, kTwoPi));
    return integrateSeg(f, var, segs[0], n, out, err);
}

// -----------------------------------------------------------------
// 零点计数（单个围道规模下）
// -----------------------------------------------------------------
struct ZeroCountAt {
    bool ok = false;
    long long n = 0;
    std::string error;
};

bool nearContour(const ComplexRegion& region, Cplx z, double bigR, double tol) {
    switch (region.kind) {
        case ComplexRegion::Kind::Disk:
            return std::fabs((z - region.center).abs() - region.radius) <= tol;
        case ComplexRegion::Kind::Annulus:
            return std::fabs((z - region.center).abs() - region.outerRadius) <= tol ||
                   std::fabs((z - region.center).abs() - region.radius) <= tol;
        case ComplexRegion::Kind::Rectangle: {
            double d1 = std::fabs(z.re - region.x0);
            double d2 = std::fabs(z.re - region.x1);
            double d3 = std::fabs(z.im - region.y0);
            double d4 = std::fabs(z.im - region.y1);
            double d = std::min(std::min(d1, d2), std::min(d3, d4));
            return d <= tol;
        }
        case ComplexRegion::Kind::HalfPlaneUpper: {
            if (std::fabs(z.im - region.yLine) <= tol) return true;
            Cplx c(0.0, region.yLine);
            return bigR > 0 && std::fabs((z - c).abs() - bigR) <= tol;
        }
        case ComplexRegion::Kind::HalfPlaneLower: {
            if (std::fabs(z.im - region.yLine) <= tol) return true;
            Cplx c(0.0, region.yLine);
            return bigR > 0 && std::fabs((z - c).abs() - bigR) <= tol;
        }
        case ComplexRegion::Kind::HalfPlaneRight: {
            if (std::fabs(z.re - region.xLine) <= tol) return true;
            Cplx c(region.xLine, 0.0);
            return bigR > 0 && std::fabs((z - c).abs() - bigR) <= tol;
        }
        case ComplexRegion::Kind::Plane:
            return bigR > 0 && std::fabs(z.abs() - bigR) <= tol;
        default:
            return false;
    }
}

// 点是否在围道"内部"（用于统计极点个数；与区域的开性一致）
bool insideContour(const ComplexRegion& region, Cplx z, double bigR) {
    double tol = -1e-9 * (1.0 + z.abs());
    switch (region.kind) {
        case ComplexRegion::Kind::HalfPlaneUpper:
        case ComplexRegion::Kind::HalfPlaneLower: {
            if (!region.contains(z, tol)) return false;
            if (bigR <= 0) return true;
            Cplx c(0.0, region.yLine);
            return (z - c).abs() < bigR * (1.0 - 1e-9);
        }
        case ComplexRegion::Kind::HalfPlaneRight: {
            if (!region.contains(z, tol)) return false;
            if (bigR <= 0) return true;
            Cplx c(region.xLine, 0.0);
            return (z - c).abs() < bigR * (1.0 - 1e-9);
        }
        case ComplexRegion::Kind::Plane: {
            if (bigR <= 0) return true;
            return z.abs() < bigR * (1.0 - 1e-9);
        }
        case ComplexRegion::Kind::ExteriorOfDisk:
            return false;  // 无界区域（圆外）不支持零点计数
        default:
            return region.contains(z, tol);
    }
}

ZeroCountAt zeroCountAtScale(const ExprP& f, const ExprP& g, const std::string& var,
                             const ComplexRegion& region, double bigR) {
    ZeroCountAt out;
    double scale = regionScale(region);
    std::vector<CurveSeg> segs;
    ComplexRegion search;

    switch (region.kind) {
        case ComplexRegion::Kind::Disk: {
            if (!(region.radius > 0)) {
                out.error = "圆盘半径必须为正";
                return out;
            }
            segs.push_back(makeCircle(region.center, region.radius, 0.0, kTwoPi));
            search = ComplexRegion::disk(region.center, region.radius + 1e-3 * scale + 1e-12);
            break;
        }
        case ComplexRegion::Kind::Annulus: {
            double ri = region.radius;
            double ro = region.outerRadius;
            if (!(ro > std::max(ri, 0.0))) {
                out.error = "圆环内外半径不合理";
                return out;
            }
            segs.push_back(makeCircle(region.center, ro, 0.0, kTwoPi));
            if (ri > 0) segs.push_back(makeCircle(region.center, ri, kTwoPi, 0.0));
            search = ComplexRegion::disk(region.center, ro + 1e-3 * scale + 1e-12);
            break;
        }
        case ComplexRegion::Kind::Rectangle: {
            double x0 = std::min(region.x0, region.x1);
            double x1 = std::max(region.x0, region.x1);
            double y0 = std::min(region.y0, region.y1);
            double y1 = std::max(region.y0, region.y1);
            segs.push_back(makeLine(Cplx(x0, y0), Cplx(x1, y0)));
            segs.push_back(makeLine(Cplx(x1, y0), Cplx(x1, y1)));
            segs.push_back(makeLine(Cplx(x1, y1), Cplx(x0, y1)));
            segs.push_back(makeLine(Cplx(x0, y1), Cplx(x0, y0)));
            double d = 1e-3 * scale + 1e-12;
            search = ComplexRegion::rectangle(x0 - d, x1 + d, y0 - d, y1 + d);
            break;
        }
        case ComplexRegion::Kind::HalfPlaneUpper: {
            double R = bigR > 0 ? bigR : 20.0;
            Cplx c(0.0, region.yLine);
            segs.push_back(makeLine(Cplx(-R, region.yLine), Cplx(R, region.yLine)));
            segs.push_back(makeCircle(c, R, 0.0, kPi));
            search = ComplexRegion::rectangle(-R * 1.05, R * 1.05,
                                              region.yLine - 1e-3 * scale - 1e-12,
                                              region.yLine + R * 1.05);
            break;
        }
        case ComplexRegion::Kind::HalfPlaneLower: {
            double R = bigR > 0 ? bigR : 20.0;
            Cplx c(0.0, region.yLine);
            segs.push_back(makeLine(Cplx(R, region.yLine), Cplx(-R, region.yLine)));
            segs.push_back(makeCircle(c, R, kPi, kTwoPi));
            search = ComplexRegion::rectangle(-R * 1.05, R * 1.05,
                                              region.yLine - R * 1.05,
                                              region.yLine + 1e-3 * scale + 1e-12);
            break;
        }
        case ComplexRegion::Kind::HalfPlaneRight: {
            double R = bigR > 0 ? bigR : 20.0;
            Cplx c(region.xLine, 0.0);
            segs.push_back(makeLine(Cplx(region.xLine, R), Cplx(region.xLine, -R)));
            segs.push_back(makeCircle(c, R, -kPi / 2.0, kPi / 2.0));
            search = ComplexRegion::rectangle(region.xLine - 1e-3 * scale - 1e-12,
                                              region.xLine + R * 1.05, -R * 1.05, R * 1.05);
            break;
        }
        case ComplexRegion::Kind::Plane: {
            double R = bigR > 0 ? bigR : 20.0;
            segs.push_back(makeCircle(Cplx(0.0, 0.0), R, 0.0, kTwoPi));
            search = ComplexRegion::disk(Cplx(0.0, 0.0), R * 1.05);
            break;
        }
        case ComplexRegion::Kind::ExteriorOfDisk:
            out.error = "无界区域（圆外）暂不支持零点计数";
            return out;
    }

    // 1) 奇点候选（含边界上的）
    std::vector<RawCandidate> cands = collectCandidatePoints(f, var, search);
    double tolB = 1e-6 * std::max(1.0, scale);
    std::vector<size_t> insideIdx;
    for (size_t i = 0; i < cands.size(); ++i) {
        if (nearContour(region, cands[i].z, bigR, tolB)) {
            out.error = "围道上有极点或奇点（" + pointStr(cands[i].z) + "），辐角原理不适用";
            return out;
        }
        if (insideContour(region, cands[i].z, bigR)) insideIdx.push_back(i);
    }

    // 2) 区域内极点个数（含重数）
    long long poleCount = 0;
    if (!insideIdx.empty()) {
        std::vector<SingularPoint> sps = classifyCandidates(f, var, cands);
        for (size_t idx : insideIdx) {
            if (idx >= sps.size()) continue;
            const SingularPoint& sp = sps[idx];
            if (sp.essential) {
                out.error = "区域内有本质奇点或分支点（" + pointStr(sp.z) + "），无法用辐角原理计数";
                return out;
            }
            if (sp.poleOrder > 0) poleCount += sp.poleOrder;
        }
    }

    // 3) 边界上 |f| 采样：检测零点与不可求值点
    {
        std::vector<double> mags;
        const int perSeg = 400;
        for (const auto& seg : segs) {
            for (int i = 0; i < perSeg; ++i) {
                Cplx z = seg.pos((double)i / perSeg);
                Cplx v;
                if (!evalAt(f, var, z, &v)) {
                    out.error = "围道上有不可求值点（可能有奇点或零点）";
                    return out;
                }
                mags.push_back(v.abs());
            }
        }
        if (!mags.empty()) {
            std::vector<double> tmp = mags;
            size_t mid = tmp.size() / 2;
            std::nth_element(tmp.begin(), tmp.begin() + static_cast<std::ptrdiff_t>(mid),
                             tmp.end());
            double median = tmp[mid];
            double mn = *std::min_element(mags.begin(), mags.end());
            if (median > 0.0 && mn < 1e-6 * median) {
                out.error = "围道上有零点，辐角原理的结果不唯一";
                return out;
            }
        }
    }

    // 4) 辐角原理积分
    Cplx W;
    std::string err;
    if (!computeWinding(g, var, segs, &W, &err)) {
        out.error = err;
        return out;
    }
    double wr = W.re;
    double wi = W.im;
    double rounded = std::round(wr);
    if (std::fabs(wi) > 0.01 || std::fabs(wr - rounded) > 0.01) {
        out.error = "辐角变化不是 2π 的整数倍（围道上有零点或极点）";
        return out;
    }
    out.n = (long long)rounded + poleCount;
    out.ok = true;
    return out;
}

}  // namespace

// =================================================================
// ComplexRegion
// =================================================================
bool ComplexRegion::contains(Cplx z, double tol) const {
    if (!z.isFinite()) return false;
    double dist = (z - center).abs();
    switch (kind) {
        case Kind::Disk:
            return dist < radius + tol;
        case Kind::Annulus:
            return dist > radius - tol && dist < outerRadius + tol;
        case Kind::Rectangle:
            return z.re > x0 - tol && z.re < x1 + tol && z.im > y0 - tol && z.im < y1 + tol;
        case Kind::HalfPlaneUpper:
            return z.im > yLine - tol;
        case Kind::HalfPlaneLower:
            return z.im < yLine + tol;
        case Kind::HalfPlaneRight:
            return z.re > xLine - tol;
        case Kind::ExteriorOfDisk:
            return dist > radius - tol;
        case Kind::Plane:
            return true;
    }
    return false;
}

bool ComplexRegion::isEmpty() const {
    switch (kind) {
        case Kind::Disk:
            return !(radius > 0.0);
        case Kind::Annulus:
            return !(outerRadius > 0.0) || !(outerRadius > radius);
        case Kind::Rectangle:
            return !(x1 > x0) || !(y1 > y0);
        case Kind::HalfPlaneUpper:
        case Kind::HalfPlaneLower:
        case Kind::HalfPlaneRight:
        case Kind::ExteriorOfDisk:
        case Kind::Plane:
            return false;
    }
    return false;
}

std::string ComplexRegion::str() const {
    switch (kind) {
        case Kind::Disk:
            return "|" + centerExpr(center) + "| < " + fmtReal(radius);
        case Kind::Annulus:
            return fmtReal(radius) + " < |" + centerExpr(center) + "| < " + fmtReal(outerRadius);
        case Kind::Rectangle:
            return fmtReal(x0) + " < Re z < " + fmtReal(x1) + ", " + fmtReal(y0) +
                   " < Im z < " + fmtReal(y1);
        case Kind::HalfPlaneUpper:
            if (std::fabs(yLine) < 1e-12) return "上半平面 Im z > 0";
            return "半平面 Im z > " + fmtReal(yLine);
        case Kind::HalfPlaneLower:
            if (std::fabs(yLine) < 1e-12) return "下半平面 Im z < 0";
            return "半平面 Im z < " + fmtReal(yLine);
        case Kind::HalfPlaneRight:
            if (std::fabs(xLine) < 1e-12) return "右半平面 Re z > 0";
            return "半平面 Re z > " + fmtReal(xLine);
        case Kind::ExteriorOfDisk:
            return "|" + centerExpr(center) + "| > " + fmtReal(radius);
        case Kind::Plane:
            return "整个复平面";
    }
    return "未知区域";
}

ComplexRegion ComplexRegion::disk(Cplx c, double r) {
    ComplexRegion g;
    g.kind = Kind::Disk;
    g.center = c;
    g.radius = r;
    return g;
}

ComplexRegion ComplexRegion::annulus(Cplx c, double rIn, double rOut) {
    ComplexRegion g;
    g.kind = Kind::Annulus;
    g.center = c;
    g.radius = rIn;
    g.outerRadius = rOut;
    return g;
}

ComplexRegion ComplexRegion::rectangle(double ax, double bx, double ay, double by) {
    ComplexRegion g;
    g.kind = Kind::Rectangle;
    g.x0 = ax;
    g.x1 = bx;
    g.y0 = ay;
    g.y1 = by;
    return g;
}

ComplexRegion ComplexRegion::wholePlane() {
    ComplexRegion g;
    g.kind = Kind::Plane;
    return g;
}

// =================================================================
// 全纯性
// =================================================================
bool opIsHolomorphic(Op op) {
    int i = static_cast<int>(op);
    if (i < 0 || i >= kOpCount) return false;
    switch (op) {
        // 这些算子在复域内不是解析函数（abs 在实轴上解析，但复平面上不解析）
        case Op::Re:
        case Op::Im:
        case Op::Conj:
        case Op::Arg:
        case Op::Abs:
        case Op::Min:
        case Op::Max:
        case Op::Floor:
        case Op::Ceil:
        case Op::Sign:
        case Op::Mod:
            return false;
        default:
            return true;
    }
}

bool builtFromHolomorphicOps(const ExprP& f, Op* badOp) {
    if (!f) return true;
    bool ok = true;
    Op bad = Op::COUNT;
    visit(f, [&](const ExprP& e) {
        if (!ok) return;
        if (!e || !opIsHolomorphic(e->op)) {
            ok = false;
            bad = e ? e->op : Op::COUNT;
        }
    });
    if (!ok && badOp) *badOp = bad;
    return ok;
}

double cauchyRiemannResidual(const ExprP& f, const std::string& var, const ComplexRegion& region,
                             int samples) {
    CrScanResult r = scanCauchyRiemann(f, var, region, samples);
    if (!r.any) return 1e3;  // 所有采样点都无法求值：返回"大残差"标记
    return r.residual;
}

ComplexCheck isHolomorphicOn(const ExprP& f, const std::string& var, const ComplexRegion& region) {
    ComplexCheck r;
    if (!f) {
        r.reason = "空表达式";
        return r;
    }
    if (region.isEmpty()) {
        r.reason = "区域为空，无法判定解析性";
        return r;
    }
    Op bad = Op::COUNT;
    if (!builtFromHolomorphicOps(f, &bad)) {
        r.reason = std::string("含非解析算子 ") + opName(bad) + "（在复平面上不满足柯西-黎曼方程）";
        return r;
    }
    CrScanResult cr = scanCauchyRiemann(f, var, region, 48, 1e-5);
    if (!cr.any) {
        r.reason = "区域内的采样点都无法求值";
        return r;
    }
    if (cr.residual > 1e-5) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3g", cr.residual);
        r.reason = "柯西-黎曼方程不成立（" + pointStr(cr.worst) + " 处相对残差 " + buf +
                   " > 1e-5）";
        return r;
    }
    // 分支割线：局部柯西-黎曼方程在割线两侧各自成立，但函数跨割线不连续 → 不解析
    if (expressionHasBranchCuts(f) && crossesBranchCut(f, var, region)) {
        r.reason = "区域跨过分支割线（ln/sqrt/反三角等主值函数在割线两侧不连续），不解析";
        return r;
    }
    std::vector<SingularPoint> sing = singularitiesIn(f, var, region);
    for (const auto& s : sing) {
        if (s.essential) {
            r.reason = pointStr(s.z) + " 处有本质奇点/分支点，不是解析点";
            return r;
        }
        if (s.poleOrder > 0) {
            r.reason = pointStr(s.z) + " 处有 " + std::to_string(s.poleOrder) + " 阶极点，不是解析点";
            return r;
        }
        // poleOrder == 0：可去奇点，解析延拓后全纯，放行
    }
    r.ok = true;
    return r;
}

// =================================================================
// 奇点 / 极点 / 留数
// =================================================================
std::vector<SingularPoint> singularitiesIn(const ExprP& f, const std::string& var,
                                           const ComplexRegion& region) {
    std::vector<SingularPoint> out;
    if (!f || region.isEmpty()) return out;
    std::vector<RawCandidate> raw = collectCandidatePoints(f, var, region);
    std::vector<RawCandidate> inside;
    for (auto& c : raw) {
        if (region.contains(c.z, -1e-9 * (1.0 + c.z.abs()))) inside.push_back(c);
    }
    if (inside.empty()) return out;
    // 用全部候选（含区域外的）估算隔离半径，避免把相邻奇点圈进积分圆
    std::vector<SingularPoint> classified = classifyCandidates(f, var, raw);
    for (auto& c : inside) {
        for (size_t i = 0; i < raw.size() && i < classified.size(); ++i) {
            if ((raw[i].z - c.z).abs() <= 1e-12 * (1.0 + c.z.abs())) {
                out.push_back(classified[i]);
                break;
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const SingularPoint& a, const SingularPoint& b) {
        if (a.z.re != b.z.re) return a.z.re < b.z.re;
        return a.z.im < b.z.im;
    });
    return out;
}

CplxResult evalComplex(const ExprP& f, const std::string& var, Cplx z) {
    CplxResult r;
    if (!f) {
        r.error = "空表达式";
        return r;
    }
    if (!z.isFinite()) {
        r.error = "求值点不是有限复数";
        return r;
    }
    VarMap vars;
    vars[var] = z;
    EvalResult e = evaluate(f, vars);
    if (!e.ok) {
        r.error = e.error;
        return r;
    }
    if (!e.value.isFinite()) {
        r.error = "结果不是有限复数";
        return r;
    }
    r.ok = true;
    r.value = e.value;
    return r;
}

CplxResult residueAt(const ExprP& f, const std::string& var, Cplx z0) {
    CplxResult res;
    if (!f) {
        res.error = "空表达式";
        return res;
    }
    if (!z0.isFinite()) {
        res.error = "留数点不是有限复数";
        return res;
    }
    // 找其它奇点，确定隔离半径（圆内不能圈住别的奇点）
    double r1 = 1e-2;
    {
        ComplexRegion search = ComplexRegion::disk(z0, 2.0);
        std::vector<RawCandidate> cands = collectCandidatePoints(f, var, search);
        double dmin = std::numeric_limits<double>::infinity();
        for (auto& c : cands) {
            double d = (c.z - z0).abs();
            if (d > 1e-7 * (1.0 + z0.abs())) dmin = std::min(dmin, d);
        }
        if (std::isfinite(dmin)) r1 = std::min(r1, dmin / 3.0);
        if (r1 < 1e-7) r1 = 1e-7;
    }
    const int nTry = 3;
    double radii[nTry] = {r1, r1 * 0.5, r1 * 0.25};
    Cplx vals[nTry];
    bool oks[nTry] = {false, false, false};
    for (int i = 0; i < nTry; ++i) {
        Cplx integral;
        std::string err;
        if (!circleIntegralOnce(f, var, z0, radii[i], 512, &integral, &err)) continue;
        vals[i] = integral / Cplx(0.0, kTwoPi);
        oks[i] = true;
    }
    // 从最小的两个半径开始找稳定结果（半径越小越不容易圈进别的奇点）
    for (int i = nTry - 1; i >= 1; --i) {
        if (oks[i] && oks[i - 1] && cplxClose(vals[i], vals[i - 1], 1e-6)) {
            res.ok = true;
            res.value = vals[i];
            return res;
        }
    }
    res.error = "留数计算结果不稳定（z0 附近可能有其它奇点或本质奇点）";
    return res;
}

CplxResult contourIntegral(const ExprP& f, const std::string& var, Cplx center, double radius) {
    CplxResult res;
    if (!f) {
        res.error = "空表达式";
        return res;
    }
    if (!(radius > 0.0) || !std::isfinite(radius)) {
        res.error = "围道半径必须为正数";
        return res;
    }
    if (!center.isFinite()) {
        res.error = "圆心不是有限复数";
        return res;
    }
    // 围道上的奇点/分支点检查
    double scale = std::max(1.0, radius);
    ComplexRegion search = ComplexRegion::disk(center, radius * 1.1 + 1e-6);
    std::vector<RawCandidate> cands = collectCandidatePoints(f, var, search);
    double tolB = 1e-6 * scale;
    for (auto& c : cands) {
        if (std::fabs((c.z - center).abs() - radius) <= tolB) {
            res.error = "围道 |z - c| = r 上有点不可解析（极点或分支点），积分不收敛";
            return res;
        }
    }
    // 梯形公式 + 自适应加密
    Cplx prev;
    bool have = false;
    const int sizes[4] = {256, 512, 1024, 2048};
    for (int si = 0; si < 4; ++si) {
        int n = sizes[si];
        Cplx part;
        std::string err;
        if (!circleIntegralOnce(f, var, center, radius, n, &part, &err)) {
            res.error = err;
            return res;
        }
        if (have && (part - prev).abs() <= 1e-8 * std::max(1.0, part.abs())) {
            res.ok = true;
            res.value = part;
            return res;
        }
        prev = part;
        have = true;
    }
    res.error = "围道积分数值不稳定（围道离奇点太近）";
    return res;
}

// =================================================================
// 零点计数
// =================================================================
IntResult zeroCountIn(const ExprP& f, const std::string& var, const ComplexRegion& region) {
    IntResult res;
    if (!f) {
        res.error = "空表达式";
        return res;
    }
    if (region.isEmpty()) {
        res.ok = true;
        res.value = 0;
        return res;
    }
    if (!builtFromHolomorphicOps(f, nullptr)) {
        res.error = "表达式含非全纯算子，辐角原理不适用";
        return res;
    }
    if (expressionHasBranchCuts(f) && crossesBranchCut(f, var, region)) {
        res.error = "区域跨过分支割线（ln/sqrt 等主值函数在割线两侧不连续），辐角原理不适用";
        return res;
    }
    if (region.kind == ComplexRegion::Kind::ExteriorOfDisk) {
        res.error = "无界区域（圆外）暂不支持零点计数";
        return res;
    }
    if (!hasVar(f)) {
        Cplx cv;
        if (!constComplex(f, &cv)) {
            res.error = "常数表达式无法求值";
            return res;
        }
        if (cv.abs() < 1e-12) {
            res.error = "零函数有无穷多个零点";
            return res;
        }
        res.ok = true;
        res.value = 0;
        return res;
    }
    ExprP df = diff(f, var);
    if (!df) {
        res.error = "符号求导失败";
        return res;
    }
    ExprP g = mkDiv(df, f);

    // 退化圆环（内半径为 0）在计数上按去心圆盘处理：等价于圆盘（含圆心的极点）
    ComplexRegion reg = region;
    if (reg.kind == ComplexRegion::Kind::Annulus && !(reg.radius > 0.0))
        reg = ComplexRegion::disk(reg.center, reg.outerRadius);

    bool unbounded = reg.kind == ComplexRegion::Kind::HalfPlaneUpper ||
                     reg.kind == ComplexRegion::Kind::HalfPlaneLower ||
                     reg.kind == ComplexRegion::Kind::HalfPlaneRight ||
                     reg.kind == ComplexRegion::Kind::Plane;
    if (!unbounded) {
        ZeroCountAt a = zeroCountAtScale(f, g, var, reg, 0.0);
        if (!a.ok) {
            res.error = a.error;
            return res;
        }
        res.ok = true;
        res.value = (int)a.n;
        return res;
    }
    // 无界区域：大围道近似，两个尺度结果一致才接受
    ZeroCountAt a = zeroCountAtScale(f, g, var, reg, 20.0);
    if (!a.ok) {
        res.error = a.error;
        return res;
    }
    ZeroCountAt b = zeroCountAtScale(f, g, var, reg, 200.0);
    if (!b.ok) {
        res.error = b.error;
        return res;
    }
    if (a.n != b.n) {
        res.error = "区域无界，零点计数在大围道下不收敛";
        return res;
    }
    res.ok = true;
    res.value = (int)a.n;
    return res;
}

// =================================================================
// 其他
// =================================================================
ModRangeResult modulusRangeOn(const ExprP& f, const std::string& var, const ComplexRegion& region) {
    ModRangeResult res;
    if (!f) {
        res.error = "空表达式";
        return res;
    }
    if (region.isEmpty()) {
        res.error = "区域为空";
        return res;
    }
    // 区域内若有极点，|f| 无界
    std::vector<SingularPoint> sing = singularitiesIn(f, var, region);
    for (const auto& s : sing) {
        if (s.essential || s.poleOrder > 0) {
            res.error = "区域内有奇点（" + pointStr(s.z) + "），|f(z)| 无界";
            return res;
        }
    }
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    long long count = 0;
    auto consider = [&](Cplx z) {
        Cplx v;
        if (!evalAt(f, var, z, &v)) return;
        double m = v.abs();
        if (!std::isfinite(m)) return;
        lo = std::min(lo, m);
        hi = std::max(hi, m);
        ++count;
    };
    double bx0, bx1, by0, by1;
    if (!boundingBox(region, &bx0, &bx1, &by0, &by1)) {
        res.error = "区域几何数据不合理";
        return res;
    }
    const int n = 80;
    for (int iy = 0; iy <= n; ++iy) {
        double y = by0 + (by1 - by0) * ((double)iy + 0.5) / (n + 1);
        for (int ix = 0; ix <= n; ++ix) {
            double x = bx0 + (bx1 - bx0) * ((double)ix + 0.5) / (n + 1);
            Cplx z(x, y);
            if (!region.contains(z, 0.0)) continue;
            consider(z);
        }
    }
    // 边界采样（含边界是任务要求）
    if (region.kind != ComplexRegion::Kind::Plane) {
        std::vector<CurveSeg> segs;
        switch (region.kind) {
            case ComplexRegion::Kind::Disk:
                segs.push_back(makeCircle(region.center, region.radius, 0.0, kTwoPi));
                break;
            case ComplexRegion::Kind::Annulus:
                segs.push_back(makeCircle(region.center, region.outerRadius, 0.0, kTwoPi));
                if (region.radius > 0)
                    segs.push_back(makeCircle(region.center, region.radius, 0.0, kTwoPi));
                break;
            case ComplexRegion::Kind::Rectangle:
                segs.push_back(makeLine(Cplx(region.x0, region.y0), Cplx(region.x1, region.y0)));
                segs.push_back(makeLine(Cplx(region.x1, region.y0), Cplx(region.x1, region.y1)));
                segs.push_back(makeLine(Cplx(region.x1, region.y1), Cplx(region.x0, region.y1)));
                segs.push_back(makeLine(Cplx(region.x0, region.y1), Cplx(region.x0, region.y0)));
                break;
            case ComplexRegion::Kind::HalfPlaneUpper:
                segs.push_back(makeLine(Cplx(-5.0, region.yLine), Cplx(5.0, region.yLine)));
                break;
            case ComplexRegion::Kind::HalfPlaneLower:
                segs.push_back(makeLine(Cplx(-5.0, region.yLine), Cplx(5.0, region.yLine)));
                break;
            case ComplexRegion::Kind::HalfPlaneRight:
                segs.push_back(makeLine(Cplx(region.xLine, -5.0), Cplx(region.xLine, 5.0)));
                break;
            case ComplexRegion::Kind::ExteriorOfDisk:
                segs.push_back(makeCircle(region.center, region.radius, 0.0, kTwoPi));
                break;
            default:
                break;
        }
        for (const auto& seg : segs)
            for (int i = 0; i < 512; ++i) consider(seg.pos((double)i / 512.0));
    }
    if (count == 0) {
        res.error = "区域内没有可求值的采样点";
        return res;
    }
    if (hi > 1e12) {
        res.error = "区域内有奇点，|f(z)| 无界";
        return res;
    }
    res.ok = true;
    res.lo = lo;
    res.hi = hi;
    return res;
}

bool usesComplexOps(const ExprP& f) {
    if (!f) return false;
    bool found = false;
    visit(f, [&](const ExprP& e) {
        if (found || !e) return;
        switch (e->op) {
            case Op::I:
            case Op::Re:
            case Op::Im:
            case Op::Conj:
            case Op::Arg:
                found = true;
                break;
            default:
                break;
        }
    });
    if (found) return true;
    // 退化检查：代入复变量后结果非实数（如 sqrt(-1)、i 隐含在常量里）
    std::set<std::string> vars = collectVars(f);
    const Cplx samples[3] = {Cplx(0.6, 0.8), Cplx(-0.5, 0.3), Cplx(1.2, -0.7)};
    for (const auto& s : samples) {
        VarMap m;
        for (const auto& v : vars) m[v] = s;
        EvalResult r = evaluate(f, m);
        if (!r.ok) continue;
        if (std::fabs(r.value.im) > 1e-9) return true;
    }
    return false;
}

}  // namespace fcg
