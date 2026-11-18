// Limit.cpp — 极限计算（符号代入 + 数值逼近 + Aitken/Richardson 外推）
//
// 策略：
//   limitAt：解析函数在定义点直接符号代入；否则左右两侧分别采样逼近
//            （h 按 2 的幂递减），用 Aitken Δ² 外推求有限极限；
//            识别 ±∞ 发散与振荡；左右不一致返回 Undefined。
//   limitAtInfinity：先尝试多项式/有理函数次数比较（精确），
//            再用大 x 采样 + Aitken 外推；识别 exp 主导、1/x 型收敛与振荡。
#include "Limit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Numeric.h"

namespace fcg {
namespace {

constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kInf = std::numeric_limits<double>::infinity();

bool evalAt(const ExprP& e, const std::string& var, double x, double* out) {
    if (!e || !std::isfinite(x)) return false;
    RealResult r = evalReal(e, var, x);
    if (!r.ok || !std::isfinite(r.value)) return false;
    if (out) *out = r.value;
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

std::string fmtValue(double v) {
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
    if (std::fabs(v - 2.71828182845904523536) <= 1e-9 * std::max(1.0, std::fabs(v))) return "e";
    if (std::fabs(v + 2.71828182845904523536) <= 1e-9 * std::max(1.0, std::fabs(v))) return "-e";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

// Aitken Δ² 外推聚类：对收敛序列给出稳定估计
bool aitkenCluster(const std::vector<double>& vs, double* out, double relTol = 1e-6) {
    size_t n = vs.size();
    if (n < 3) return false;
    std::vector<std::pair<size_t, double>> ests;
    ests.reserve(n);
    for (size_t k = 0; k + 2 < n; ++k) {
        double d1 = vs[k + 1] - vs[k];
        double d2 = vs[k + 2] - vs[k + 1];
        double denom = d2 - d1;
        double scale = std::max({1.0, std::fabs(vs[k]), std::fabs(vs[k + 1]), std::fabs(vs[k + 2])});
        double est;
        if (std::fabs(denom) <= 1e-14 * scale) {
            est = vs[k + 2];
        } else {
            est = vs[k] - d1 * d1 / denom;
        }
        if (std::isfinite(est)) ests.emplace_back(k, est);
    }
    if (ests.size() < 3) return false;
    int bestCount = 0;
    double bestCand = 0.0;
    for (size_t i = 0; i < ests.size(); ++i) {
        double cand = ests[i].second;
        double tol = relTol * std::max(1.0, std::fabs(cand));
        int cnt = 0;
        for (size_t j = 0; j < ests.size(); ++j) {
            if (std::fabs(ests[j].second - cand) <= tol) ++cnt;
        }
        if (cnt > bestCount) {
            bestCount = cnt;
            bestCand = cand;
        }
    }
    if (bestCount < 3) return false;
    // 聚类中越靠后的估计对应越小的步长 / 越大的 x，精度更高：
    // 取聚类中最后（最大索引）的至多 3 个估计求平均
    double tol = relTol * std::max(1.0, std::fabs(bestCand));
    double sum = 0.0;
    int cnt = 0;
    for (size_t r = ests.size(); r-- > 0 && cnt < 3;) {
        if (std::fabs(ests[r].second - bestCand) <= tol) {
            sum += ests[r].second;
            ++cnt;
        }
    }
    if (cnt == 0) {
        *out = bestCand;
        return true;
    }
    *out = sum / cnt;
    return true;
}

// 收敛判定：先找"差分绝对值持续收缩"的前缀（噪声段会被截掉），
// 再在干净前缀上做 Aitken 外推。返回 No=不像收敛，Finite=得到极限，Slow=像收敛但外推失败。
enum class ConvState { No, Finite, Slow };

ConvState tryConverge(const std::vector<double>& vs, double* out) {
    size_t n = vs.size();
    if (n < 6) return ConvState::No;
    size_t prefix = 1;
    double prevDiff = 0.0;
    bool have = false;
    for (size_t k = 1; k < n; ++k) {
        double d = vs[k] - vs[k - 1];
        if (!have) {
            prevDiff = d;
            have = true;
            prefix = k + 1;
            continue;
        }
        double noiseTol = 1e-13 * std::max(1.0, std::fabs(vs[k]));
        if (std::fabs(d) <= 0.98 * std::fabs(prevDiff) + noiseTol) {
            prevDiff = d;
            prefix = k + 1;
        } else {
            break;
        }
    }
    if (prefix < 6) return ConvState::No;
    std::vector<double> sub(vs.begin(), vs.begin() + prefix);
    if (aitkenCluster(sub, out)) return ConvState::Finite;
    if (sub.size() >= 2 &&
        std::fabs(sub.back() - sub[sub.size() - 2]) <=
            1e-9 * std::max(1.0, std::fabs(sub.back()))) {
        *out = sub.back();
        return ConvState::Finite;
    }
    return ConvState::Slow;
}

// ------------------------------------------------------------------
// 多项式 / 有理函数系数（用于 ∞ 处的精确判定）
// ------------------------------------------------------------------
using Poly = std::map<int, double>;

void cleanPoly(Poly& p) {
    for (auto it = p.begin(); it != p.end();) {
        if (it->second == 0.0)
            it = p.erase(it);
        else
            ++it;
    }
}

int polyDeg(const Poly& p) {
    if (p.empty()) return -1;
    return p.rbegin()->first;
}

bool polyOf(const ExprP& e, const std::string& var, Poly& out) {
    out.clear();
    if (!e) return false;
    switch (e->op) {
        case Op::Num:
            out[0] = e->num;
            return true;
        case Op::Var:
            if (e->var != var) return false;
            out[1] = 1.0;
            return true;
        case Op::Pi:
            out[0] = kPi;
            return true;
        case Op::E:
            out[0] = 2.71828182845904523536;
            return true;
        case Op::Neg: {
            Poly a;
            if (!polyOf(e->a, var, a)) return false;
            for (const auto& kv : a) out[kv.first] = -kv.second;
            cleanPoly(out);
            return true;
        }
        case Op::Add:
        case Op::Sub: {
            Poly a, b;
            if (!polyOf(e->a, var, a) || !polyOf(e->b, var, b)) return false;
            out = a;
            for (const auto& kv : b) out[kv.first] += (e->op == Op::Add ? kv.second : -kv.second);
            cleanPoly(out);
            return true;
        }
        case Op::Mul: {
            Poly a, b;
            if (!polyOf(e->a, var, a) || !polyOf(e->b, var, b)) return false;
            if (!a.empty() && !b.empty() && a.rbegin()->first + b.rbegin()->first > 40) return false;
            for (const auto& x : a) {
                for (const auto& y : b) {
                    out[x.first + y.first] += x.second * y.second;
                }
            }
            cleanPoly(out);
            return true;
        }
        case Op::Div: {
            Poly a, b;
            if (!polyOf(e->a, var, a) || !polyOf(e->b, var, b)) return false;
            if (b.size() != 1) return false;
            auto it = b.begin();
            if (it->first != 0 || it->second == 0.0) return false;
            double c = it->second;
            for (const auto& kv : a) out[kv.first] = kv.second / c;
            cleanPoly(out);
            return true;
        }
        case Op::Pow: {
            if (!isNum(e->b)) return false;
            double kd = e->b->num;
            if (!std::isfinite(kd) || std::fabs(kd - std::round(kd)) > 1e-9 || kd < 0.0 || kd > 8.0)
                return false;
            long long k = static_cast<long long>(std::llround(kd));
            Poly a;
            if (!polyOf(e->a, var, a)) return false;
            out[0] = 1.0;
            for (long long i = 0; i < k; ++i) {
                Poly next;
                if (!out.empty() && !a.empty() && out.rbegin()->first + a.rbegin()->first > 40) return false;
                for (const auto& x : out) {
                    for (const auto& y : a) next[x.first + y.first] += x.second * y.second;
                }
                cleanPoly(next);
                out = std::move(next);
                if (out.empty()) break;
            }
            return true;
        }
        default:
            return false;
    }
}

// ------------------------------------------------------------------
// 单侧极限
// ------------------------------------------------------------------
struct SideResult {
    enum class Kind { Finite, PosInf, NegInf, Undefined, NoData, Unknown };
    Kind kind = Kind::Unknown;
    double value = 0.0;
    std::string detail;
};

SideResult finalizeSide(const std::vector<double>& vs) {
    SideResult s;
    size_t n = vs.size();
    if (n < 3) {
        s.kind = SideResult::Kind::NoData;
        s.detail = "在该侧邻域内无定义";
        return s;
    }
    // 1) 爆炸式增长 → ±∞
    {
        size_t m = std::min<size_t>(6, n);
        bool growing = true;
        for (size_t i = n - m; i + 1 < n; ++i) {
            if (std::fabs(vs[i + 1]) <= std::fabs(vs[i])) {
                growing = false;
                break;
            }
        }
        if (growing && std::fabs(vs.back()) > 1e8) {
            s.kind = vs.back() > 0 ? SideResult::Kind::PosInf : SideResult::Kind::NegInf;
            s.detail = vs.back() > 0 ? "趋于 +∞" : "趋于 -∞";
            return s;
        }
    }
    // 2) 收敛检测（差分收缩前缀 + Aitken 外推，噪声段自动截断）
    {
        double L = 0.0;
        ConvState conv = tryConverge(vs, &L);
        if (conv == ConvState::Finite) {
            s.kind = SideResult::Kind::Finite;
            s.value = L;
            s.detail = "数值逼近 + Richardson/Aitken 外推";
            return s;
        }
        if (conv == ConvState::Slow) {
            s.kind = SideResult::Kind::Unknown;
            s.detail = "收敛缓慢，无法稳定外推";
            return s;
        }
    }
    std::vector<double> diffs;
    {
        size_t m = std::min<size_t>(10, n);
        for (size_t i = n - m; i + 1 < n; ++i) diffs.push_back(vs[i + 1] - vs[i]);
    }
    // 3) 单调发散
    {
        if (diffs.size() >= 4) {
            bool allPos = true, allNeg = true;
            for (double d : diffs) {
                if (d <= 0.0) allPos = false;
                if (d >= 0.0) allNeg = false;
            }
            double fa = std::fabs(diffs.front());
            double la = std::fabs(diffs.back());
            if ((allPos || allNeg) && la >= 0.3 * std::max(fa, 1e-300)) {
                s.kind = allPos ? SideResult::Kind::PosInf : SideResult::Kind::NegInf;
                s.detail = allPos ? "单调发散趋于 +∞" : "单调发散趋于 -∞";
                return s;
            }
        }
    }
    // 3.5) 振荡衰减到 0（例如 x·sin(1/x)）
    {
        size_t head = std::min<size_t>(n, 6);
        double headMax = 0.0;
        for (size_t i = 0; i < head; ++i) headMax = std::max(headMax, std::fabs(vs[i]));
        double tailMax = 0.0;
        size_t tail = std::min<size_t>(n, 5);
        for (size_t i = n - tail; i < n; ++i) tailMax = std::max(tailMax, std::fabs(vs[i]));
        if (headMax > 1e-12 && tailMax <= 1e-3 * headMax && std::fabs(vs.back()) <= 1e-6) {
            s.kind = SideResult::Kind::Finite;
            s.value = vs.back();
            s.detail = "数值逼近（振荡衰减）";
            return s;
        }
    }
    // 4) 振荡 / 无极限
    double maxAbs = 0.0;
    for (double v : vs) maxAbs = std::max(maxAbs, std::fabs(v));
    s.kind = SideResult::Kind::Undefined;
    s.detail = maxAbs < 1e6 ? "在邻域内振荡，无极限" : "在邻域内无有限极限";
    return s;
}

SideResult oneSidedLimit(const ExprP& f, const std::string& var, double target, bool fromLeft) {
    double scale = std::max(1.0, std::fabs(target));
    std::vector<double> vs;
    for (int k = 0; k <= 40; ++k) {
        double h = scale * std::pow(2.0, -static_cast<double>(k + 1));
        double x = fromLeft ? target - h : target + h;
        if (x == target) break;
        double v;
        if (!evalAt(f, var, x, &v)) continue;
        vs.push_back(v);
    }
    return finalizeSide(vs);
}

LimitResult fromSide(const SideResult& s) {
    LimitResult r;
    switch (s.kind) {
        case SideResult::Kind::Finite:
            r.kind = LimitResult::Kind::Finite;
            r.value = s.value;
            break;
        case SideResult::Kind::PosInf:
            r.kind = LimitResult::Kind::PosInf;
            break;
        case SideResult::Kind::NegInf:
            r.kind = LimitResult::Kind::NegInf;
            break;
        case SideResult::Kind::Undefined:
            r.kind = LimitResult::Kind::Undefined;
            break;
        case SideResult::Kind::NoData:
        case SideResult::Kind::Unknown:
            r.kind = LimitResult::Kind::Unknown;
            break;
    }
    r.detail = s.detail;
    return r;
}

bool sideIsInfinite(const SideResult& s) {
    return s.kind == SideResult::Kind::PosInf || s.kind == SideResult::Kind::NegInf;
}

LimitResult combineSides(const SideResult& L, const SideResult& R, Direction dir) {
    LimitResult r;
    if (dir == Direction::Left) {
        r = fromSide(L);
        if (r.kind == LimitResult::Kind::Finite && L.detail.size())
            r.detail = "左极限：" + L.detail;
        return r;
    }
    if (dir == Direction::Right) {
        r = fromSide(R);
        if (r.kind == LimitResult::Kind::Finite && R.detail.size())
            r.detail = "右极限：" + R.detail;
        return r;
    }
    bool lNo = (L.kind == SideResult::Kind::NoData);
    bool rNo = (R.kind == SideResult::Kind::NoData);
    if (lNo && rNo) {
        r.kind = LimitResult::Kind::Unknown;
        r.detail = "目标点邻域内没有定义";
        return r;
    }
    if (lNo || rNo) {
        const SideResult& s = lNo ? R : L;
        r = fromSide(s);
        if (s.kind == SideResult::Kind::Finite) r.detail = "仅在定义域一侧存在极限：" + s.detail;
        return r;
    }
    if (L.kind == SideResult::Kind::Finite && R.kind == SideResult::Kind::Finite) {
        double avg = 0.5 * (L.value + R.value);
        double tol = 1e-6 * std::max(1.0, std::fabs(avg));
        if (std::fabs(L.value - R.value) <= tol) {
            r.kind = LimitResult::Kind::Finite;
            r.value = avg;
            r.detail = "左右极限相等";
            return r;
        }
        r.kind = LimitResult::Kind::Undefined;
        r.detail = "左右极限不相等（左=" + fmtValue(L.value) + "，右=" + fmtValue(R.value) + "）";
        return r;
    }
    if (L.kind == SideResult::Kind::PosInf && R.kind == SideResult::Kind::PosInf) {
        r.kind = LimitResult::Kind::PosInf;
        r.detail = "左右极限均为 +∞";
        return r;
    }
    if (L.kind == SideResult::Kind::NegInf && R.kind == SideResult::Kind::NegInf) {
        r.kind = LimitResult::Kind::NegInf;
        r.detail = "左右极限均为 -∞";
        return r;
    }
    if (sideIsInfinite(L) || sideIsInfinite(R)) {
        r.kind = LimitResult::Kind::Undefined;
        r.detail = "左右极限不一致";
        return r;
    }
    if (L.kind == SideResult::Kind::Undefined || R.kind == SideResult::Kind::Undefined) {
        r.kind = LimitResult::Kind::Undefined;
        r.detail = L.kind == SideResult::Kind::Undefined ? L.detail : R.detail;
        return r;
    }
    r.kind = LimitResult::Kind::Unknown;
    r.detail = "无法判定";
    return r;
}

}  // namespace

// ==================================================================
// LimitResult
// ==================================================================
std::string LimitResult::str() const {
    switch (kind) {
        case Kind::Finite:
            return fmtValue(value);
        case Kind::PosInf:
            return "+∞";
        case Kind::NegInf:
            return "-∞";
        case Kind::Undefined:
            return "不存在";
        default:
            return "未知";
    }
}

// ==================================================================
// x → target
// ==================================================================
LimitResult limitAt(const ExprP& f, const std::string& var, double target, Direction dir) {
    LimitResult r;
    if (!f) {
        r.kind = LimitResult::Kind::Undefined;
        r.detail = "空表达式";
        return r;
    }
    if (!std::isfinite(target)) {
        return limitAtInfinity(f, var, target > 0 ? 1 : -1);
    }
    if (hasOtherVar(f, var)) {
        r.kind = LimitResult::Kind::Unknown;
        r.detail = "包含其他变量，无法判定";
        return r;
    }
    // 解析函数在定义点直接符号代入（解析 ⇒ 连续）
    if (allOpsAnalytic(f)) {
        double v0;
        if (evalAt(f, var, target, &v0)) {
            r.kind = LimitResult::Kind::Finite;
            r.value = v0;
            r.detail = "符号代入";
            return r;
        }
    }
    SideResult left = oneSidedLimit(f, var, target, true);
    SideResult right = oneSidedLimit(f, var, target, false);
    return combineSides(left, right, dir);
}

// ==================================================================
// x → ±∞
// ==================================================================
LimitResult limitAtInfinity(const ExprP& f, const std::string& var, int sign) {
    LimitResult r;
    if (!f) {
        r.kind = LimitResult::Kind::Undefined;
        r.detail = "空表达式";
        return r;
    }
    if (sign == 0) sign = 1;
    if (hasOtherVar(f, var)) {
        r.kind = LimitResult::Kind::Unknown;
        r.detail = "包含其他变量，无法判定";
        return r;
    }
    // 常量
    if (isConst(f)) {
        VarMap vars;
        EvalResult ev = evaluate(f, vars);
        if (ev.ok && ev.value.isFinite() && std::fabs(ev.value.im) <= 1e-9) {
            r.kind = LimitResult::Kind::Finite;
            r.value = ev.value.re;
            r.detail = "常量";
        } else {
            r.kind = LimitResult::Kind::Undefined;
            r.detail = "常量表达式无实数值";
        }
        return r;
    }

    // 符号路径：多项式 / 有理函数次数比较（精确）
    {
        Poly num, den;
        bool rational = false;
        if (f->op == Op::Div) {
            if (polyOf(f->a, var, num) && polyOf(f->b, var, den)) rational = true;
        } else if (polyOf(f, var, num)) {
            den.clear();
            den[0] = 1.0;
            rational = true;
        }
        if (rational) {
            int dn = polyDeg(num);
            int dd = polyDeg(den);
            if (dd < 0) {
                r.kind = LimitResult::Kind::Undefined;
                r.detail = "分母恒为 0";
                return r;
            }
            if (dn < 0) {
                r.kind = LimitResult::Kind::Finite;
                r.value = 0.0;
                r.detail = "分子恒为 0";
                return r;
            }
            double leadN = num[dn];
            double leadD = den[dd];
            if (std::isfinite(leadN) && std::isfinite(leadD) && leadD != 0.0) {
                if (dn > dd) {
                    int power = dn - dd;
                    bool negative = (leadN / leadD) < 0.0;
                    if (sign < 0 && (power % 2 == 1)) negative = !negative;
                    r.kind = negative ? LimitResult::Kind::NegInf : LimitResult::Kind::PosInf;
                    r.detail = "有理函数次数比较";
                    return r;
                }
                if (dn == dd) {
                    r.kind = LimitResult::Kind::Finite;
                    r.value = leadN / leadD;
                    r.detail = "有理函数次数比较";
                    return r;
                }
                r.kind = LimitResult::Kind::Finite;
                r.value = 0.0;
                r.detail = "有理函数次数比较";
                return r;
            }
        }
    }

    // 数值路径：大 x 采样（限制在 1e12 以内，避免 (1+1/x) 之类在双精度下失真）
    std::vector<double> vs;
    for (int k = 0; k <= 50; ++k) {
        double x = sign * 10.0 * std::pow(2.0, static_cast<double>(k));
        if (!std::isfinite(x) || std::fabs(x) > 1e12) break;
        double v;
        if (evalAt(f, var, x, &v)) vs.push_back(v);
    }
    size_t n = vs.size();
    if (n < 4) {
        r.kind = LimitResult::Kind::Undefined;
        r.detail = "在无穷远处邻域内没有定义";
        return r;
    }
    // 1) 爆炸增长
    {
        size_t m = std::min<size_t>(6, n);
        bool growing = true;
        for (size_t i = n - m; i + 1 < n; ++i) {
            if (std::fabs(vs[i + 1]) <= std::fabs(vs[i])) {
                growing = false;
                break;
            }
        }
        if (growing && std::fabs(vs.back()) > 1e8) {
            r.kind = vs.back() > 0 ? LimitResult::Kind::PosInf : LimitResult::Kind::NegInf;
            r.detail = "大 x 采样：爆炸式增长";
            return r;
        }
    }
    // 2) 收敛检测
    {
        double L = 0.0;
        ConvState conv = tryConverge(vs, &L);
        if (conv == ConvState::Finite) {
            r.kind = LimitResult::Kind::Finite;
            r.value = L;
            r.detail = "大 x 采样 + Aitken 外推";
            return r;
        }
        if (conv == ConvState::Slow) {
            r.kind = LimitResult::Kind::Unknown;
            r.detail = "收敛缓慢，无法稳定外推";
            return r;
        }
    }
    std::vector<double> diffs;
    {
        size_t m = std::min<size_t>(10, n);
        for (size_t i = n - m; i + 1 < n; ++i) diffs.push_back(vs[i + 1] - vs[i]);
    }
    // 3) 单调发散
    {
        if (diffs.size() >= 4) {
            bool allPos = true, allNeg = true;
            for (double d : diffs) {
                if (d <= 0.0) allPos = false;
                if (d >= 0.0) allNeg = false;
            }
            double fa = std::fabs(diffs.front());
            double la = std::fabs(diffs.back());
            if ((allPos || allNeg) && la >= 0.3 * std::max(fa, 1e-300)) {
                r.kind = allPos ? LimitResult::Kind::PosInf : LimitResult::Kind::NegInf;
                r.detail = "大 x 采样：单调发散";
                return r;
            }
        }
    }
    // 3.5) 振荡衰减到 0
    {
        size_t head = std::min<size_t>(n, 6);
        double headMax = 0.0;
        for (size_t i = 0; i < head; ++i) headMax = std::max(headMax, std::fabs(vs[i]));
        double tailMax = 0.0;
        size_t tail = std::min<size_t>(n, 5);
        for (size_t i = n - tail; i < n; ++i) tailMax = std::max(tailMax, std::fabs(vs[i]));
        if (headMax > 1e-12 && tailMax <= 1e-3 * headMax && std::fabs(vs.back()) <= 1e-6) {
            r.kind = LimitResult::Kind::Finite;
            r.value = vs.back();
            r.detail = "大 x 采样（振荡衰减）";
            return r;
        }
    }
    // 4) 振荡
    double maxAbs = 0.0;
    for (size_t i = n > 8 ? n - 8 : 0; i < n; ++i) maxAbs = std::max(maxAbs, std::fabs(vs[i]));
    r.kind = LimitResult::Kind::Undefined;
    r.detail = maxAbs < 1e6 ? "在无穷远处振荡，无极限" : "在无穷远处无极限";
    return r;
}

bool horizontalAsymptote(const ExprP& f, const std::string& var, int sign, double* out) {
    LimitResult r = limitAtInfinity(f, var, sign);
    if (r.kind == LimitResult::Kind::Finite) {
        if (out) *out = r.value;
        return true;
    }
    return false;
}

}  // namespace fcg
