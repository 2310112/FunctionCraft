// EndlessGen.cpp — 无尽模式关卡程序化生成
//
// 关键设计：先生成参考解（表达式模板池），再对参考解做数值分析
// （奇偶性 / 单调性 / 值域 / 零点 / 极值 / 渐近线 / 周期性），
// 由参考解反推出它必然满足的约束，最后用 checkConstraint 逐条过滤，
// 保证每一关的参考解都能通过本关全部约束（关关可解）。
#include "EndlessGen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "Constraint.h"
#include "Numeric.h"
#include "Parser.h"
#include "Scoring.h"

namespace fcg {
namespace {

constexpr double kPi = 3.14159265358979323846;

// -----------------------------------------------------------------
// 随机数（splitmix64，跨平台确定）
// -----------------------------------------------------------------
uint64_t splitmix64(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed) {}
    uint64_t next() { return splitmix64(state); }
    double uniform01() {
        return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
    }
    int intRange(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int>(uniform01() * static_cast<double>(hi - lo + 1));
    }
    bool chance(double p) { return uniform01() < p; }
    template <typename T>
    void shuffle(std::vector<T>& v) {
        for (size_t i = v.size(); i > 1; --i) {
            const size_t j = static_cast<size_t>(uniform01() * static_cast<double>(i));
            std::swap(v[i - 1], v[j]);
        }
    }
};

// -----------------------------------------------------------------
// 参考解模板池（模板必须全局有定义或至少在所取区间内有定义）
// -----------------------------------------------------------------
struct Template {
    const char* expr;
    const char* name;
    int minTier;
    bool allowZeros;
};

const std::vector<Template>& templates() {
    static const std::vector<Template> pool = {
        // tier 1：基础三角
        {"sin(x)", "正弦波", 1, true},
        {"cos(x)", "余弦波", 1, true},
        {"-cos(x)", "反相余弦", 1, true},
        {"cos(2x)", "倍频余弦", 1, true},
        {"sin(2x)", "倍频正弦", 1, true},
        {"sin(x) + cos(x)", "正余弦叠加", 1, true},
        // tier 2：指数 / 有理函数
        {"exp(-x*x)", "高斯钟形", 2, false},
        {"exp(-x*x) + 1", "抬升高斯", 2, false},
        {"1/(1 + x*x)", "有理钟形", 2, false},
        {"x/(1 + x*x)", "有理波动", 2, true},
        {"x*exp(-x*x)", "高斯脉冲", 2, true},
        {"2*cos(x)", "放大余弦", 1, true},
        // tier 3：双曲 / 多项式 / 逻辑函数
        {"tanh(x)", "双曲正切", 3, true},
        {"atan(x)", "反正切弧线", 3, true},
        {"x^3", "立方曲线", 3, true},
        {"x*x", "抛物线", 3, false},
        {"cos(x)^2", "平方余弦", 3, false},
        {"1/(1 + exp(-x))", "逻辑 S 曲线", 3, false},
        // tier 4：扩展函数库
        {"sinh(x)", "双曲正弦", 4, true},
        {"cosh(x)", "双曲余弦", 4, false},
        {"cbrt(x)", "立方根曲线", 4, true},
        {"asin(x)", "反正弦弧线", 4, true},
        {"x^4 - x^2", "双谷曲线", 4, false},
        {"x*cos(x)", "振荡增长", 4, true},
    };
    return pool;
}

int tierOfIndex(int index) {
    if (index <= 10) return 1;
    if (index <= 30) return 2;
    if (index <= 60) return 3;
    return 4;
}

int baseTargetConstraints(int index) {
    if (index <= 10) return 2;
    if (index <= 20) return 3;
    if (index <= 30) return 4;
    if (index <= 45) return 4;
    if (index <= 60) return 5;
    if (index <= 100) return 5;
    return 6;
}

std::vector<Op> opsForTier(int tier) {
    std::vector<Op> ops = {Op::Add, Op::Sub, Op::Neg, Op::Mul, Op::Div, Op::Pow};
    ops.push_back(Op::Sin);
    ops.push_back(Op::Cos);
    if (tier >= 2) ops.push_back(Op::Exp);
    if (tier >= 3) {
        ops.push_back(Op::Tanh);
        ops.push_back(Op::Atan);
    }
    if (tier >= 4) {
        ops.push_back(Op::Sinh);
        ops.push_back(Op::Cosh);
        ops.push_back(Op::Asinh);
        ops.push_back(Op::Cbrt);
        ops.push_back(Op::Asin);
        ops.push_back(Op::Acos);
    }
    return ops;
}

// -----------------------------------------------------------------
// 数值工具
// -----------------------------------------------------------------
bool evalFiniteAt(const ExprP& f, const std::string& var, double x, double* out) {
    if (!f) return false;
    const RealResult r = evalReal(f, var, x);
    if (!r.ok || !std::isfinite(r.value)) return false;
    if (out) *out = r.value;
    return true;
}

bool finiteBounds(const Interval& iv, double* a, double* b) {
    if (iv.loInf || iv.hiInf) return false;
    if (!(iv.hi > iv.lo)) return false;
    *a = iv.lo;
    *b = iv.hi;
    return true;
}

std::vector<Interval> intervalCandidates() {
    return {Interval::closed(-kPi, kPi), Interval::closed(0.0, 2.0 * kPi),
            Interval::closed(-2.0, 2.0), Interval::closed(0.0, 3.0),
            Interval::closed(-3.0, 3.0), Interval::closed(-1.0, 1.0),
            Interval::closed(0.0, 1.0),   Interval::closed(-1.5, 1.5)};
}

bool intervalUsable(const ExprP& f, const std::string& var, const Interval& iv) {
    double a = 0.0, b = 0.0;
    if (!finiteBounds(iv, &a, &b)) return false;
    const int n = 32;
    for (int i = 0; i <= n; ++i) {
        const double x = a + (b - a) * static_cast<double>(i) / n;
        double v = 0.0;
        if (!evalFiniteAt(f, var, x, &v)) return false;
        if (std::fabs(v) > 1e5) return false;
    }
    return true;
}

bool detectParity(const ExprP& f, const std::string& var, bool* even) {
    static const double xs[] = {0.13, 0.29, 0.51, 0.77, 1.07, 1.31, 1.73,
                                2.11, 2.53, 2.91, -0.23, -0.61, -1.19, -1.87, -2.47};
    int tested = 0;
    bool evenOk = true, oddOk = true;
    for (double x : xs) {
        double a = 0.0, b = 0.0;
        if (!evalFiniteAt(f, var, -x, &a) || !evalFiniteAt(f, var, x, &b)) continue;
        const double tol = 1e-7 * (1.0 + std::fabs(a) + std::fabs(b));
        if (std::fabs(a - b) > tol) evenOk = false;
        if (std::fabs(a + b) > tol) oddOk = false;
        ++tested;
    }
    if (tested < 6) return false;
    if (evenOk && oddOk) {
        *even = true;
        return true;
    }
    if (evenOk) {
        *even = true;
        return true;
    }
    if (oddOk) {
        *even = false;
        return true;
    }
    return false;
}

struct MonoDetect {
    bool ok = false;
    bool increasing = true;
    bool strict = false;
};

MonoDetect detectMonotonic(const ExprP& f, const std::string& var, const Interval& iv) {
    MonoDetect r;
    double a = 0.0, b = 0.0;
    if (!finiteBounds(iv, &a, &b)) return r;
    const int n = 128;
    std::vector<double> vs(static_cast<size_t>(n) + 1);
    double maxAbs = 0.0;
    for (int i = 0; i <= n; ++i) {
        const double x = a + (b - a) * static_cast<double>(i) / n;
        if (!evalFiniteAt(f, var, x, &vs[static_cast<size_t>(i)])) return r;
        maxAbs = std::max(maxAbs, std::fabs(vs[static_cast<size_t>(i)]));
    }
    const double tol = 1e-10 * (1.0 + maxAbs);
    int pos = 0, neg = 0;
    double minAbsDiff = 1e300;
    for (int i = 0; i < n; ++i) {
        const double d = vs[static_cast<size_t>(i) + 1] - vs[static_cast<size_t>(i)];
        if (d > tol) ++pos;
        else if (d < -tol) ++neg;
        minAbsDiff = std::min(minAbsDiff, std::fabs(d));
    }
    if (pos > 0 && neg > 0) return r;
    r.ok = true;
    r.increasing = (neg == 0 && pos > 0) ? true : (pos == 0 && neg > 0 ? false : true);
    r.strict = (minAbsDiff > 1e-9 * (1.0 + maxAbs));
    return r;
}

struct RangeDetect {
    bool ok = false;
    double lo = 0.0, hi = 0.0;
};

RangeDetect detectRange(const ExprP& f, const std::string& var, const Interval& iv) {
    RangeDetect r;
    double a = 0.0, b = 0.0;
    if (!finiteBounds(iv, &a, &b)) return r;
    const int n = 256;
    int cnt = 0;
    for (int i = 0; i <= n; ++i) {
        const double x = a + (b - a) * static_cast<double>(i) / n;
        double v = 0.0;
        if (!evalFiniteAt(f, var, x, &v)) return r;
        if (cnt == 0) {
            r.lo = r.hi = v;
        } else {
            r.lo = std::min(r.lo, v);
            r.hi = std::max(r.hi, v);
        }
        ++cnt;
    }
    r.ok = cnt > 8;
    return r;
}

bool pickBoundary(const ExprP& f, const std::string& var, const Interval& iv, Rng& rng,
                  double* point, double* value) {
    double a = 0.0, b = 0.0;
    if (!finiteBounds(iv, &a, &b)) return false;
    const double h = b - a;
    const std::vector<double> candidates = {0.0, a + 0.5 * h, a + 0.3 * h, b - 0.3 * h,
                                            kPi / 2.0, -kPi / 2.0};
    std::vector<std::pair<double, double>> good;
    for (double p : candidates) {
        if (p <= a + 1e-9 || p >= b - 1e-9) continue;
        double v = 0.0;
        if (!evalFiniteAt(f, var, p, &v)) continue;
        if (std::fabs(v) > 1e4) continue;
        good.emplace_back(p, v);
    }
    if (good.empty()) return false;
    const size_t idx = static_cast<size_t>(rng.intRange(0, static_cast<int>(good.size()) - 1));
    *point = good[idx].first;
    *value = good[idx].second;
    return true;
}

bool detectPeriod(const ExprP& f, const std::string& var, double* period) {
    static const double cands[] = {kPi / 2.0, 1.0, kPi, 2.0 * kPi, 4.0 * kPi};
    static const double xs[] = {-2.31, -1.43, -0.67, -0.11, 0.37, 0.93, 1.61,
                                2.07, 2.53, 2.99, -2.87};
    for (double T : cands) {
        bool allOk = true;
        int tested = 0;
        for (double x : xs) {
            double a = 0.0, b = 0.0;
            if (!evalFiniteAt(f, var, x + T, &a) || !evalFiniteAt(f, var, x, &b)) continue;
            const double tol = 1e-6 * (1.0 + std::fabs(a) + std::fabs(b));
            if (std::fabs(a - b) > tol) {
                allOk = false;
                break;
            }
            ++tested;
        }
        if (allOk && tested >= 8) {
            *period = T;
            return true;
        }
    }
    return false;
}

bool estimateLimitInfinity(const ExprP& f, const std::string& var, int sign, double* out) {
    static const double ms[] = {1e3, 1e4, 1e5, 1e6};
    double v[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        if (!evalFiniteAt(f, var, sign * ms[i], &v[i])) return false;
    }
    if (std::fabs(v[3] - v[2]) > 1e-3 * (1.0 + std::fabs(v[3]))) return false;
    if (std::fabs(v[2] - v[1]) > 1e-2 * (1.0 + std::fabs(v[2]))) return false;
    *out = v[3];
    return true;
}

double snapNiceValue(double v) {
    static const double nice[] = {0.0,          1.0,          -1.0,        0.5,
                                  -0.5,         2.0,          -2.0,        kPi / 2.0,
                                  -kPi / 2.0,   kPi / 4.0,    -kPi / 4.0,  kPi,
                                  -kPi,         0.25,         -0.25,       3.0,
                                  -3.0};
    for (double n : nice) {
        if (std::fabs(v - n) <= 1e-3 * (1.0 + std::fabs(n))) return n;
    }
    return v;
}

bool findExtremum(const ExprP& f, const std::string& var, const Interval& iv, Rng& rng,
                  double* point, bool* isMax) {
    double a = 0.0, b = 0.0;
    if (!finiteBounds(iv, &a, &b)) return false;
    std::vector<double> candidates = {0.0, kPi / 2.0, -kPi / 2.0, kPi, -kPi, a + 0.5 * (b - a),
                                      kPi / 4.0, -kPi / 4.0};
    rng.shuffle(candidates);
    for (double p : candidates) {
        if (p <= a + 0.1 || p >= b - 0.1) continue;
        const double h = 1e-2 * (1.0 + std::fabs(p));
        double f0 = 0.0;
        if (!evalFiniteAt(f, var, p, &f0)) continue;
        double nbr[4] = {0, 0, 0, 0};
        const double offs[4] = {-2 * h, -h, h, 2 * h};
        bool ok = true;
        for (int i = 0; i < 4 && ok; ++i) {
            ok = evalFiniteAt(f, var, p + offs[i], &nbr[i]);
        }
        if (!ok) continue;
        const double nearTol = 1e-9 * (1.0 + std::fabs(f0));
        bool allBelow = true, allAbove = true, strictBelow = false, strictAbove = false;
        for (double v : nbr) {
            if (v > f0 + nearTol) allBelow = false;
            if (v < f0 - nearTol) allAbove = false;
            if (v < f0 - nearTol) strictBelow = true;
            if (v > f0 + nearTol) strictAbove = true;
        }
        if (allBelow && strictBelow) {
            *point = p;
            *isMax = true;
            return true;
        }
        if (allAbove && strictAbove) {
            *point = p;
            *isMax = false;
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------
// 由参考解反推候选约束
// -----------------------------------------------------------------
void addZeroCountCandidate(const ExprP& f, const Interval& iv, std::vector<Constraint>* cands) {
    for (int n = 0; n <= 4; ++n) {
        Constraint c;
        c.kind = ConstraintKind::ZeroCount;
        c.interval = iv;
        c.count = n;
        if (checkConstraint(c, f).ok) {
            cands->push_back(c);
            return;
        }
    }
}

std::vector<Constraint> buildCandidates(const ExprP& f, const Template& tpl, int tier,
                                       const Interval& iv, Rng& rng) {
    std::vector<Constraint> rest;
    const std::string var = "x";

    // 函数库
    {
        Constraint c;
        c.kind = ConstraintKind::AllowedFunctions;
        c.allowedOps = opsForTier(tier);
        rest.push_back(c);
    }
    // 边界值
    {
        double p = 0.0, v = 0.0;
        if (pickBoundary(f, var, iv, rng, &p, &v)) {
            Constraint c;
            c.kind = ConstraintKind::BoundaryValue;
            c.point = p;
            c.value = v;
            rest.push_back(c);
        }
    }
    // 奇偶性
    bool even = true;
    if (detectParity(f, var, &even)) {
        Constraint c;
        c.kind = ConstraintKind::Parity;
        c.even = even;
        rest.push_back(c);
        if (even) {
            Constraint s;
            s.kind = ConstraintKind::SymmetryAxis;
            s.point = 0.0;
            rest.push_back(s);
        }
    }
    // 单调性
    {
        const MonoDetect m = detectMonotonic(f, var, iv);
        if (m.ok) {
            Constraint c;
            c.kind = ConstraintKind::Monotonic;
            c.interval = iv;
            c.increasing = m.increasing;
            c.strict = m.strict;
            rest.push_back(c);
        }
    }
    // 值域
    {
        const RangeDetect r = detectRange(f, var, iv);
        if (r.ok && std::isfinite(r.lo) && std::isfinite(r.hi)) {
            const double span = r.hi - r.lo;
            const double pad = 0.05 + 0.01 * std::max(0.0, span);
            Constraint c;
            c.kind = ConstraintKind::RangeOn;
            c.interval = iv;
            c.lo = std::floor((r.lo - pad) * 20.0) / 20.0;
            c.hi = std::ceil((r.hi + pad) * 20.0) / 20.0;
            if (c.lo < c.hi) rest.push_back(c);
        }
    }
    // 周期性
    {
        double period = 0.0;
        if (detectPeriod(f, var, &period)) {
            Constraint c;
            c.kind = ConstraintKind::Periodic;
            c.value = period;
            rest.push_back(c);
        }
    }
    // 极值
    {
        double p = 0.0;
        bool isMax = true;
        if (findExtremum(f, var, iv, rng, &p, &isMax)) {
            Constraint c;
            c.kind = ConstraintKind::Extremum;
            c.point = p;
            c.isMax = isMax;
            rest.push_back(c);
        }
    }
    // 零点个数
    if (tpl.allowZeros) addZeroCountCandidate(f, iv, &rest);
    // 水平渐近线
    {
        double lp = 0.0, lm = 0.0;
        const bool posOk = estimateLimitInfinity(f, var, +1, &lp);
        const bool negOk = estimateLimitInfinity(f, var, -1, &lm);
        if (posOk && negOk && std::fabs(lp - lm) < 1e-3 * (1.0 + std::fabs(lp))) {
            Constraint c;
            c.kind = ConstraintKind::Asymptote;
            c.value = snapNiceValue(0.5 * (lp + lm));
            c.asymptoteSign = 0;
            rest.push_back(c);
        } else {
            if (posOk) {
                Constraint c;
                c.kind = ConstraintKind::Asymptote;
                c.value = snapNiceValue(lp);
                c.asymptoteSign = 1;
                rest.push_back(c);
            }
            if (negOk) {
                Constraint c;
                c.kind = ConstraintKind::Asymptote;
                c.value = snapNiceValue(lm);
                c.asymptoteSign = -1;
                rest.push_back(c);
            }
        }
    }

    rng.shuffle(rest);

    std::vector<Constraint> ordered;
    // 定义域约束永远放第一条
    {
        Constraint domain;
        domain.kind = ConstraintKind::DomainOn;
        domain.interval = iv;
        ordered.push_back(domain);
    }
    for (Constraint& c : rest) ordered.push_back(std::move(c));
    return ordered;
}

// -----------------------------------------------------------------
// 兜底关卡（理论上走不到）
// -----------------------------------------------------------------
LevelDef fallbackLevel(int index, uint64_t seed, std::string* why) {
    LevelDef L;
    L.id = "endless_" + std::to_string(index);
    L.title = "无尽 " + std::to_string(index) + " · 正弦波";
    L.hint = "从「正弦波」类函数入手，它天然满足本关全部约束。";
    L.chapter = tierOfIndex(index);
    L.parScore = 85.0;
    L.timeLimit = 0.0;
    ExprP f = parseExpr("sin(x)", nullptr);
    const Interval iv = Interval::closed(-kPi, kPi);
    std::vector<Constraint> cands;
    {
        Constraint c;
        c.kind = ConstraintKind::DomainOn;
        c.interval = iv;
        cands.push_back(c);
    }
    {
        Constraint c;
        c.kind = ConstraintKind::BoundaryValue;
        c.point = 0.0;
        c.value = 0.0;
        cands.push_back(c);
    }
    {
        Constraint c;
        c.kind = ConstraintKind::Parity;
        c.even = false;
        cands.push_back(c);
    }
    for (const Constraint& c : cands) {
        if (checkConstraint(c, f).ok) L.constraints.push_back(c);
    }
    if (L.constraints.empty()) {
        Constraint c;
        c.kind = ConstraintKind::BoundaryValue;
        c.point = 0.0;
        c.value = 0.0;
        L.constraints.push_back(c);
    }
    L.reference = "sin(x)";
    if (why && !why->empty()) L.intro = *why;
    (void)seed;
    return L;
}

}  // namespace

bool isBossLevel(int index) { return index > 0 && index % 10 == 0; }

std::string difficultyLabel(int index) {
    if (index < 1) index = 1;
    std::string tier;
    if (index <= 10) tier = "第 1 层 · 基础";
    else if (index <= 30) tier = "第 2 层 · 组合";
    else if (index <= 60) tier = "第 3 层 · 进阶";
    else tier = "第 4 层 · 大师";
    if (isBossLevel(index)) return "Boss · " + tier;
    return tier;
}

LevelDef generateEndlessLevel(int index, uint64_t seed) {
    if (index < 1) index = 1;
    const int tier = tierOfIndex(index);
    const bool boss = isBossLevel(index);
    int target = baseTargetConstraints(index);
    if (boss) target = std::min(6, target + 1);

    LevelDef L;
    L.id = "endless_" + std::to_string(index);
    L.chapter = tier;
    L.timeLimit = 0.0;
    L.parScore = 85.0;

    // 每个关卡使用独立的确定性随机流
    uint64_t mixSeed = seed ^ (static_cast<uint64_t>(index) * 0x9E3779B97F4A7C15ULL);
    Rng rng(splitmix64(mixSeed));

    std::vector<int> order(templates().size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    rng.shuffle(order);

    for (int ti : order) {
        const Template& tpl = templates()[static_cast<size_t>(ti)];
        if (tpl.minTier > tier) continue;
        ParseError pe;
        ExprP f = parseExpr(tpl.expr, &pe);
        if (!f) continue;
        const std::string var = "x";

        // 选一个可用的区间
        std::vector<Interval> ivs = intervalCandidates();
        rng.shuffle(ivs);
        Interval iv;
        bool haveIv = false;
        for (const Interval& cand : ivs) {
            if (intervalUsable(f, var, cand)) {
                iv = cand;
                haveIv = true;
                break;
            }
        }
        if (!haveIv) continue;

        std::vector<Constraint> cands = buildCandidates(f, tpl, tier, iv, rng);

        std::vector<Constraint> kept;
        // 定义域约束（第一条）先验证保留
        if (!cands.empty() && checkConstraint(cands[0], f).ok) {
            kept.push_back(cands[0]);
        }
        // Boss 关强制加入复杂度上限
        if (boss) {
            Constraint c;
            c.kind = ConstraintKind::ComplexityLimit;
            c.maxFuncCalls = funcCallCount(f);
            if (checkConstraint(c, f).ok && static_cast<int>(kept.size()) < target) {
                kept.push_back(c);
            }
        }
        for (size_t i = 1; i < cands.size() && static_cast<int>(kept.size()) < target; ++i) {
            if (checkConstraint(cands[i], f).ok) kept.push_back(cands[i]);
        }
        if (static_cast<int>(kept.size()) < 2) continue;

        L.constraints = std::move(kept);
        L.title = "无尽 " + std::to_string(index) + " · " + tpl.name;
        L.hint = std::string("提示：试试「") + tpl.name +
                 "」类函数，它天然满足本关的全部约束。";
        // 参考解直接使用模板的规范文本：Expr::toStr() 目前对复合子表达式
        // 不加括号（如 1/(1+x*x) 会打印成 1/1 + x·x），不能安全地用于
        // "存储后再解析" 的往返；模板文本本身是规范且可解析的。
        L.reference = tpl.expr;
        std::string verr;
        if (!L.valid(&verr)) {
            std::string why = verr;
            return fallbackLevel(index, seed, &why);
        }
        return L;
    }

    return fallbackLevel(index, seed, nullptr);
}

bool selfTestGenerator(int levels, uint64_t seed, std::string* err) {
    if (levels < 1) {
        if (err) *err = "自检关数必须 >= 1";
        return false;
    }
    for (int i = 1; i <= levels; ++i) {
        const LevelDef L = generateEndlessLevel(i, seed);
        std::string verr;
        if (!L.valid(&verr)) {
            if (err) *err = "第 " + std::to_string(i) + " 关数据非法：" + verr;
            return false;
        }
        ParseError pe;
        ExprP ref = parseExpr(L.reference, &pe);
        if (!ref) {
            if (err) *err = "第 " + std::to_string(i) + " 关参考解无法解析：" + pe.message;
            return false;
        }
        std::vector<ConstraintReport> reports;
        if (!checkAllConstraints(ref, L, &reports)) {
            for (const ConstraintReport& r : reports) {
                if (r.ok) continue;
                if (err) {
                    *err = "第 " + std::to_string(i) + " 关（" + L.id + "）参考解不满足约束「" +
                           r.label + "」：" + r.detail;
                }
                return false;
            }
            if (err) *err = "第 " + std::to_string(i) + " 关参考解未通过全部约束";
            return false;
        }
    }
    if (err) err->clear();
    return true;
}

}  // namespace fcg
