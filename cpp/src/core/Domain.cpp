// Domain.cpp — 区间 / 定义域 / 解析性 / 单调性 / 值域 / 零点分析
//
// 实现策略（与任务书一致）：
//   1. 符号推导：递归遍历 AST，对每个节点提取"实数有定义"的条件；
//      条件求解优先用符号反解（一次式、偶数次幂、|·|、单调函数反函数），
//      无法反解时退化为数值扫描 + 二分细化边界。
//   2. 数值校验：对确实触发过数值兜底的表达式，在 [-100,100] 上用 4096 个
//      采样点复核，把符号推导遗漏的区间补回来。
//   3. 零点 / 单调性 / 值域同样采用"符号快路径 + 数值密采样 + 二分/三分细化"。
#include "Domain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Diff.h"
#include "Limit.h"
#include "Numeric.h"

namespace fcg {
namespace {

// ------------------------------------------------------------------
// 常量与基础工具
// ------------------------------------------------------------------
constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kHuge = 1e308;

constexpr double kWindowLo = -100.0;   // 数值扫描窗口
constexpr double kWindowHi = 100.0;
constexpr int kSolverPoints = 2048;    // 条件求解内部扫描点数
constexpr int kValidatePoints = 4096;  // 全表达式数值校验点数

bool nearValue(double a, double b, double relTol = 1e-12) {
    if (a == b) return true;
    if (!std::isfinite(a) || !std::isfinite(b)) return false;
    double s = std::max({1.0, std::fabs(a), std::fabs(b)});
    return std::fabs(a - b) <= relTol * s;
}

// 把接近"漂亮数"的边界吸附过去：整数 / π 的有理倍数 / 简单有理数。
// 这样可以保证 tan 极点、sqrt(1-x^2) 的 ±1 等边界落在数学上精确的位置。
double snapNice(double v) {
    if (!std::isfinite(v)) return v;
    if (std::fabs(v) < 1e-12) return 0.0;
    double scale = std::max(1.0, std::fabs(v));
    double r = std::round(v);
    if (std::fabs(v - r) <= 1e-10 * scale) return r;
    for (int n = 1; n <= 12; ++n) {
        for (int k = -64; k <= 64; ++k) {
            if (k == 0) continue;
            double cand = k * kPi / n;
            if (std::fabs(v - cand) <= 1e-10 * scale) return cand;
        }
    }
    for (int q = 2; q <= 12; ++q) {
        double p = std::round(v * q);
        if (std::fabs(v - p / q) <= 1e-10 * scale) return p / q;
    }
    return v;
}

std::string piLabel(int k, int n) {
    std::string s;
    if (k == 1)
        s = "π";
    else if (k == -1)
        s = "-π";
    else
        s = std::to_string(k) + "π";
    if (n != 1) s += "/" + std::to_string(n);
    return s;
}

std::string rationalLabel(long long p, long long q) {
    if (q == 0) return "?";
    if (q < 0) {
        p = -p;
        q = -q;
    }
    long long a = std::llabs(p), b = q;
    while (b != 0) {
        long long t = a % b;
        a = b;
        b = t;
    }
    long long g = a == 0 ? 1 : a;
    p /= g;
    q /= g;
    if (q == 1) return std::to_string(p);
    return std::to_string(p) + "/" + std::to_string(q);
}

std::string fmtNum(double v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "+∞" : "-∞";
    double s = snapNice(v);
    if (std::fabs(s - v) <= 1e-12 * std::max(1.0, std::fabs(v))) v = s;
    if (std::fabs(v) < 1e-12) return "0";
    double r = std::round(v);
    if (std::fabs(v - r) <= 1e-10 * std::max(1.0, std::fabs(v))) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", r);
        return buf;
    }
    for (int n = 1; n <= 12; ++n) {
        for (int k = -64; k <= 64; ++k) {
            if (k == 0) continue;
            double cand = k * kPi / n;
            if (std::fabs(v - cand) <= 1e-10 * std::max(1.0, std::fabs(v))) return piLabel(k, n);
        }
    }
    for (int k = -4; k <= 4; ++k) {
        if (k == 0 || k == 1 || k == -1) continue;
        double cand = k * 2.71828182845904523536;
        if (std::fabs(v - cand) <= 1e-10 * std::max(1.0, std::fabs(v))) return std::to_string(k) + "e";
    }
    if (std::fabs(v - 2.71828182845904523536) <= 1e-10 * std::max(1.0, std::fabs(v))) return "e";
    if (std::fabs(v + 2.71828182845904523536) <= 1e-10 * std::max(1.0, std::fabs(v))) return "-e";
    for (int q = 2; q <= 12; ++q) {
        long long p = static_cast<long long>(std::llround(v * q));
        if (std::fabs(v - static_cast<double>(p) / q) <= 1e-10 * std::max(1.0, std::fabs(v)))
            return rationalLabel(p, q);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

// ------------------------------------------------------------------
// 区间构造与集合运算
// ------------------------------------------------------------------
Interval makeInterval(bool loInf, double lo, bool loClosed, bool hiInf, double hi, bool hiClosed) {
    Interval iv;
    iv.loInf = loInf;
    iv.lo = loInf ? -kHuge : lo;
    iv.loClosed = loClosed;
    iv.hiInf = hiInf;
    iv.hi = hiInf ? kHuge : hi;
    iv.hiClosed = hiClosed;
    return iv;
}

int cmpLo(const Interval& a, const Interval& b) {
    if (a.loInf && b.loInf) return 0;
    if (a.loInf) return -1;
    if (b.loInf) return 1;
    if (a.lo < b.lo) return -1;
    if (a.lo > b.lo) return 1;
    return 0;
}

int cmpHi(const Interval& a, const Interval& b) {
    if (a.hiInf && b.hiInf) return 0;
    if (a.hiInf) return 1;
    if (b.hiInf) return -1;
    if (a.hi < b.hi) return -1;
    if (a.hi > b.hi) return 1;
    return 0;
}

Interval intersectInterval(const Interval& a, const Interval& b) {
    Interval r;
    // 下端点取较大者
    if (a.loInf && b.loInf) {
        r.loInf = true;
        r.lo = -kHuge;
        r.loClosed = true;
    } else if (a.loInf) {
        r.loInf = false;
        r.lo = b.lo;
        r.loClosed = b.loClosed;
    } else if (b.loInf) {
        r.loInf = false;
        r.lo = a.lo;
        r.loClosed = a.loClosed;
    } else if (a.lo > b.lo) {
        r.loInf = false;
        r.lo = a.lo;
        r.loClosed = a.loClosed;
    } else if (a.lo < b.lo) {
        r.loInf = false;
        r.lo = b.lo;
        r.loClosed = b.loClosed;
    } else {
        r.loInf = false;
        r.lo = a.lo;
        r.loClosed = a.loClosed && b.loClosed;
    }
    // 上端点取较小者
    if (a.hiInf && b.hiInf) {
        r.hiInf = true;
        r.hi = kHuge;
        r.hiClosed = true;
    } else if (a.hiInf) {
        r.hiInf = false;
        r.hi = b.hi;
        r.hiClosed = b.hiClosed;
    } else if (b.hiInf) {
        r.hiInf = false;
        r.hi = a.hi;
        r.hiClosed = a.hiClosed;
    } else if (a.hi < b.hi) {
        r.hiInf = false;
        r.hi = a.hi;
        r.hiClosed = a.hiClosed;
    } else if (a.hi > b.hi) {
        r.hiInf = false;
        r.hi = b.hi;
        r.hiClosed = b.hiClosed;
    } else {
        r.hiInf = false;
        r.hi = a.hi;
        r.hiClosed = a.hiClosed && b.hiClosed;
    }
    return r;
}

// a \ b，结果最多两段
std::vector<Interval> subtractInterval(const Interval& a, const Interval& b) {
    std::vector<Interval> out;
    if (a.isEmpty()) return out;
    if (b.isEmpty()) {
        out.push_back(a);
        return out;
    }
    bool noOverlap = false;
    if (!b.hiInf) {
        if (a.loInf) {
            // a 向左无限，b 有上界 → 必相交（除非 a 上界比 b 上界还小，下面处理）
            if (!a.hiInf && (b.hi < a.lo || (b.hi == a.lo && !(b.hiClosed && a.loClosed)))) noOverlap = true;
        } else if (b.hi < a.lo) {
            noOverlap = true;
        } else if (b.hi == a.lo && !(b.hiClosed && a.loClosed)) {
            noOverlap = true;
        }
    }
    if (!noOverlap && !b.loInf) {
        if (a.hiInf) {
            // a 上界无穷
        } else if (b.lo > a.hi || (b.lo == a.hi && !(b.loClosed && a.hiClosed))) {
            noOverlap = true;
        }
    }
    if (noOverlap) {
        out.push_back(a);
        return out;
    }
    if (!b.loInf) {
        Interval left = makeInterval(a.loInf, a.lo, a.loClosed, false, b.lo, !b.loClosed);
        Interval piece = intersectInterval(a, left);
        if (!piece.isEmpty()) out.push_back(piece);
    }
    if (!b.hiInf) {
        Interval right = makeInterval(false, b.hi, !b.hiClosed, a.hiInf, a.hi, a.hiClosed);
        Interval piece = intersectInterval(a, right);
        if (!piece.isEmpty()) out.push_back(piece);
    }
    return out;
}

Interval mergeTwo(const Interval& a, const Interval& b) {
    Interval r = a;
    if (b.loInf || (!a.loInf && b.lo < a.lo)) {
        r.loInf = b.loInf;
        r.lo = b.loInf ? -kHuge : b.lo;
        r.loClosed = b.loClosed;
    } else if (!a.loInf && !b.loInf && a.lo == b.lo) {
        r.loClosed = a.loClosed || b.loClosed;
    }
    if (b.hiInf || (!a.hiInf && b.hi > a.hi)) {
        r.hiInf = b.hiInf;
        r.hi = b.hiInf ? kHuge : b.hi;
        r.hiClosed = b.hiClosed;
    } else if (!a.hiInf && !b.hiInf && a.hi == b.hi) {
        r.hiClosed = a.hiClosed || b.hiClosed;
    }
    return r;
}

// ------------------------------------------------------------------
// 求值 / 结构工具
// ------------------------------------------------------------------
bool evalAt(const ExprP& e, const std::string& var, double x, double* out) {
    if (!e || !std::isfinite(x)) return false;
    RealResult r = evalReal(e, var, x);
    if (!r.ok || !std::isfinite(r.value)) return false;
    if (out) *out = r.value;
    return true;
}

bool constValue(const ExprP& e, double* out) {
    if (!e || !isConst(e)) return false;
    VarMap vars;
    EvalResult r = evaluate(e, vars);
    if (!r.ok || !r.value.isFinite()) return false;
    if (std::fabs(r.value.im) > 1e-9) return false;
    if (out) *out = r.value.re;
    return true;
}

bool hasOtherVar(const ExprP& e, const std::string& var) {
    if (!e) return false;
    for (const auto& name : collectVars(e)) {
        if (name != var) return true;
    }
    return false;
}

bool allOpsAnalytic(const ExprP& e) {
    bool ok = true;
    visit(e, [&ok](const ExprP& n) {
        if (!n) return;
        if (!opIsAnalytic(n->op)) ok = false;
    });
    return ok;
}

// 数值兜底标记：domainOf 里只有真正用过数值方法才做 4096 点复核
thread_local bool g_usedNumeric = false;

// ------------------------------------------------------------------
// 一次式提取：a*x + b
// ------------------------------------------------------------------
struct Affine {
    bool ok = false;
    double a = 0.0;
    double b = 0.0;
};

Affine asAffine(const ExprP& e, const std::string& var) {
    Affine r;
    if (!e) return r;
    if (e->op == Op::Var) {
        if (e->var == var) {
            r.ok = true;
            r.a = 1.0;
            r.b = 0.0;
        }
        return r;
    }
    double c;
    if (isConst(e)) {
        if (constValue(e, &c)) {
            r.ok = true;
            r.a = 0.0;
            r.b = c;
        }
        return r;
    }
    switch (e->op) {
        case Op::Neg: {
            Affine x = asAffine(e->a, var);
            if (x.ok) {
                r.ok = true;
                r.a = -x.a;
                r.b = -x.b;
            }
            return r;
        }
        case Op::Add:
        case Op::Sub: {
            Affine x = asAffine(e->a, var);
            Affine y = asAffine(e->b, var);
            if (x.ok && y.ok) {
                r.ok = true;
                r.a = x.a + y.a;
                r.b = x.b + (e->op == Op::Add ? y.b : -y.b);
            }
            return r;
        }
        case Op::Mul: {
            Affine x = asAffine(e->a, var);
            Affine y = asAffine(e->b, var);
            if (!x.ok || !y.ok) return r;
            if (x.a == 0.0) {
                r.ok = true;
                r.a = x.b * y.a;
                r.b = x.b * y.b;
            } else if (y.a == 0.0) {
                r.ok = true;
                r.a = y.b * x.a;
                r.b = y.b * x.b;
            }
            return r;
        }
        case Op::Div: {
            Affine x = asAffine(e->a, var);
            Affine y = asAffine(e->b, var);
            if (x.ok && y.ok && y.a == 0.0 && y.b != 0.0) {
                r.ok = true;
                r.a = x.a / y.b;
                r.b = x.b / y.b;
            }
            return r;
        }
        case Op::Pow: {
            Affine x = asAffine(e->a, var);
            Affine y = asAffine(e->b, var);
            if (!x.ok || !y.ok) return r;
            if (x.a == 0.0 && y.a == 0.0) {
                if (constValue(e, &c)) {
                    r.ok = true;
                    r.a = 0.0;
                    r.b = c;
                }
            }
            return r;
        }
        default:
            return r;
    }
}

// ------------------------------------------------------------------
// 值条件
// ------------------------------------------------------------------
struct Cond {
    enum class Kind { Interval, Nonzero };
    Kind kind = Kind::Interval;
    double lo = -kInf;
    double hi = kInf;
    bool loClosed = true;
    bool hiClosed = true;

    bool test(double y) const {
        if (std::isnan(y)) return false;
        if (kind == Kind::Nonzero) return y != 0.0;
        if (y < lo) return false;
        if (y > hi) return false;
        if (y == lo && !loClosed) return false;
        if (y == hi && !hiClosed) return false;
        return true;
    }
};

Cond condInterval(double lo, bool loClosed, double hi, bool hiClosed) {
    Cond c;
    c.kind = Cond::Kind::Interval;
    c.lo = lo;
    c.hi = hi;
    c.loClosed = loClosed;
    c.hiClosed = hiClosed;
    return c;
}

Cond condPositive() { return condInterval(0.0, false, kInf, true); }
Cond condNonnegative() { return condInterval(0.0, true, kInf, true); }
Cond condNegative() { return condInterval(-kInf, true, 0.0, false); }
Cond condNonpositive() { return condInterval(-kInf, true, 0.0, true); }
Cond condBetweenClosed(double a, double b) { return condInterval(a, true, b, true); }
Cond condBetweenOpen(double a, double b) { return condInterval(a, false, b, false); }
Cond condAtLeast(double a) { return condInterval(a, true, kInf, true); }

Cond condNonzero() {
    Cond c;
    c.kind = Cond::Kind::Nonzero;
    return c;
}

bool condIsEmpty(const Cond& c) {
    if (c.kind != Cond::Kind::Interval) return false;
    if (c.lo > c.hi) return true;
    if (c.lo == c.hi && !(c.loClosed && c.hiClosed)) return true;
    return false;
}

// 关于原点镜像：{-y : y ∈ C}
Cond mirrorCond(const Cond& c) {
    if (c.kind != Cond::Kind::Interval) return c;
    return condInterval(-c.hi, c.hiClosed, -c.lo, c.loClosed);
}

// ------------------------------------------------------------------
// 单调函数映射表：用于通过反函数把条件反推到自变量
// ------------------------------------------------------------------
struct MonotoneMap {
    bool increasing = true;
    double rangeLo = -kInf;
    bool rangeLoClosed = true;
    double rangeHi = kInf;
    bool rangeHiClosed = true;
    double domainLo = -kInf;
    bool domainLoClosed = true;
    double domainHi = kInf;
    bool domainHiClosed = true;
    double (*inv)(double) = nullptr;
};

bool monotoneMapFor(Op op, MonotoneMap* out) {
    MonotoneMap m;
    switch (op) {
        case Op::Sqrt:
            m.rangeLo = 0.0;
            m.rangeLoClosed = true;
            m.inv = [](double y) { return y * y; };
            m.domainLo = 0.0;
            m.domainLoClosed = true;
            break;
        case Op::Cbrt:
            m.inv = [](double y) { return y * y * y; };
            break;
        case Op::Exp:
            m.rangeLo = 0.0;
            m.rangeLoClosed = false;
            m.inv = [](double y) { return std::log(y); };
            break;
        case Op::Ln:
            m.domainLo = 0.0;
            m.domainLoClosed = false;
            m.inv = [](double y) { return std::exp(y); };
            break;
        case Op::Log2:
            m.domainLo = 0.0;
            m.domainLoClosed = false;
            m.inv = [](double y) { return std::pow(2.0, y); };
            break;
        case Op::Log10:
            m.domainLo = 0.0;
            m.domainLoClosed = false;
            m.inv = [](double y) { return std::pow(10.0, y); };
            break;
        case Op::Asin:
            m.rangeLo = -kPi / 2.0;
            m.rangeLoClosed = true;
            m.rangeHi = kPi / 2.0;
            m.rangeHiClosed = true;
            m.domainLo = -1.0;
            m.domainLoClosed = true;
            m.domainHi = 1.0;
            m.domainHiClosed = true;
            m.inv = [](double y) { return std::sin(y); };
            break;
        case Op::Acos:
            m.increasing = false;
            m.rangeLo = 0.0;
            m.rangeLoClosed = true;
            m.rangeHi = kPi;
            m.rangeHiClosed = true;
            m.domainLo = -1.0;
            m.domainLoClosed = true;
            m.domainHi = 1.0;
            m.domainHiClosed = true;
            m.inv = [](double y) { return std::cos(y); };
            break;
        case Op::Atan:
            m.rangeLo = -kPi / 2.0;
            m.rangeLoClosed = false;
            m.rangeHi = kPi / 2.0;
            m.rangeHiClosed = false;
            m.inv = [](double y) { return std::tan(y); };
            break;
        case Op::Sinh:
            m.inv = [](double y) { return std::asinh(y); };
            break;
        case Op::Asinh:
            m.inv = [](double y) { return std::sinh(y); };
            break;
        case Op::Tanh:
            m.rangeLo = -1.0;
            m.rangeLoClosed = false;
            m.rangeHi = 1.0;
            m.rangeHiClosed = false;
            m.inv = [](double y) { return std::atanh(y); };
            break;
        case Op::Atanh:
            m.domainLo = -1.0;
            m.domainLoClosed = false;
            m.domainHi = 1.0;
            m.domainHiClosed = false;
            m.inv = [](double y) { return std::tanh(y); };
            break;
        case Op::Acosh:
            m.rangeLo = 0.0;
            m.rangeLoClosed = true;
            m.domainLo = 1.0;
            m.domainLoClosed = true;
            m.inv = [](double y) { return std::cosh(y); };
            break;
        default:
            return false;
    }
    *out = m;
    return true;
}

// ------------------------------------------------------------------
// 数值扫描基础设施
// ------------------------------------------------------------------
std::vector<double> solverGrid() {
    static const std::vector<double> grid = [] {
        std::vector<double> xs;
        xs.reserve(static_cast<size_t>(kSolverPoints) + 96);
        for (int i = 0; i <= kSolverPoints; ++i) {
            xs.push_back(kWindowLo + (kWindowHi - kWindowLo) * (static_cast<double>(i) / kSolverPoints));
        }
        for (int k = 1; k <= 24; ++k) {
            double p = 100.0 * std::pow(2.0, k);
            if (p > 1e9) break;
            xs.push_back(p);
            xs.push_back(-p);
        }
        for (double p : {1e10, 1e12, 1e15, 1e20, 1e30}) {
            xs.push_back(p);
            xs.push_back(-p);
        }
        std::sort(xs.begin(), xs.end());
        return xs;
    }();
    return grid;
}

// 在 pred(xInvalid) != pred(xValid) 的两点之间二分，返回"有效一侧"的边界
double refineTransition(const std::function<bool(double)>& pred, double xInvalid, double xValid) {
    double a = xInvalid, b = xValid;
    for (int i = 0; i < 200; ++i) {
        double mid = a + (b - a) * 0.5;
        if (mid == a || mid == b) break;
        if (pred(mid))
            b = mid;
        else
            a = mid;
    }
    return b;
}

bool probeExtends(const std::function<bool(double)>& pred, double sign) {
    static const double probes[] = {1e6, 1e9, 1e12, 1e16, 1e20, 1e30};
    for (double p : probes) {
        double x = sign * p;
        if (!std::isfinite(x)) continue;
        if (!pred(x)) return false;
    }
    return true;
}

// 用谓词构造定义域（数值兜底路径）
DomainSet domainFromPredicate(const std::function<bool(double)>& pred) {
    const std::vector<double>& xs = solverGrid();
    const size_t n = xs.size();
    std::vector<unsigned char> ok(n, 0);
    for (size_t i = 0; i < n; ++i) ok[i] = pred(xs[i]) ? 1 : 0;

    std::vector<Interval> parts;
    size_t i = 0;
    while (i < n) {
        if (!ok[i]) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j + 1 < n && ok[j + 1]) ++j;

        Interval iv;
        bool loExt = (i == 0);
        bool hiExt = (j == n - 1);
        if (loExt) {
            if (probeExtends(pred, -1.0)) {
                iv.loInf = true;
                iv.lo = -kHuge;
                iv.loClosed = true;
            } else {
                iv.loInf = false;
                iv.lo = kWindowLo;
                iv.loClosed = true;
            }
        } else {
            double b = snapNice(refineTransition(pred, xs[i - 1], xs[i]));
            iv.loInf = false;
            iv.lo = b;
            iv.loClosed = pred(b);
        }
        if (hiExt) {
            if (probeExtends(pred, 1.0)) {
                iv.hiInf = true;
                iv.hi = kHuge;
                iv.hiClosed = true;
            } else {
                iv.hiInf = false;
                iv.hi = kWindowHi;
                iv.hiClosed = true;
            }
        } else {
            double b = snapNice(refineTransition(pred, xs[j + 1], xs[j]));
            iv.hiInf = false;
            iv.hi = b;
            iv.hiClosed = pred(b);
        }
        if (!iv.isEmpty()) parts.push_back(iv);
        i = j + 1;
    }
    DomainSet d;
    d.parts = std::move(parts);
    d.normalize();
    return d;
}

// ------------------------------------------------------------------
// 定义域递归：前向声明
// ------------------------------------------------------------------
DomainSet domainOfRec(const ExprP& e, const std::string& var);
DomainSet solveCond(const ExprP& e, const Cond& cond, const std::string& var);
DomainSet solveCondNumeric(const ExprP& e, const Cond& cond, const std::string& var);
DomainSet solveTanDomain(const ExprP& arg, const std::string& var);
std::vector<double> tanPoles(const ExprP& arg, const std::string& var);

// ------------------------------------------------------------------
// 集合工具
// ------------------------------------------------------------------
DomainSet intersectDS(const DomainSet& a, const DomainSet& b) {
    DomainSet d;
    if (a.empty() || b.empty()) return d;
    d.parts.reserve(a.parts.size() * b.parts.size());
    for (const auto& x : a.parts) {
        for (const auto& y : b.parts) {
            Interval z = intersectInterval(x, y);
            if (!z.isEmpty()) d.parts.push_back(z);
        }
    }
    d.normalize();
    return d;
}

DomainSet unionDS(const DomainSet& a, const DomainSet& b) {
    DomainSet d = a;
    d.parts.insert(d.parts.end(), b.parts.begin(), b.parts.end());
    d.normalize();
    return d;
}

DomainSet excludePoints(const DomainSet& base, const std::vector<double>& pts) {
    DomainSet d = base;
    for (double p : pts) {
        if (!std::isfinite(p)) continue;
        Interval point = makeInterval(false, p, true, false, p, true);
        std::vector<Interval> next;
        for (const auto& iv : d.parts) {
            auto pieces = subtractInterval(iv, point);
            next.insert(next.end(), pieces.begin(), pieces.end());
        }
        d.parts = std::move(next);
        d.normalize();
    }
    return d;
}

// 条件 → 单个区间（用于 var 直接求解）
DomainSet domainFromCond(const Cond& c) {
    DomainSet d;
    if (c.kind == Cond::Kind::Nonzero) {
        return excludePoints(DomainSet::whole(), {0.0});
    }
    if (condIsEmpty(c)) return d;
    Interval iv;
    if (std::isinf(c.lo)) {
        iv.loInf = true;
        iv.lo = -kHuge;
        iv.loClosed = true;
    } else {
        iv.loInf = false;
        iv.lo = c.lo;
        iv.loClosed = c.loClosed;
    }
    if (std::isinf(c.hi)) {
        iv.hiInf = true;
        iv.hi = kHuge;
        iv.hiClosed = true;
    } else {
        iv.hiInf = false;
        iv.hi = c.hi;
        iv.hiClosed = c.hiClosed;
    }
    if (!iv.isEmpty()) d.parts.push_back(iv);
    return d;
}

// ------------------------------------------------------------------
// 条件求解：一次式
// ------------------------------------------------------------------
DomainSet solveAffineCond(const Affine& aff, const Cond& cond) {
    DomainSet d;
    if (aff.a == 0.0) {
        if (cond.test(aff.b)) return DomainSet::whole();
        return d;
    }
    if (cond.kind == Cond::Kind::Nonzero) {
        double root = -aff.b / aff.a;
        return excludePoints(DomainSet::whole(), {root});
    }
    // x ∈ (C - b) / a
    double lo = (cond.lo - aff.b) / aff.a;
    double hi = (cond.hi - aff.b) / aff.a;
    bool loC = cond.loClosed, hiC = cond.hiClosed;
    Cond mapped;
    mapped.kind = Cond::Kind::Interval;
    if (aff.a > 0.0) {
        mapped.lo = lo;
        mapped.hi = hi;
        mapped.loClosed = loC;
        mapped.hiClosed = hiC;
    } else {
        mapped.lo = hi;
        mapped.hi = lo;
        mapped.loClosed = hiC;
        mapped.hiClosed = loC;
    }
    return domainFromCond(mapped);
}

// ------------------------------------------------------------------
// 条件求解：|h| ∈ C
// ------------------------------------------------------------------
DomainSet solveAbsCond(const ExprP& child, const Cond& cond, const std::string& var) {
    if (cond.kind == Cond::Kind::Nonzero) return solveCond(child, condNonzero(), var);
    Cond c = cond;
    if (c.hi < 0.0) return DomainSet{};
    // 与 [0, +∞) 求交
    if (c.lo < 0.0) {
        c.lo = 0.0;
        c.loClosed = true;
    } else if (c.lo == 0.0) {
        // 保持开闭语义
    }
    if (condIsEmpty(c)) return DomainSet{};
    double A = c.lo;
    double B = c.hi;  // 可能为 +∞
    bool aC = c.loClosed;
    bool bC = c.hiClosed;
    if (A == 0.0 && aC) {
        // |h| <= B  ⇔  h ∈ [-B, B]
        return solveCond(child, condInterval(-B, bC, B, bC), var);
    }
    // A <= |h| <= B  ⇔  h ∈ [-B,-A] ∪ [A,B]
    DomainSet l = solveCond(child, condInterval(-B, bC, -A, aC), var);
    DomainSet r = solveCond(child, condInterval(A, aC, B, bC), var);
    return unionDS(l, r);
}

// ------------------------------------------------------------------
// 条件求解：h^k ∈ C（k 为正整数）
// ------------------------------------------------------------------
double signedRoot(double y, long long k) {
    if (std::isinf(y)) return y;
    double r = std::pow(std::fabs(y), 1.0 / static_cast<double>(k));
    return y < 0.0 ? -r : r;
}

DomainSet solveIntPowPositive(const ExprP& base, long long k, const Cond& cond,
                              const std::string& var) {
    if (k == 0) {
        if (cond.kind == Cond::Kind::Nonzero) return DomainSet::whole();  // 值恒为 1
        return cond.test(1.0) ? domainOfRec(base, var) : DomainSet{};
    }
    if (cond.kind == Cond::Kind::Nonzero) {
        // h^k = 0 ⇔ h = 0
        return solveCond(base, condNonzero(), var);
    }
    bool even = (k % 2 == 0);
    // 偶次幂值域非负；奇次幂无此限制
    if (even && cond.hi < 0.0) return DomainSet{};
    // 正负性快路径
    if (cond.lo == 0.0 && cond.hi == kInf) {
        if (cond.loClosed) {
            if (even) return domainOfRec(base, var);
            return solveCond(base, condNonnegative(), var);
        }
        if (even) return solveCond(base, condNonzero(), var);
        return solveCond(base, condPositive(), var);
    }
    // 一般区间
    if (even) {
        Cond c = cond;
        if (c.lo < 0.0) {
            c.lo = 0.0;
            c.loClosed = true;
        }
        if (condIsEmpty(c)) return DomainSet{};
        double A = c.lo;
        double B = c.hi;
        bool aC = c.loClosed;
        bool bC = c.hiClosed;
        if (A == 0.0 && aC) {
            // |h|^k <= B  ⇔  |h| <= B^(1/k)
            double root = std::isinf(B) ? kInf : std::pow(B, 1.0 / static_cast<double>(k));
            return solveCond(base, condInterval(-root, bC, root, bC), var);
        }
        double rootA = std::pow(A, 1.0 / static_cast<double>(k));
        double rootB = std::isinf(B) ? kInf : std::pow(B, 1.0 / static_cast<double>(k));
        DomainSet left = solveCond(base, condInterval(-rootB, bC, -rootA, aC), var);
        DomainSet right = solveCond(base, condInterval(rootA, aC, rootB, bC), var);
        return unionDS(left, right);
    }
    // 奇次幂：单调
    Cond mapped;
    mapped.kind = Cond::Kind::Interval;
    mapped.lo = signedRoot(cond.lo, k);
    mapped.hi = signedRoot(cond.hi, k);
    mapped.loClosed = cond.loClosed;
    mapped.hiClosed = cond.hiClosed;
    return solveCond(base, mapped, var);
}

// ------------------------------------------------------------------
// 条件求解：单调函数
// ------------------------------------------------------------------
DomainSet solveMonotoneCond(const ExprP& child, const Cond& cond, const MonotoneMap& m,
                            const std::string& var) {
    if (cond.kind != Cond::Kind::Interval) return DomainSet{};
    Cond c = cond;
    // 与函数值域求交
    if (c.lo < m.rangeLo) {
        c.lo = m.rangeLo;
        c.loClosed = m.rangeLoClosed;
    } else if (c.lo == m.rangeLo) {
        c.loClosed = c.loClosed && m.rangeLoClosed;
    }
    if (c.hi > m.rangeHi) {
        c.hi = m.rangeHi;
        c.hiClosed = m.rangeHiClosed;
    } else if (c.hi == m.rangeHi) {
        c.hiClosed = c.hiClosed && m.rangeHiClosed;
    }
    if (condIsEmpty(c)) return DomainSet{};

    Cond mapped;
    mapped.kind = Cond::Kind::Interval;
    if (m.increasing) {
        if (c.lo == m.rangeLo) {
            mapped.lo = m.domainLo;
            mapped.loClosed = m.domainLoClosed && c.loClosed && std::isfinite(m.rangeLo);
        } else {
            mapped.lo = m.inv(c.lo);
            mapped.loClosed = c.loClosed;
        }
        if (c.hi == m.rangeHi) {
            mapped.hi = m.domainHi;
            mapped.hiClosed = m.domainHiClosed && c.hiClosed && std::isfinite(m.rangeHi);
        } else {
            mapped.hi = m.inv(c.hi);
            mapped.hiClosed = c.hiClosed;
        }
    } else {
        if (c.hi == m.rangeHi) {
            mapped.lo = m.domainLo;
            mapped.loClosed = m.domainLoClosed && c.hiClosed && std::isfinite(m.rangeHi);
        } else {
            mapped.lo = m.inv(c.hi);
            mapped.loClosed = c.hiClosed;
        }
        if (c.lo == m.rangeLo) {
            mapped.hi = m.domainHi;
            mapped.hiClosed = m.domainHiClosed && c.loClosed && std::isfinite(m.rangeLo);
        } else {
            mapped.hi = m.inv(c.lo);
            mapped.hiClosed = c.loClosed;
        }
    }
    if (condIsEmpty(mapped)) return DomainSet{};
    return solveCond(child, mapped, var);
}

// ------------------------------------------------------------------
// 条件求解：数值兜底
// ------------------------------------------------------------------
DomainSet solveCondNumeric(const ExprP& e, const Cond& cond, const std::string& var) {
    g_usedNumeric = true;
    std::function<bool(double)> pred = [&](double x) -> bool {
        double v;
        if (!evalAt(e, var, x, &v)) return false;
        return cond.test(v);
    };
    return domainFromPredicate(pred);
}

// ------------------------------------------------------------------
// 条件求解总入口
// ------------------------------------------------------------------
DomainSet solveCond(const ExprP& e, const Cond& cond, const std::string& var) {
    if (!e) return DomainSet::whole();
    if (cond.kind == Cond::Kind::Interval && condIsEmpty(cond)) return DomainSet{};
    if (hasOtherVar(e, var)) return DomainSet::whole();  // 含参数变量，无法符号判定，保守放行
    if (isConst(e)) {
        double v;
        if (!constValue(e, &v)) return DomainSet{};  // 常量本身无实数值
        return cond.test(v) ? DomainSet::whole() : DomainSet{};
    }
    // 变量 / 一次式
    if (e->op == Op::Var) {
        if (e->var == var) return domainFromCond(cond);
        return DomainSet::whole();
    }
    Affine aff = asAffine(e, var);
    if (aff.ok) return solveAffineCond(aff, cond);

    // |h|
    if (e->op == Op::Abs) return solveAbsCond(e->a, cond, var);
    // -h
    if (e->op == Op::Neg) return solveCond(e->a, mirrorCond(cond), var);
    // 整数次幂
    if (e->op == Op::Pow && isNum(e->b)) {
        double kd = e->b->num;
        if (std::isfinite(kd) && std::fabs(kd - std::round(kd)) < 1e-9 &&
            std::fabs(kd) <= static_cast<double>(1LL << 40)) {
            long long k = static_cast<long long>(std::llround(kd));
            if (k > 0) return solveIntPowPositive(e->a, k, cond, var);
            if (k == 0) {
                if (cond.kind == Cond::Kind::Nonzero) return domainOfRec(e->a, var);
                return cond.test(1.0) ? domainOfRec(e->a, var) : DomainSet{};
            }
            // k < 0
            if (cond.kind == Cond::Kind::Nonzero) return solveCond(e->a, condNonzero(), var);
            bool evenM = ((-k) % 2 == 0);
            if (cond.hi == kInf) {
                if (cond.lo > 0.0 || (cond.lo == 0.0 && cond.loClosed)) {
                    return evenM ? solveCond(e->a, condNonzero(), var)
                                 : solveCond(e->a, condPositive(), var);
                }
                if (cond.lo == 0.0 && !cond.loClosed) {
                    return evenM ? solveCond(e->a, condNonzero(), var)
                                 : solveCond(e->a, condPositive(), var);
                }
            }
            if (cond.hi == 0.0 && cond.hiClosed && cond.lo < 0.0) {
                if (evenM) return DomainSet{};
                return solveCond(e->a, condNegative(), var);
            }
            // 其余交给数值兜底
        }
    }
    // 单调函数
    MonotoneMap m;
    if (monotoneMapFor(e->op, &m)) {
        if (cond.kind != Cond::Kind::Nonzero) return solveMonotoneCond(e->a, cond, m, var);
    }

    // 数值兜底
    if (cond.kind == Cond::Kind::Nonzero) {
        g_usedNumeric = true;
        DomainSet base = domainOfRec(e, var);
        std::vector<double> zeros = zerosIn(e, makeInterval(false, kWindowLo, true, false, kWindowHi, true), var);
        return excludePoints(base, zeros);
    }
    return solveCondNumeric(e, cond, var);
}

// ------------------------------------------------------------------
// tan 的极点
// ------------------------------------------------------------------
std::vector<double> tanPoles(const ExprP& arg, const std::string& var) {
    std::vector<double> poles;
    Affine aff = asAffine(arg, var);
    if (aff.ok) {
        if (aff.a == 0.0) return poles;
        double first = (kPi / 2.0 - aff.b) / aff.a;
        double period = kPi / aff.a;
        if (!std::isfinite(first) || !std::isfinite(period) || std::fabs(period) < 1e-15) return poles;
        double klo = (-125.0 - first) / period;
        double khi = (125.0 - first) / period;
        int ka = static_cast<int>(std::ceil(std::min(klo, khi))) - 1;
        int kb = static_cast<int>(std::floor(std::max(klo, khi))) + 1;
        ka = std::max(ka, -1000);
        kb = std::min(kb, 1000);
        for (int k = ka; k <= kb; ++k) {
            double p = first + k * period;
            if (std::isfinite(p) && std::fabs(p) <= 125.0) poles.push_back(snapNice(p));
        }
        return poles;
    }
    // 复合参数：数值找 cos(arg) 的变号点
    g_usedNumeric = true;
    ExprP w = mkFunc1(Op::Cos, arg);
    return zerosIn(w, makeInterval(false, -125.0, true, false, 125.0, true), var);
}

DomainSet solveTanDomain(const ExprP& arg, const std::string& var) {
    if (!arg) return DomainSet{};
    DomainSet base = domainOfRec(arg, var);
    if (base.empty()) return base;
    if (isConst(arg)) {
        double v;
        if (!constValue(arg, &v)) return DomainSet{};
        if (std::fabs(std::cos(v)) < 1e-9) return DomainSet{};
        return base;
    }
    if (hasOtherVar(arg, var)) return base;
    std::vector<double> poles = tanPoles(arg, var);
    return excludePoints(base, poles);
}

// ------------------------------------------------------------------
// 定义域递归推导
// ------------------------------------------------------------------
DomainSet domainOfRec(const ExprP& e, const std::string& var) {
    if (!e) return DomainSet::whole();
    switch (e->op) {
        case Op::Num:
        case Op::Pi:
        case Op::E:
        case Op::I:
            return DomainSet::whole();
        case Op::Var:
            return DomainSet::whole();
        case Op::Neg:
            return domainOfRec(e->a, var);
        case Op::Add:
        case Op::Sub:
        case Op::Mul:
        case Op::Min:
        case Op::Max:
            return intersectDS(domainOfRec(e->a, var), domainOfRec(e->b, var));
        case Op::Div: {
            DomainSet d = intersectDS(domainOfRec(e->a, var), domainOfRec(e->b, var));
            return intersectDS(d, solveCond(e->b, condNonzero(), var));
        }
        case Op::Mod: {
            DomainSet d = intersectDS(domainOfRec(e->a, var), domainOfRec(e->b, var));
            return intersectDS(d, solveCond(e->b, condNonzero(), var));
        }
        case Op::Pow: {
            DomainSet d = intersectDS(domainOfRec(e->a, var), domainOfRec(e->b, var));
            if (d.empty()) return d;
            Cond baseCond;
            bool needCond = false;
            if (isConst(e->b)) {
                double ev = 0.0;
                if (!constValue(e->b, &ev)) return DomainSet{};  // 指数无实数值
                if (std::fabs(ev - std::round(ev)) < 1e-9) {
                    if (ev < 0.0) {
                        needCond = true;
                        baseCond = condNonzero();
                    }
                } else if (ev > 0.0) {
                    needCond = true;
                    baseCond = condNonnegative();
                } else {
                    needCond = true;
                    baseCond = condPositive();
                }
            } else {
                // 指数含变量：实数意义下底数须 > 0
                needCond = true;
                baseCond = condPositive();
            }
            if (needCond) d = intersectDS(d, solveCond(e->a, baseCond, var));
            return d;
        }
        case Op::Ln:
        case Op::Log2:
        case Op::Log10: {
            DomainSet d = domainOfRec(e->a, var);
            return intersectDS(d, solveCond(e->a, condPositive(), var));
        }
        case Op::Sqrt: {
            DomainSet d = domainOfRec(e->a, var);
            return intersectDS(d, solveCond(e->a, condNonnegative(), var));
        }
        case Op::Cbrt:
        case Op::Exp:
        case Op::Atan:
        case Op::Sinh:
        case Op::Cosh:
        case Op::Tanh:
        case Op::Asinh:
        case Op::Abs:
        case Op::Floor:
        case Op::Ceil:
        case Op::Sign:
            return domainOfRec(e->a, var);
        case Op::Tan: {
            DomainSet d = domainOfRec(e->a, var);
            return intersectDS(d, solveTanDomain(e->a, var));
        }
        case Op::Asin:
        case Op::Acos: {
            DomainSet d = domainOfRec(e->a, var);
            return intersectDS(d, solveCond(e->a, condBetweenClosed(-1.0, 1.0), var));
        }
        case Op::Acosh: {
            DomainSet d = domainOfRec(e->a, var);
            return intersectDS(d, solveCond(e->a, condAtLeast(1.0), var));
        }
        case Op::Atanh: {
            DomainSet d = domainOfRec(e->a, var);
            return intersectDS(d, solveCond(e->a, condBetweenOpen(-1.0, 1.0), var));
        }
        default:
            return DomainSet::whole();
    }
}

// 数值复核：把符号推导遗漏的有定义区间补回来（只在触发过数值兜底时执行）
DomainSet numericFillGaps(const ExprP& e, const std::string& var, const DomainSet& symbolic) {
    const int n = kValidatePoints;
    const double step = (kWindowHi - kWindowLo) / n;
    std::vector<double> extra;
    for (int i = 0; i <= n; ++i) {
        double x = kWindowLo + step * i;
        if (symbolic.contains(x)) continue;
        double v;
        if (evalAt(e, var, x, &v)) extra.push_back(x);
    }
    if (extra.empty()) return symbolic;
    std::function<bool(double)> pred = [&](double x) -> bool {
        double v;
        return evalAt(e, var, x, &v);
    };
    DomainSet result = symbolic;
    size_t i = 0;
    while (i < extra.size()) {
        size_t j = i;
        while (j + 1 < extra.size() && extra[j + 1] - extra[j] <= step * 1.5) ++j;
        size_t count = j - i + 1;
        if (count >= 3) {
            double lo = snapNice(refineTransition(pred, extra[i] - step, extra[i]));
            double hi = snapNice(refineTransition(pred, extra[j] + step, extra[j]));
            Interval iv = makeInterval(false, lo, pred(lo), false, hi, pred(hi));
            if (!iv.isEmpty()) result.parts.push_back(iv);
        }
        i = j + 1;
    }
    result.normalize();
    return result;
}

}  // namespace

// ==================================================================
// Interval / DomainSet
// ==================================================================
Interval Interval::all() {
    Interval i;
    i.lo = -kHuge;
    i.hi = kHuge;
    i.loInf = true;
    i.hiInf = true;
    i.loClosed = true;
    i.hiClosed = true;
    return i;
}

Interval Interval::closed(double a, double b) {
    Interval i;
    i.lo = a;
    i.hi = b;
    i.loInf = false;
    i.hiInf = false;
    i.loClosed = true;
    i.hiClosed = true;
    return i;
}

Interval Interval::open(double a, double b) {
    Interval i;
    i.lo = a;
    i.hi = b;
    i.loInf = false;
    i.hiInf = false;
    i.loClosed = false;
    i.hiClosed = false;
    return i;
}

bool Interval::isEmpty() const {
    if (loInf || hiInf) return false;
    if (lo > hi) return true;
    if (lo == hi && !(loClosed && hiClosed)) return true;
    return false;
}

bool Interval::contains(double x) const {
    if (std::isnan(x)) return false;
    if (!std::isfinite(x)) return false;  // ±∞ 不是实数
    if (!loInf) {
        if (loClosed) {
            if (x < lo && !nearValue(x, lo)) return false;
        } else {
            if (x <= lo) return false;
        }
    }
    if (!hiInf) {
        if (hiClosed) {
            if (x > hi && !nearValue(x, hi)) return false;
        } else {
            if (x >= hi) return false;
        }
    }
    return true;
}

double Interval::length() const {
    if (loInf || hiInf) return 1e308;
    if (isEmpty()) return 0.0;
    return hi - lo;
}

std::string Interval::str() const {
    if (isEmpty()) return "∅";
    std::string left = loInf ? "(-∞" : (std::string(loClosed ? "[" : "(") + fmtNum(lo));
    std::string right = hiInf ? "+∞)" : (fmtNum(hi) + std::string(hiClosed ? "]" : ")"));
    return left + ", " + right;
}

bool operator==(const Interval& a, const Interval& b) {
    bool ae = a.isEmpty(), be = b.isEmpty();
    if (ae && be) return true;
    if (ae != be) return false;
    if (a.loInf != b.loInf || a.hiInf != b.hiInf) return false;
    if (!a.loInf && !nearValue(a.lo, b.lo)) return false;
    if (!a.hiInf && !nearValue(a.hi, b.hi)) return false;
    if (!a.loInf && a.loClosed != b.loClosed) return false;
    if (!a.hiInf && a.hiClosed != b.hiClosed) return false;
    return true;
}

bool DomainSet::contains(double x) const {
    for (const auto& p : parts) {
        if (p.contains(x)) return true;
    }
    return false;
}

bool DomainSet::containsAll(const Interval& iv) const {
    if (iv.isEmpty()) return true;
    if (parts.empty()) return false;
    std::vector<Interval> remain{iv};
    for (const auto& p : parts) {
        if (p.isEmpty()) continue;
        std::vector<Interval> next;
        for (const auto& r : remain) {
            auto pieces = subtractInterval(r, p);
            next.insert(next.end(), pieces.begin(), pieces.end());
        }
        remain = std::move(next);
        if (remain.empty()) return true;
    }
    return remain.empty();
}

std::string DomainSet::str() const {
    if (parts.empty()) return "∅";
    std::string s;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) s += " ∪ ";
        s += parts[i].str();
    }
    return s;
}

void DomainSet::normalize(double gapMerge) {
    std::vector<Interval> keep;
    keep.reserve(parts.size());
    for (const auto& iv : parts) {
        if (!iv.isEmpty()) keep.push_back(iv);
    }
    std::sort(keep.begin(), keep.end(), [](const Interval& a, const Interval& b) {
        int c = cmpLo(a, b);
        if (c != 0) return c < 0;
        return cmpHi(a, b) < 0;
    });
    std::vector<Interval> out;
    out.reserve(keep.size());
    for (const auto& iv : keep) {
        if (out.empty()) {
            out.push_back(iv);
            continue;
        }
        Interval& back = out.back();
        bool canMerge = false;
        if (back.hiInf || iv.loInf) {
            canMerge = true;
        } else {
            double gap = iv.lo - back.hi;
            if (gap < 0.0) {
                canMerge = true;  // 真正重叠
            } else if (gap == 0.0) {
                // 仅在公共端点被至少一侧包含时才可合并，
                // 否则 (-∞,0) ∪ (0,∞) 会把被排除的 0 补回来
                canMerge = back.hiClosed || iv.loClosed;
            } else {
                double scale = std::max({1.0, std::fabs(back.hi), std::fabs(iv.lo)});
                if (gap <= std::fabs(gapMerge) * scale) canMerge = true;
            }
        }
        if (canMerge)
            out.back() = mergeTwo(back, iv);
        else
            out.push_back(iv);
    }
    parts = std::move(out);
}

DomainSet DomainSet::whole() {
    DomainSet d;
    d.parts.push_back(Interval::all());
    return d;
}

// ==================================================================
// 定义域
// ==================================================================
DomainSet domainOf(const ExprP& e, const std::string& var) {
    if (!e) return DomainSet::whole();
    bool prev = g_usedNumeric;
    g_usedNumeric = false;
    DomainSet d = domainOfRec(e, var);
    bool usedNumeric = g_usedNumeric;
    g_usedNumeric = prev;
    d.normalize();
    if (usedNumeric && !hasOtherVar(e, var)) {
        d = numericFillGaps(e, var, d);
    }
    d.normalize();
    return d;
}

// ==================================================================
// 解析性
// ==================================================================
bool isBuiltFromAnalyticOps(const ExprP& e, Op* firstBadOp) {
    if (!e) return true;
    if (!opIsAnalytic(e->op)) {
        if (firstBadOp) *firstBadOp = e->op;
        return false;
    }
    if (!isBuiltFromAnalyticOps(e->a, firstBadOp)) return false;
    if (!isBuiltFromAnalyticOps(e->b, firstBadOp)) return false;
    return true;
}

namespace {

void collectResiduals(const ExprP& e, std::vector<ExprP>& out) {
    if (!e) return;
    switch (e->op) {
        case Op::Div:
        case Op::Mod:
            if (e->b) out.push_back(e->b);
            break;
        case Op::Tan:
            if (e->a) out.push_back(mkFunc1(Op::Cos, e->a));
            break;
        case Op::Sqrt:
        case Op::Cbrt:
        case Op::Ln:
        case Op::Log2:
        case Op::Log10:
        case Op::Abs:
        case Op::Sign:
            if (e->a) out.push_back(e->a);
            break;
        case Op::Asin:
        case Op::Acos:
        case Op::Atanh:
            if (e->a) {
                out.push_back(mkSub(e->a, mkNum(1.0)));
                out.push_back(mkAdd(e->a, mkNum(1.0)));
            }
            break;
        case Op::Acosh:
            if (e->a) out.push_back(mkSub(e->a, mkNum(1.0)));
            break;
        case Op::Min:
        case Op::Max:
            if (e->a && e->b) out.push_back(mkSub(e->a, e->b));
            break;
        case Op::Pow: {
            bool restrict = true;
            if (isNum(e->b)) {
                double k = e->b->num;
                if (std::isfinite(k)) {
                    if (std::fabs(k) < 1e-9)
                        restrict = false;
                    else if (std::fabs(k - std::round(k)) < 1e-9 && k > 0)
                        restrict = false;
                }
            }
            if (restrict && e->a) out.push_back(e->a);
            break;
        }
        default:
            break;
    }
    collectResiduals(e->a, out);
    collectResiduals(e->b, out);
}

// 数值求根：二分 + 切触（局部极小）检测
bool refineSignChange(const ExprP& e, const std::string& var, double x0, double x1, double zeroTol,
                      double* out) {
    double v0 = 0.0;
    if (!evalAt(e, var, x0, &v0)) return false;
    for (int i = 0; i < 120; ++i) {
        double mid = x0 + (x1 - x0) * 0.5;
        if (mid == x0 || mid == x1) break;
        double vm = 0.0;
        if (!evalAt(e, var, mid, &vm)) return false;
        if ((vm > 0.0) == (v0 > 0.0)) {
            x0 = mid;
            v0 = vm;
        } else {
            x1 = mid;
        }
    }
    double xm = x0 + (x1 - x0) * 0.5;
    double vm = 0.0;
    if (!evalAt(e, var, xm, &vm)) return false;
    if (std::fabs(vm) > zeroTol) return false;  // 很可能是极点/间断点而非零点
    *out = xm;
    return true;
}

bool refineTouchZero(const ExprP& e, const std::string& var, double a, double b, double zeroTol,
                     double* out) {
    auto fabsAt = [&](double x, double* v) -> bool {
        double y;
        if (!evalAt(e, var, x, &y)) return false;
        *v = std::fabs(y);
        return true;
    };
    double va, vb;
    if (!fabsAt(a, &va) || !fabsAt(b, &vb)) return false;
    for (int i = 0; i < 120; ++i) {
        double m1 = a + (b - a) / 3.0;
        double m2 = b - (b - a) / 3.0;
        if (m1 == a || m2 == b) break;
        double v1, v2;
        if (!fabsAt(m1, &v1) || !fabsAt(m2, &v2)) break;
        if (v1 < v2)
            b = m2;
        else
            a = m1;
    }
    double xm = a + (b - a) * 0.5;
    double vm;
    if (!fabsAt(xm, &vm)) return false;
    if (vm > zeroTol) return false;
    *out = xm;
    return true;
}

}  // namespace

std::vector<double> singularitiesIn(const ExprP& e, const Interval& iv, const std::string& var) {
    std::vector<double> pts;
    if (!e) return pts;
    if (iv.isEmpty()) return pts;
    if (hasOtherVar(e, var)) return pts;

    std::vector<ExprP> residuals;
    collectResiduals(e, residuals);
    for (const auto& r : residuals) {
        if (!r) continue;
        auto zeros = zerosIn(r, iv, var);
        for (double z : zeros) {
            if (iv.contains(z)) pts.push_back(z);
        }
    }
    // 定义域边界（sqrt/ln 等的端点）也属于不可导点
    DomainSet dom = domainOf(e, var);
    for (const auto& part : dom.parts) {
        if (!part.loInf && std::isfinite(part.lo) && std::fabs(part.lo) < 1e300 && iv.contains(part.lo))
            pts.push_back(part.lo);
        if (!part.hiInf && std::isfinite(part.hi) && std::fabs(part.hi) < 1e300 && iv.contains(part.hi))
            pts.push_back(part.hi);
    }
    std::sort(pts.begin(), pts.end());
    std::vector<double> out;
    for (double p : pts) {
        double s = snapNice(p);
        if (out.empty() || std::fabs(s - out.back()) > 1e-7 * std::max(1.0, std::fabs(s)))
            out.push_back(s);
    }
    return out;
}

bool isAnalyticOn(const ExprP& e, const Interval& iv, const std::string& var, std::string* reason) {
    auto fail = [reason](const std::string& s) {
        if (reason) *reason = s;
        return false;
    };
    if (!e) return fail("表达式为空");
    if (iv.isEmpty()) return fail("区间为空");
    Op bad = Op::Num;
    if (!isBuiltFromAnalyticOps(e, &bad)) {
        return fail(std::string("包含非解析函数：") + opName(bad));
    }
    DomainSet dom = domainOf(e, var);
    if (!dom.containsAll(iv)) {
        return fail("区间内存在无定义点，定义域为 " + dom.str());
    }
    auto sing = singularitiesIn(e, iv, var);
    if (!sing.empty()) {
        std::string msg = "存在奇点/不可导点：";
        for (size_t i = 0; i < sing.size() && i < 5; ++i) {
            if (i) msg += "、";
            msg += "x=" + fmtNum(sing[i]);
        }
        return fail(msg);
    }
    if (reason) reason->clear();
    return true;
}

// ==================================================================
// 值域
// ==================================================================
RangeResult rangeOn(const ExprP& e, const Interval& iv, const std::string& var) {
    RangeResult r;
    if (!e) {
        r.error = "表达式为空";
        return r;
    }
    if (iv.isEmpty()) {
        r.error = "区间为空";
        return r;
    }
    if (hasOtherVar(e, var)) {
        r.error = "表达式包含其他变量，无法数值分析";
        return r;
    }
    double a = iv.loInf ? kWindowLo : iv.lo;
    double b = iv.hiInf ? kWindowHi : iv.hi;
    if (!(b > a)) {
        double v;
        if (evalAt(e, var, a, &v)) {
            r.ok = true;
            r.lo = v;
            r.hi = v;
        } else {
            r.error = "该点无定义";
        }
        return r;
    }

    const int n = 2048;
    std::vector<double> xs(static_cast<size_t>(n) + 1);
    std::vector<double> vs(static_cast<size_t>(n) + 1, 0.0);
    std::vector<unsigned char> ok(static_cast<size_t>(n) + 1, 0);
    double lo = kInf, hi = -kInf;
    bool any = false;
    for (int i = 0; i <= n; ++i) {
        double x = a + (b - a) * (static_cast<double>(i) / n);
        xs[static_cast<size_t>(i)] = x;
        double v;
        if (evalAt(e, var, x, &v)) {
            ok[static_cast<size_t>(i)] = 1;
            vs[static_cast<size_t>(i)] = v;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            any = true;
        }
    }
    if (!any) {
        r.error = "在区间上没有有定义的取值点";
        return r;
    }

    // 局部极值细化（三分法）
    auto ternaryRefine = [&](double xa, double xb, bool wantMin, double* outX, double* outV) {
        auto valueAt = [&](double x, double* v) -> bool { return evalAt(e, var, x, v); };
        double va = 0.0, vb = 0.0;
        if (!valueAt(xa, &va) || !valueAt(xb, &vb)) return false;
        for (int it = 0; it < 100; ++it) {
            double m1 = xa + (xb - xa) / 3.0;
            double m2 = xb - (xb - xa) / 3.0;
            if (m1 == xa || m2 == xb) break;
            double v1 = 0.0, v2 = 0.0;
            if (!valueAt(m1, &v1) || !valueAt(m2, &v2)) break;
            if (wantMin) {
                if (v1 < v2)
                    xb = m2;
                else
                    xa = m1;
            } else {
                if (v1 > v2)
                    xb = m2;
                else
                    xa = m1;
            }
        }
        double xm = xa + (xb - xa) * 0.5;
        double vm = 0.0;
        if (!valueAt(xm, &vm)) return false;
        *outX = xm;
        *outV = vm;
        return true;
    };

    for (int i = 1; i < n; ++i) {
        size_t k = static_cast<size_t>(i);
        if (!ok[k - 1] || !ok[k] || !ok[k + 1]) continue;
        if (vs[k] <= vs[k - 1] && vs[k] <= vs[k + 1]) {
            double xm, vm;
            if (ternaryRefine(xs[k - 1], xs[k + 1], true, &xm, &vm)) lo = std::min(lo, vm);
        }
        if (vs[k] >= vs[k - 1] && vs[k] >= vs[k + 1]) {
            double xm, vm;
            if (ternaryRefine(xs[k - 1], xs[k + 1], false, &xm, &vm)) hi = std::max(hi, vm);
        }
    }

    auto applyLimit = [&](const LimitResult& lr) {
        if (lr.kind == LimitResult::Kind::Finite) {
            lo = std::min(lo, lr.value);
            hi = std::max(hi, lr.value);
        } else if (lr.kind == LimitResult::Kind::PosInf) {
            hi = kInf;
        } else if (lr.kind == LimitResult::Kind::NegInf) {
            lo = -kInf;
        }
    };

    // 端点极限（开区间端点取极限值）
    if (!iv.loInf)
        applyLimit(limitAt(e, var, iv.lo, Direction::Right));
    else
        applyLimit(limitAtInfinity(e, var, -1));
    if (!iv.hiInf)
        applyLimit(limitAt(e, var, iv.hi, Direction::Left));
    else
        applyLimit(limitAtInfinity(e, var, 1));

    // 内部奇点：可能出现无穷极限
    auto sing = singularitiesIn(e, iv, var);
    for (double p : sing) {
        if (!(p > a && p < b)) continue;
        applyLimit(limitAt(e, var, p, Direction::Left));
        applyLimit(limitAt(e, var, p, Direction::Right));
    }

    r.ok = true;
    r.lo = lo;
    r.hi = hi;
    return r;
}

// ==================================================================
// 零点
// ==================================================================
std::vector<double> zerosIn(const ExprP& e, const Interval& iv, const std::string& var) {
    std::vector<double> out;
    if (!e || iv.isEmpty()) return out;
    if (hasOtherVar(e, var)) return out;
    double a = iv.loInf ? kWindowLo : iv.lo;
    double b = iv.hiInf ? kWindowHi : iv.hi;
    if (!(b > a)) {
        double v;
        if (evalAt(e, var, a, &v) && std::fabs(v) < 1e-9) out.push_back(snapNice(a));
        return out;
    }
    const int n = 4096;
    std::vector<double> xs(static_cast<size_t>(n) + 1);
    std::vector<double> vs(static_cast<size_t>(n) + 1, 0.0);
    std::vector<unsigned char> ok(static_cast<size_t>(n) + 1, 0);
    double maxAbs = 0.0;
    for (int i = 0; i <= n; ++i) {
        double x = a + (b - a) * (static_cast<double>(i) / n);
        xs[static_cast<size_t>(i)] = x;
        double v;
        if (evalAt(e, var, x, &v)) {
            ok[static_cast<size_t>(i)] = 1;
            vs[static_cast<size_t>(i)] = v;
            maxAbs = std::max(maxAbs, std::fabs(v));
        }
    }
    double scale = std::max(1.0, maxAbs);
    double zeroTol = std::max(1e-12, maxAbs * 1e-14);
    double touchTol = std::max(1e-12, maxAbs * 1e-14);
    double poleTol = std::max(1e-12, maxAbs * 1e-6);

    std::vector<double> cand;
    // 1) 采样点直接命中
    for (int i = 0; i <= n; ++i) {
        size_t k = static_cast<size_t>(i);
        if (!ok[k]) continue;
        if (std::fabs(vs[k]) <= zeroTol) cand.push_back(xs[k]);
    }
    // 2) 变号 → 二分
    for (int i = 0; i < n; ++i) {
        size_t k = static_cast<size_t>(i);
        if (!ok[k] || !ok[k + 1]) continue;
        if ((vs[k] > 0.0) == (vs[k + 1] > 0.0)) continue;
        double z;
        if (refineSignChange(e, var, xs[k], xs[k + 1], poleTol, &z)) cand.push_back(z);
    }
    // 3) 切触零点（局部极小且接近 0）
    for (int i = 1; i < n; ++i) {
        size_t k = static_cast<size_t>(i);
        if (!ok[k - 1] || !ok[k] || !ok[k + 1]) continue;
        double av = std::fabs(vs[k]);
        if (av > 1e-3 * scale) continue;
        if (av <= std::fabs(vs[k - 1]) && av <= std::fabs(vs[k + 1])) {
            double z;
            if (refineTouchZero(e, var, xs[k - 1], xs[k + 1], touchTol, &z)) cand.push_back(z);
        }
    }
    std::sort(cand.begin(), cand.end());
    for (double c : cand) {
        double s = snapNice(c);
        if (out.empty() || std::fabs(s - out.back()) > 1e-6 * std::max(1.0, std::fabs(s)))
            out.push_back(s);
    }
    if (out.size() > 512) out.resize(512);
    return out;
}

// ==================================================================
// 单调性
// ==================================================================
namespace {

bool centralDiff(const ExprP& f, const std::string& var, double x, double h, double* out) {
    double vp, vm;
    if (!evalAt(f, var, x + h, &vp)) return false;
    if (!evalAt(f, var, x - h, &vm)) return false;
    double d = (vp - vm) / (2.0 * h);
    if (!std::isfinite(d)) return false;
    *out = d;
    return true;
}

bool symbolicDerivativeUsable(const ExprP& f, const ExprP& df, const std::vector<double>& xs,
                              const std::string& var) {
    if (!df) return false;
    int total = 0, match = 0;
    for (double x : xs) {
        double numeric;
        double h = 1e-5 * std::max(1.0, std::fabs(x));
        if (!centralDiff(f, var, x, h, &numeric)) continue;
        double dv;
        if (!evalAt(df, var, x, &dv)) return false;
        ++total;
        if (std::fabs(dv - numeric) <= 1e-4 * std::max(1.0, std::fabs(numeric))) ++match;
    }
    return total >= 3 && match == total;
}

}  // namespace

MonotonicResult monotonicOn(const ExprP& f, const Interval& iv, const std::string& var) {
    MonotonicResult r;
    if (!f) {
        r.error = "表达式为空";
        return r;
    }
    if (iv.isEmpty()) {
        r.error = "区间为空";
        return r;
    }
    if (hasOtherVar(f, var)) {
        r.error = "表达式包含其他变量，无法分析";
        return r;
    }
    double a, b;
    if (iv.loInf || iv.hiInf) {
        a = kWindowLo;
        b = kWindowHi;
    } else {
        a = iv.lo;
        b = iv.hi;
    }
    if (!(b > a)) {
        r.error = "区间退化";
        return r;
    }

    // 区间必须在定义域内
    DomainSet dom = domainOf(f, var);
    if (!dom.containsAll(iv)) {
        r.error = "区间内存在定义域缺口，不满足" + dom.str();
        return r;
    }

    const int n = 1024;
    std::vector<double> xs(static_cast<size_t>(n) + 1);
    std::vector<double> fv(static_cast<size_t>(n) + 1, 0.0);
    std::vector<unsigned char> okf(static_cast<size_t>(n) + 1, 0);
    bool anyValid = false;
    // 校验用的采样点
    std::vector<double> probe;
    for (int i = 0; i <= n; ++i) {
        double x = a + (b - a) * (static_cast<double>(i) / n);
        xs[static_cast<size_t>(i)] = x;
        double v;
        if (evalAt(f, var, x, &v)) {
            okf[static_cast<size_t>(i)] = 1;
            fv[static_cast<size_t>(i)] = v;
            anyValid = true;
            if (probe.size() < 8 && (i % (n / 8 + 1)) == 0) probe.push_back(x);
        }
    }
    if (!anyValid) {
        r.error = "在区间上没有有定义的取值点";
        return r;
    }

    // 尝试符号导数（若可用且数值校验通过）
    ExprP df;
    bool useSymbolic = false;
    {
        ExprP cand = diff(f, var);
        if (symbolicDerivativeUsable(f, cand, probe, var)) {
            df = cand;
            useSymbolic = true;
        }
    }

    double maxAbsD = 0.0;
    std::vector<double> ds;
    ds.reserve(static_cast<size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        size_t k = static_cast<size_t>(i);
        if (!okf[k]) {
            ds.push_back(0.0);
            continue;
        }
        double x = xs[k];
        double d = 0.0;
        bool have = false;
        if (useSymbolic) have = evalAt(df, var, x, &d);
        if (!have) {
            double h = 1e-6 * std::max(1.0, std::fabs(x));
            have = centralDiff(f, var, x, h, &d);
        }
        if (!have) d = 0.0;
        ds.push_back(d);
        maxAbsD = std::max(maxAbsD, std::fabs(d));
    }
    double tolD = std::max(1e-9, maxAbsD * 1e-7);
    int pos = 0, neg = 0;
    int firstPos = -1, firstNeg = -1;
    for (int i = 0; i <= n; ++i) {
        size_t k = static_cast<size_t>(i);
        if (!okf[k]) continue;
        if (ds[k] > tolD) {
            ++pos;
            if (firstPos < 0) firstPos = i;
        } else if (ds[k] < -tolD) {
            ++neg;
            if (firstNeg < 0) firstNeg = i;
        }
    }
    std::string whereText = "x≈" + fmtNum(xs[static_cast<size_t>(firstPos < 0 ? 0 : firstPos)]);
    if (pos > 0 && neg > 0) {
        int at = firstPos > firstNeg ? firstPos : firstNeg;
        r.error = "导数在 x≈" + fmtNum(xs[static_cast<size_t>(at)]) + " 附近变号，函数在区间上不单调";
        return r;
    }
    if (pos == 0 && neg == 0) {
        // 常数函数
        r.ok = true;
        r.increasing = true;
        r.strictly = false;
        return r;
    }
    r.increasing = (pos > 0);
    // 严格性：用函数值差分判定
    double fscale = std::max(1.0, std::max(std::fabs(fv[0]), std::fabs(fv[static_cast<size_t>(n)])));
    for (int i = 0; i <= n; ++i) {
        if (okf[static_cast<size_t>(i)])
            fscale = std::max(fscale, std::fabs(fv[static_cast<size_t>(i)]));
    }
    double tolF = fscale * 1e-9;
    bool strict = true;
    bool moved = false;
    for (int i = 0; i + 1 <= n; ++i) {
        size_t k = static_cast<size_t>(i);
        if (!okf[k] || !okf[k + 1]) {
            strict = false;
            break;
        }
        double d = fv[k + 1] - fv[k];
        if (r.increasing) {
            if (d < -tolF) {
                strict = false;
                break;
            }
            if (d > tolF) moved = true;
        } else {
            if (d > tolF) {
                strict = false;
                break;
            }
            if (d < -tolF) moved = true;
        }
    }
    if (!moved) strict = false;
    r.strictly = strict;
    r.ok = true;
    return r;
}

}  // namespace fcg
