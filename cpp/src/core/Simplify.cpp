// Simplify.cpp — 符号化简与符号等价判定（由 engine-simplify 实现）
//
// 设计要点
// -----------------------------------------------------------------------
// 1) 域安全优先：不做会扩大定义域的化简。
//    - x/x 不化为 1（会丢失 x≠0），除非能证明分子恒非零（如 exp(x)/exp(x)）；
//    - sqrt(x^2) 化为 abs(x)，而不是 x；
//    - exp(ln(x)) 只有在 ln 的参数可证恒正、且不扩大定义域时才化为 x；
//    - (x^2)^(1/2) 不化为 x；(x^a)^b 只在整数指数下合并；
//    - x^-1 * x^2 不合并为 x（否则丢掉 x≠0）。
//    （"0 因子吞噬" 0*f → 0、a-a → 0 按主流 CAS 的代数恒等式处理，注释见下。）
// 2) 单轮化简 = 自底向上的局部重写；simplify() 迭代到结构不再变化。
// 3) 深度 / 节点数 / 迭代次数 / 分配律规模 四重保护，防爆栈与表达式爆炸。
// -----------------------------------------------------------------------

#include "Simplify.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Numeric.h"

namespace fcg {
namespace {

// ---------------- 保护参数 ----------------
constexpr int kMaxDepth = 400;       // 递归深度上限
constexpr int kMaxIterations = 24;   // simplify 迭代上限
constexpr int kMaxNodes = 30000;     // 表达式节点数上限
constexpr int kDistributeNodeLimit = 256;  // 分配律：两侧节点数乘积上限
constexpr int kDistributeTermLimit = 8;    // 分配律：和式的项数上限
constexpr int kCancelFactorLimit = 64;     // 约分参与的因子数上限

bool finiteVal(double v) { return std::isfinite(v); }

// 是否为"整数值"的数值节点（用于整数指数判定）
bool intValue(const ExprP& e, double* out) {
    if (!e || e->op != Op::Num) return false;
    double v = e->num;
    if (!std::isfinite(v) || std::fabs(v) > 1e12) return false;
    double r = std::round(v);
    if (std::fabs(v - r) > 1e-9) return false;
    if (out) *out = r;
    return true;
}

bool evenIntValue(const ExprP& e, double* out) {
    double v = 0.0;
    if (!intValue(e, &v)) return false;
    if (std::fmod(std::fabs(v), 2.0) != 0.0) return false;
    if (out) *out = v;
    return true;
}

// 常量表达式求实数值（失败返回 false，不改变节点）
bool constRealValue(const ExprP& e, double* out) {
    if (!isConst(e)) return false;
    VarMap m;
    auto r = evaluate(e, m);
    if (!r.ok || !std::isfinite(r.value.re) || std::fabs(r.value.im) > 1e-12) return false;
    if (out) *out = r.value.re;
    return true;
}

// 值是否"接近整数"（用于安全折叠 sin(pi)=0、ln(e)=1 之类）
bool nearInteger(double v) {
    if (!std::isfinite(v) || std::fabs(v) > 1e15) return false;
    double r = std::round(v);
    return std::fabs(v - r) <= 1e-12 * std::max(1.0, std::fabs(v));
}

// ---------------- 前向声明 ----------------
std::string keyOf(const ExprP& e, int depth);
int signStrict(const ExprP& e, int depth);
bool nonnegIsh(const ExprP& e, int depth);
bool nonposIsh(const ExprP& e, int depth);
bool totalIsh(const ExprP& e, int depth);

ExprP makeNegS(const ExprP& a);
ExprP makeAddS(const ExprP& a, const ExprP& b);
ExprP makeSubS(const ExprP& a, const ExprP& b);
ExprP makeMulS(const ExprP& a, const ExprP& b);
ExprP makeDivS(const ExprP& a, const ExprP& b);
ExprP makePowS(const ExprP& base, const ExprP& expn);
ExprP makeFunc1S(Op op, const ExprP& a);
ExprP makeFunc2S(Op op, const ExprP& a, const ExprP& b);
ExprP simpRec(const ExprP& e, int depth);

bool definitelyNonzero(const ExprP& e) { return signStrict(e, 0) != 0; }
bool definitelyPositive(const ExprP& e) { return signStrict(e, 0) > 0; }
bool totalExpr(const ExprP& e) { return totalIsh(e, 0); }

// ---------------- 符号 / 定义域分析 ----------------
// signStrict: +1 恒正, -1 恒负, 0 无法确定（可能为 0 或未知）
int signStrict(const ExprP& e, int depth) {
    if (!e || depth > kMaxDepth) return 0;
    switch (e->op) {
        case Op::Num:
            return e->num > 0.0 ? 1 : (e->num < 0.0 ? -1 : 0);
        case Op::Pi:
        case Op::E:
            return 1;
        case Op::I:
            return 0;
        case Op::Neg:
            return -signStrict(e->a, depth + 1);
        case Op::Add: {
            if (nonnegIsh(e->a, depth + 1) && nonnegIsh(e->b, depth + 1)) {
                if (signStrict(e->a, depth + 1) > 0 || signStrict(e->b, depth + 1) > 0) return 1;
                return 0;
            }
            if (nonposIsh(e->a, depth + 1) && nonposIsh(e->b, depth + 1)) {
                if (signStrict(e->a, depth + 1) < 0 || signStrict(e->b, depth + 1) < 0) return -1;
                return 0;
            }
            return 0;
        }
        case Op::Sub: {
            if (nonnegIsh(e->a, depth + 1) && nonposIsh(e->b, depth + 1)) {
                if (signStrict(e->a, depth + 1) > 0 || signStrict(e->b, depth + 1) < 0) return 1;
                return 0;
            }
            if (nonposIsh(e->a, depth + 1) && nonnegIsh(e->b, depth + 1)) {
                if (signStrict(e->a, depth + 1) < 0 || signStrict(e->b, depth + 1) > 0) return -1;
                return 0;
            }
            return 0;
        }
        case Op::Mul: {
            int sa = signStrict(e->a, depth + 1);
            if (sa == 0) return 0;
            int sb = signStrict(e->b, depth + 1);
            if (sb == 0) return 0;
            return sa * sb;
        }
        case Op::Div: {
            int sa = signStrict(e->a, depth + 1);
            if (sa == 0) return 0;
            int sb = signStrict(e->b, depth + 1);
            if (sb == 0) return 0;
            return sa * sb;
        }
        case Op::Abs:
            return signStrict(e->a, depth + 1) != 0 ? 1 : 0;
        case Op::Exp:
        case Op::Cosh:
            return 1;
        case Op::Sqrt:
            return signStrict(e->a, depth + 1) > 0 ? 1 : 0;
        case Op::Cbrt:
            return signStrict(e->a, depth + 1);
        case Op::Asinh:
        case Op::Tanh:
        case Op::Atan:
            return signStrict(e->a, depth + 1);
        case Op::Pow: {
            double n = 0.0;
            if (intValue(e->b, &n)) {
                int sb = signStrict(e->a, depth + 1);
                if (sb == 0) return 0;
                if (std::fmod(std::fabs(n), 2.0) == 0.0) return 1;
                return sb;
            }
            return signStrict(e->a, depth + 1) > 0 ? 1 : 0;
        }
        default:
            return 0;
    }
}

bool nonnegIsh(const ExprP& e, int depth) {
    if (!e || depth > kMaxDepth) return false;
    switch (e->op) {
        case Op::Num:
            return e->num >= 0.0;
        case Op::Pi:
        case Op::E:
            return true;
        case Op::Abs:
        case Op::Exp:
        case Op::Cosh:
        case Op::Sqrt:
            return true;
        case Op::Pow: {
            double n = 0.0;
            if (intValue(e->b, &n)) {
                if (std::fmod(std::fabs(n), 2.0) == 0.0) return true;
                return signStrict(e->a, depth + 1) > 0;
            }
            return signStrict(e->a, depth + 1) > 0;
        }
        case Op::Neg:
            return nonposIsh(e->a, depth + 1);
        case Op::Add:
            return nonnegIsh(e->a, depth + 1) && nonnegIsh(e->b, depth + 1);
        case Op::Sub:
            return nonnegIsh(e->a, depth + 1) && nonposIsh(e->b, depth + 1);
        case Op::Mul: {
            int negCount = 0;
            const ExprP fs[2] = {e->a, e->b};
            for (const auto& f : fs) {
                if (nonnegIsh(f, depth + 1)) continue;
                if (nonposIsh(f, depth + 1)) {
                    ++negCount;
                    continue;
                }
                return false;
            }
            return negCount % 2 == 0;
        }
        case Op::Div: {
            bool same = nonnegIsh(e->a, depth + 1) && nonnegIsh(e->b, depth + 1);
            bool both = nonposIsh(e->a, depth + 1) && nonposIsh(e->b, depth + 1);
            return same || both;
        }
        case Op::Min:
        case Op::Max:
            return nonnegIsh(e->a, depth + 1) && nonnegIsh(e->b, depth + 1);
        default:
            return signStrict(e, depth + 1) > 0;
    }
}

bool nonposIsh(const ExprP& e, int depth) {
    if (!e || depth > kMaxDepth) return false;
    switch (e->op) {
        case Op::Num:
            return e->num <= 0.0;
        case Op::Neg:
            return nonnegIsh(e->a, depth + 1);
        case Op::Add:
            return nonposIsh(e->a, depth + 1) && nonposIsh(e->b, depth + 1);
        case Op::Sub:
            return nonposIsh(e->a, depth + 1) && nonnegIsh(e->b, depth + 1);
        case Op::Mul: {
            int negCount = 0;
            const ExprP fs[2] = {e->a, e->b};
            for (const auto& f : fs) {
                if (nonposIsh(f, depth + 1)) {
                    ++negCount;
                    continue;
                }
                if (nonnegIsh(f, depth + 1)) continue;
                return false;
            }
            return negCount % 2 == 1;
        }
        case Op::Div: {
            bool mix = nonposIsh(e->a, depth + 1) && nonnegIsh(e->b, depth + 1);
            bool both = nonnegIsh(e->a, depth + 1) && nonposIsh(e->b, depth + 1);
            return mix || both;
        }
        case Op::Min:
        case Op::Max:
            return nonposIsh(e->a, depth + 1) && nonposIsh(e->b, depth + 1);
        default:
            return signStrict(e, depth + 1) < 0;
    }
}

// 定义域是否覆盖全体实数（可静态证明）
bool totalIsh(const ExprP& e, int depth) {
    if (!e || depth > kMaxDepth) return false;
    switch (e->op) {
        case Op::Num:
            return std::isfinite(e->num);
        case Op::Var:
        case Op::Pi:
        case Op::E:
        case Op::I:
            return true;
        case Op::Neg:
        case Op::Sin:
        case Op::Cos:
        case Op::Exp:
        case Op::Cbrt:
        case Op::Atan:
        case Op::Sinh:
        case Op::Cosh:
        case Op::Tanh:
        case Op::Asinh:
        case Op::Abs:
        case Op::Floor:
        case Op::Ceil:
        case Op::Sign:
            return totalIsh(e->a, depth + 1);
        case Op::Add:
        case Op::Sub:
        case Op::Mul:
        case Op::Min:
        case Op::Max:
            return totalIsh(e->a, depth + 1) && totalIsh(e->b, depth + 1);
        case Op::Div:
            return totalIsh(e->a, depth + 1) && totalIsh(e->b, depth + 1) &&
                   signStrict(e->b, depth + 1) != 0;
        case Op::Mod:
            return totalIsh(e->a, depth + 1) && totalIsh(e->b, depth + 1) &&
                   signStrict(e->b, depth + 1) != 0;
        case Op::Pow: {
            double n = 0.0;
            if (intValue(e->b, &n)) {
                if (n > 0.0) return totalIsh(e->a, depth + 1);
                if (n == 0.0) return totalIsh(e->a, depth + 1);
                return totalIsh(e->a, depth + 1) && signStrict(e->a, depth + 1) != 0;
            }
            return totalIsh(e->a, depth + 1) && signStrict(e->a, depth + 1) > 0;
        }
        case Op::Ln:
        case Op::Log2:
        case Op::Log10:
            return totalIsh(e->a, depth + 1) && signStrict(e->a, depth + 1) > 0;
        case Op::Sqrt:
            return totalIsh(e->a, depth + 1) && nonnegIsh(e->a, depth + 1);
        case Op::Tan:
        case Op::Asin:
        case Op::Acos:
        case Op::Acosh:
        case Op::Atanh: {
            double v = 0.0;
            return constRealValue(e, &v);
        }
        default:
            return false;
    }
}

// ---------------- 规范键（可交换运算排序） ----------------
std::string numKey(const ExprP& e) {
    char buf[48];
    double v = (e->num == 0.0) ? 0.0 : e->num;  // 归一 -0
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string("n") + buf;
}

void sumKeyItems(const ExprP& e, bool neg, std::vector<std::string>& items, int depth) {
    if (!e || depth > kMaxDepth) {
        items.push_back("?");
        return;
    }
    switch (e->op) {
        case Op::Add:
            sumKeyItems(e->a, neg, items, depth + 1);
            sumKeyItems(e->b, neg, items, depth + 1);
            return;
        case Op::Sub:
            sumKeyItems(e->a, neg, items, depth + 1);
            sumKeyItems(e->b, !neg, items, depth + 1);
            return;
        case Op::Neg:
            sumKeyItems(e->a, !neg, items, depth + 1);
            return;
        default:
            if (e->op == Op::Num) {
                // 数值项把符号并入键，保证 -1 与 +(-1) 同键
                double v = neg ? -e->num : e->num;
                char buf[48];
                std::snprintf(buf, sizeof(buf), "%s%.17g", v < 0.0 ? "-n" : "+n",
                              std::fabs(v == 0.0 ? 0.0 : v));
                items.push_back(buf);
                return;
            }
            items.push_back((neg ? std::string("-") : std::string("+")) + keyOf(e, depth + 1));
            return;
    }
}

void mulKeyItems(const ExprP& e, bool neg, std::vector<std::string>& items, int depth) {
    if (!e || depth > kMaxDepth) {
        items.push_back("?");
        return;
    }
    switch (e->op) {
        case Op::Mul:
            mulKeyItems(e->a, neg, items, depth + 1);
            mulKeyItems(e->b, neg, items, depth + 1);
            return;
        case Op::Neg:
            mulKeyItems(e->a, !neg, items, depth + 1);
            return;
        default:
            items.push_back((neg ? std::string("~") : std::string("")) + keyOf(e, depth + 1));
            return;
    }
}

std::string joinKeys(std::vector<std::string> items, const char* prefix) {
    std::sort(items.begin(), items.end());
    std::string s(prefix);
    s += "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) s += ",";
        s += items[i];
    }
    s += "]";
    return s;
}

std::string keyOf(const ExprP& e, int depth) {
    if (!e) return "?";
    if (depth > kMaxDepth) return "#";
    switch (e->op) {
        case Op::Num:
            return numKey(e);
        case Op::Var:
            return "v" + e->var;
        case Op::Pi:
            return "PI";
        case Op::E:
            return "E";
        case Op::I:
            return "I";
        case Op::Add:
        case Op::Sub: {
            std::vector<std::string> items;
            sumKeyItems(e, false, items, depth);
            return joinKeys(std::move(items), "S");
        }
        case Op::Mul: {
            std::vector<std::string> items;
            mulKeyItems(e, false, items, depth);
            return joinKeys(std::move(items), "M");
        }
        case Op::Div:
            return "d(" + keyOf(e->a, depth + 1) + "," + keyOf(e->b, depth + 1) + ")";
        case Op::Pow:
            return "p(" + keyOf(e->a, depth + 1) + "," + keyOf(e->b, depth + 1) + ")";
        case Op::Min:
        case Op::Max: {
            std::string ka = keyOf(e->a, depth + 1);
            std::string kb = keyOf(e->b, depth + 1);
            if (ka > kb) std::swap(ka, kb);
            return std::string(e->op == Op::Min ? "min(" : "max(") + ka + "," + kb + ")";
        }
        default:
            if (opArity(e->op) == 2) {
                return std::string(opName(e->op)) + "(" + keyOf(e->a, depth + 1) + "," +
                       keyOf(e->b, depth + 1) + ")";
            }
            return std::string(opName(e->op)) + "(" + keyOf(e->a, depth + 1) + ")";
    }
}

// ---------------- 复合构造函数 ----------------
// 负号规范化
ExprP makeNegS(const ExprP& a) {
    if (!a) return a;
    switch (a->op) {
        case Op::Num: {
            double v = -a->num;
            if (v == 0.0) v = 0.0;
            return mkNum(v);
        }
        case Op::Neg:
            return a->a;
        case Op::Add:
            // -(a+b) = -a-b（保域），统一成和式规范形
            return makeAddS(makeNegS(a->a), makeNegS(a->b));
        case Op::Sub:
            return makeAddS(a->b, makeNegS(a->a));
        case Op::Mul: {
            if (isNum(a->a)) {
                double c = -a->a->num;
                if (c == 1.0) return a->b;
                if (c == -1.0) return makeNegS(a->b);
                return mkMul(mkNum(c), a->b);
            }
            return mkNeg(a);
        }
        default:
            return mkNeg(a);
    }
}

// ---- 和式 ----
struct SumTerm {
    bool neg = false;
    ExprP node;
};

void collectSum(const ExprP& e, bool neg, std::vector<SumTerm>& out, int depth) {
    if (!e || depth > kMaxDepth) {
        if (e) out.push_back({neg, e});
        return;
    }
    switch (e->op) {
        case Op::Add:
            collectSum(e->a, neg, out, depth + 1);
            collectSum(e->b, neg, out, depth + 1);
            return;
        case Op::Sub:
            collectSum(e->a, neg, out, depth + 1);
            collectSum(e->b, !neg, out, depth + 1);
            return;
        case Op::Neg:
            collectSum(e->a, !neg, out, depth + 1);
            return;
        default:
            out.push_back({neg, e});
            return;
    }
}

struct TermGroup {
    double coeff = 0.0;
    ExprP rest;
};

ExprP termWithCoeff(double c, const ExprP& rest) {
    double a = std::fabs(c);
    if (a == 1.0) return rest;
    return makeMulS(mkNum(a), rest);
}

ExprP buildSum(std::vector<SumTerm>& terms) {
    double constSum = 0.0;
    std::map<std::string, TermGroup> groups;
    for (const auto& term : terms) {
        if (!term.node) continue;
        double coeff = term.neg ? -1.0 : 1.0;
        ExprP rest = term.node;
        for (int guard = 0; guard < 8; ++guard) {
            if (!rest) break;
            if (rest->op == Op::Neg) {
                coeff = -coeff;
                rest = rest->a;
                continue;
            }
            if (rest->op == Op::Mul && isNum(rest->a)) {
                coeff *= rest->a->num;
                rest = rest->b;
                continue;
            }
            if (rest->op == Op::Div && isNum(rest->b) && rest->b->num != 0.0) {
                coeff /= rest->b->num;
                rest = rest->a;
                continue;
            }
            break;
        }
        if (!rest) {
            constSum += coeff;
            continue;
        }
        if (isNum(rest)) {
            constSum += coeff * rest->num;
            continue;
        }
        std::string k = keyOf(rest, 0);
        auto it = groups.find(k);
        if (it == groups.end()) {
            TermGroup g;
            g.coeff = coeff;
            g.rest = rest;
            groups.emplace(std::move(k), std::move(g));
        } else {
            it->second.coeff += coeff;
        }
    }

    std::vector<ExprP> posTerms;
    std::vector<ExprP> negTerms;
    for (auto& kv : groups) {
        TermGroup& g = kv.second;
        if (g.coeff == 0.0) continue;  // 同类项抵消（a - a = 0）
        if (g.coeff > 0.0) {
            posTerms.push_back(termWithCoeff(g.coeff, g.rest));
        } else {
            negTerms.push_back(termWithCoeff(-g.coeff, g.rest));
        }
    }

    ExprP acc;
    // 只有负项 + 正常数：写成 "c - ..." 更自然，也保持结构稳定
    if (posTerms.empty() && !negTerms.empty() && constSum > 0.0) {
        acc = mkNum(constSum);
        for (const auto& t : negTerms) acc = mkSub(acc, t);
        return acc;
    }
    for (const auto& t : posTerms) acc = acc ? mkAdd(acc, t) : t;
    for (const auto& t : negTerms) acc = acc ? mkSub(acc, t) : makeNegS(t);
    if (constSum != 0.0) {
        if (constSum > 0.0) {
            ExprP c = mkNum(constSum);
            acc = acc ? mkAdd(acc, c) : c;
        } else {
            ExprP c = mkNum(-constSum);
            acc = acc ? mkSub(acc, c) : mkNum(constSum);
        }
    }
    return acc ? acc : mkNum(0.0);
}

ExprP makeAddS(const ExprP& a, const ExprP& b) {
    std::vector<SumTerm> terms;
    collectSum(a, false, terms, 0);
    collectSum(b, false, terms, 0);
    return buildSum(terms);
}

ExprP makeSubS(const ExprP& a, const ExprP& b) {
    std::vector<SumTerm> terms;
    collectSum(a, false, terms, 0);
    collectSum(b, true, terms, 0);
    return buildSum(terms);
}

// ---- 乘积 ----
struct MulFactor {
    bool neg = false;
    ExprP node;
};

void collectProduct(const ExprP& e, bool neg, std::vector<MulFactor>& out, int depth) {
    if (!e || depth > kMaxDepth) {
        if (e) out.push_back({neg, e});
        return;
    }
    switch (e->op) {
        case Op::Mul:
            collectProduct(e->a, neg, out, depth + 1);
            collectProduct(e->b, neg, out, depth + 1);
            return;
        case Op::Neg:
            collectProduct(e->a, !neg, out, depth + 1);
            return;
        default:
            out.push_back({neg, e});
            return;
    }
}

// 指数合并是否保域：整数且同号，或底数恒正
bool exponentsMergeSafe(const ExprP& base, const std::vector<ExprP>& exps) {
    if (definitelyPositive(base)) return true;
    bool allInt = true;
    bool allNonneg = true;
    bool allNonpos = true;
    for (const auto& e : exps) {
        double v = 0.0;
        if (!intValue(e, &v)) {
            allInt = false;
            break;
        }
        if (v < 0.0) allNonneg = false;
        if (v > 0.0) allNonpos = false;
    }
    return allInt && (allNonneg || allNonpos);
}

// 把（已化简的）因子列表合并成乘积：数值系数、同底幂合并、因子排序
ExprP mulProductList(const std::vector<MulFactor>& factors) {
    double coeff = 1.0;
    std::vector<ExprP> fs;
    for (const auto& f : factors) {
        if (!f.node) continue;
        if (isNum(f.node)) {
            coeff *= f.neg ? -f.node->num : f.node->num;
            continue;
        }
        if (f.neg) coeff = -coeff;
        fs.push_back(f.node);
    }
    if (!finiteVal(coeff)) return mkNum(coeff);
    if (coeff == 0.0) return mkNum(0.0);  // 0 因子吞噬
    if (fs.empty()) return mkNum(coeff);

    struct Group {
        ExprP base;
        std::vector<ExprP> exps;
        std::vector<ExprP> orig;
    };
    std::map<std::string, Group> groups;
    for (const auto& f : fs) {
        ExprP base = f;
        ExprP expn = mkNum(1.0);
        if (f->op == Op::Pow) {
            base = f->a;
            expn = f->b;
        }
        std::string k = keyOf(base, 0);
        auto it = groups.find(k);
        if (it == groups.end()) {
            Group g;
            g.base = base;
            it = groups.emplace(k, std::move(g)).first;
        }
        it->second.exps.push_back(expn);
        it->second.orig.push_back(f);
    }

    std::vector<ExprP> merged;
    for (auto& kv : groups) {
        Group& g = kv.second;
        if (g.orig.size() == 1) {
            merged.push_back(g.orig[0]);
            continue;
        }
        if (!exponentsMergeSafe(g.base, g.exps)) {
            for (const auto& o : g.orig) merged.push_back(o);
            continue;
        }
        bool allNum = true;
        double sum = 0.0;
        for (const auto& e : g.exps) {
            if (!isNum(e)) {
                allNum = false;
                break;
            }
            sum += e->num;
        }
        ExprP newExp;
        if (allNum) {
            newExp = mkNum(sum);
        } else {
            ExprP acc = g.exps[0];
            for (size_t i = 1; i < g.exps.size(); ++i) acc = makeAddS(acc, g.exps[i]);
            newExp = acc;
        }
        merged.push_back(makePowS(g.base, newExp));
    }

    std::sort(merged.begin(), merged.end(), [](const ExprP& x, const ExprP& y) {
        return keyOf(x, 0) < keyOf(y, 0);
    });
    ExprP p = merged[0];
    for (size_t i = 1; i < merged.size(); ++i) p = mkMul(p, merged[i]);
    if (coeff == 1.0) return p;
    if (coeff == -1.0) return makeNegS(p);
    return mkMul(mkNum(coeff), p);
}

int countSumTerms(const ExprP& e, int depth) {
    if (!e || depth > 64) return 1;
    switch (e->op) {
        case Op::Add:
        case Op::Sub:
            return countSumTerms(e->a, depth + 1) + countSumTerms(e->b, depth + 1);
        case Op::Neg:
            return countSumTerms(e->a, depth + 1);
        default:
            return 1;
    }
}

ExprP buildProductFromFactors(const std::vector<MulFactor>& factors);
ExprP distributeProduct(const std::vector<MulFactor>& factors, size_t idx);

// 由因子列表构建乘积；和式因子在规模受控时使用分配律展开
ExprP buildProductFromFactors(const std::vector<MulFactor>& factors) {
    if (factors.empty()) return mkNum(1.0);
    for (const auto& f : factors) {
        if (!f.node) continue;
        if (isNum(f.node) && f.node->num == 0.0) return mkNum(0.0);  // 0 因子吞噬
    }
    int sumIdx = -1;
    long long sumTerms = 1;
    long long nodeProd = 1;
    for (size_t i = 0; i < factors.size(); ++i) {
        const ExprP& n = factors[i].node;
        if (!n) continue;
        long long nc = nodeCount(n);
        if (nc > 100000) nc = 100000;
        nodeProd *= nc;
        if (nodeProd > 1000000) nodeProd = 1000000;
        if (n->op == Op::Add || n->op == Op::Sub) {
            if (sumIdx < 0) sumIdx = static_cast<int>(i);
            sumTerms *= countSumTerms(n, 0);
            if (sumTerms > 1000) sumTerms = 1000;
        }
    }
    if (sumIdx >= 0 && sumTerms <= kDistributeTermLimit && nodeProd <= kDistributeNodeLimit) {
        return distributeProduct(factors, static_cast<size_t>(sumIdx));
    }
    return mulProductList(factors);
}

ExprP distributeProduct(const std::vector<MulFactor>& factors, size_t idx) {
    if (idx >= factors.size()) return mulProductList(factors);
    const MulFactor sf = factors[idx];
    if (!sf.node || (sf.node->op != Op::Add && sf.node->op != Op::Sub)) {
        return mulProductList(factors);
    }
    std::vector<MulFactor> rest;
    rest.reserve(factors.size());
    for (size_t i = 0; i < factors.size(); ++i) {
        if (i != idx) rest.push_back(factors[i]);
    }
    const ExprP& s = sf.node;
    std::vector<MulFactor> left = rest;
    left.push_back({sf.neg, s->a});
    std::vector<MulFactor> right = rest;
    right.push_back({sf.neg, s->b});
    if (s->op == Op::Add) {
        return makeAddS(buildProductFromFactors(left), buildProductFromFactors(right));
    }
    return makeSubS(buildProductFromFactors(left), buildProductFromFactors(right));
}

ExprP makeMulS(const ExprP& a, const ExprP& b) {
    if (!a || !b) return mkNum(0.0);
    std::vector<MulFactor> factors;
    collectProduct(a, false, factors, 0);
    collectProduct(b, false, factors, 0);
    return buildProductFromFactors(factors);
}

// ---- 除法约分 ----
void flattenMulFactors(const ExprP& e, bool neg, std::vector<MulFactor>& out, int depth) {
    if (!e) return;
    if (depth > kMaxDepth) {
        out.push_back({neg, e});
        return;
    }
    switch (e->op) {
        case Op::Mul:
            flattenMulFactors(e->a, neg, out, depth + 1);
            flattenMulFactors(e->b, neg, out, depth + 1);
            return;
        case Op::Neg:
            flattenMulFactors(e->a, !neg, out, depth + 1);
            return;
        default:
            out.push_back({neg, e});
            return;
    }
}

ExprP rebuildProduct(const std::vector<MulFactor>& items) {
    double coeff = 1.0;
    std::vector<ExprP> fs;
    for (const auto& it : items) {
        if (!it.node) continue;
        if (isNum(it.node)) {
            coeff *= it.neg ? -it.node->num : it.node->num;
            continue;
        }
        if (it.neg) coeff = -coeff;
        fs.push_back(it.node);
    }
    if (!finiteVal(coeff)) return mkNum(coeff);
    if (coeff == 0.0) return mkNum(0.0);
    if (fs.empty()) return mkNum(coeff);
    std::sort(fs.begin(), fs.end(), [](const ExprP& x, const ExprP& y) {
        return keyOf(x, 0) < keyOf(y, 0);
    });
    ExprP p = fs[0];
    for (size_t i = 1; i < fs.size(); ++i) p = mkMul(p, fs[i]);
    if (coeff == 1.0) return p;
    if (coeff == -1.0) return makeNegS(p);
    return mkMul(mkNum(coeff), p);
}

ExprP makeDivS(const ExprP& a, const ExprP& b) {
    if (!a) return mkNum(0.0);
    if (!b) return mkNum(0.0);
    if (isNum(b)) {
        if (b->num == 1.0) return a;
        if (b->num == -1.0) return makeNegS(a);
        if (isNum(a)) {
            double r = a->num / b->num;
            if (finiteVal(r)) return mkNum(r);
            return mkDiv(a, b);
        }
    }
    if (isNum(a) && a->num == 0.0) return mkNum(0.0);
    if (a->op == Op::Neg) return makeNegS(makeDivS(a->a, b));
    if (b->op == Op::Neg) return makeNegS(makeDivS(a, b->a));
    // 同构/同键相除：仅当可判定非零才化为 1（域安全）
    if (structurallyEqual(a, b) || keyOf(a, 0) == keyOf(b, 0)) {
        if (definitelyNonzero(a)) return mkNum(1.0);
        return mkDiv(a, b);
    }
    // 同底幂相除：底数恒非零时指数相减保域
    if (a->op == Op::Pow && b->op == Op::Pow && keyOf(a->a, 0) == keyOf(b->a, 0) &&
        definitelyNonzero(a->a)) {
        return makePowS(a->a, makeSubS(a->b, b->b));
    }
    // 乘积因子约分（只有恒非零的因子才能约去）
    std::vector<MulFactor> fa;
    std::vector<MulFactor> fb;
    flattenMulFactors(a, false, fa, 0);
    flattenMulFactors(b, false, fb, 0);
    if (fa.size() + fb.size() <= static_cast<size_t>(kCancelFactorLimit) &&
        fa.size() + fb.size() > 1) {
        std::vector<std::string> ka(fa.size());
        std::vector<std::string> kb(fb.size());
        for (size_t i = 0; i < fa.size(); ++i) {
            ka[i] = (fa[i].neg ? std::string("~") : std::string("")) + keyOf(fa[i].node, 0);
        }
        for (size_t j = 0; j < fb.size(); ++j) {
            kb[j] = (fb[j].neg ? std::string("~") : std::string("")) + keyOf(fb[j].node, 0);
        }
        std::vector<bool> deadA(fa.size(), false);
        std::vector<bool> deadB(fb.size(), false);
        bool cancelled = false;
        for (size_t i = 0; i < fa.size(); ++i) {
            if (deadA[i] || !definitelyNonzero(fa[i].node)) continue;
            for (size_t j = 0; j < fb.size(); ++j) {
                if (deadB[j]) continue;
                if (ka[i] == kb[j]) {
                    deadA[i] = true;
                    deadB[j] = true;
                    cancelled = true;
                    break;
                }
            }
        }
        if (cancelled) {
            std::vector<MulFactor> ra;
            std::vector<MulFactor> rb;
            for (size_t i = 0; i < fa.size(); ++i) {
                if (!deadA[i]) ra.push_back(fa[i]);
            }
            for (size_t j = 0; j < fb.size(); ++j) {
                if (!deadB[j]) rb.push_back(fb[j]);
            }
            return makeDivS(rebuildProduct(ra), rebuildProduct(rb));
        }
    }
    return mkDiv(a, b);
}

// ---- 幂 ----
ExprP makePowS(const ExprP& base, const ExprP& expn) {
    if (!base) return mkNum(1.0);
    if (!expn) return mkNum(1.0);
    double n = 0.0;
    bool expIsInt = intValue(expn, &n);
    bool expIsNum = isNum(expn);
    double ev = expIsNum ? expn->num : 0.0;

    // 常量折叠
    if (isNum(base) && expIsNum) {
        double r = 0.0;
        bool ok = false;
        if (expIsInt) {
            r = std::pow(base->num, n);
            ok = finiteVal(r);
        } else if (base->num > 0.0) {
            r = std::pow(base->num, ev);
            ok = finiteVal(r);
        } else if (base->num == 0.0 && ev > 0.0) {
            r = 0.0;
            ok = true;
        }
        if (ok) return mkNum(r);
        return mkPow(base, expn);
    }
    if (isNum(base) && base->num == 0.0) {
        if (expIsNum) {
            if (ev > 0.0) return mkNum(0.0);
            if (ev == 0.0) return mkNum(1.0);
        }
        return mkPow(base, expn);
    }
    if (expIsNum && ev == 1.0) return base;
    if (expIsNum && ev == 0.0) {
        // x^0 = 1 仅在底数定义域覆盖全体实数时保域
        if (totalExpr(base)) return mkNum(1.0);
        return mkPow(base, expn);
    }
    if (isNum(base) && base->num == 1.0) {
        if (totalExpr(expn)) return mkNum(1.0);
        return mkPow(base, expn);
    }
    // (u^p)^q -> u^(p*q)，仅整数指数（保域）
    double p = 0.0;
    if (base->op == Op::Pow && intValue(base->b, &p) && expIsInt) {
        return makePowS(base->a, mkNum(p * n));
    }
    // (sqrt(u))^n：u>=0 时可降幂
    if (base->op == Op::Sqrt && expIsInt) {
        if (n > 0.0 && evenIntValue(expn, nullptr) && nonnegIsh(base->a, 0)) {
            double k = n / 2.0;
            if (k == 1.0) return base->a;
            return makePowS(base->a, mkNum(k));
        }
        if (n < 0.0 && definitelyPositive(base->a)) {
            return makePowS(base->a, mkNum(n / 2.0));
        }
    }
    // |u|^n：偶次幂可去掉绝对值
    if (base->op == Op::Abs && expIsInt && n > 0.0 && evenIntValue(expn, nullptr)) {
        return makePowS(base->a, mkNum(n));
    }
    // (-u)^n：整数次幂
    if (base->op == Op::Neg && expIsInt) {
        ExprP inner = makePowS(base->a, expn);
        if (evenIntValue(expn, nullptr)) return inner;
        return makeNegS(inner);
    }
    // (cbrt(u))^3 = u
    if (base->op == Op::Cbrt && expIsInt && n == 3.0) return base->a;
    // (u^(2k))^0.5 = |u|^k
    if (base->op == Op::Pow && expIsNum && std::fabs(ev - 0.5) < 1e-12) {
        double m = 0.0;
        if (intValue(base->b, &m) && m > 0.0 && std::fmod(m, 2.0) == 0.0) {
            if (m == 2.0) return makeFunc1S(Op::Abs, base->a);
            return makePowS(makeFunc1S(Op::Abs, base->a), mkNum(m / 2.0));
        }
    }
    return mkPow(base, expn);
}

// ---- 一元函数 ----
ExprP makeFunc1S(Op op, const ExprP& a) {
    if (!a) return a;
    bool foldAlways = (op == Op::Floor || op == Op::Ceil || op == Op::Sign);
    if (isConst(a)) {
        ExprP node = mkFunc1(op, a);
        double v = 0.0;
        if (constRealValue(node, &v)) {
            if (foldAlways) return mkNum(std::round(v));
            if (nearInteger(v)) return mkNum(std::round(v));
        }
    }
    switch (op) {
        case Op::Sin:
            if (a->op == Op::Neg) return makeNegS(makeFunc1S(Op::Sin, a->a));
            break;
        case Op::Cos:
            if (a->op == Op::Neg) return makeFunc1S(Op::Cos, a->a);
            break;
        case Op::Tan:
            if (a->op == Op::Neg) return makeNegS(makeFunc1S(Op::Tan, a->a));
            break;
        case Op::Sinh:
            if (a->op == Op::Neg) return makeNegS(makeFunc1S(Op::Sinh, a->a));
            break;
        case Op::Cosh:
            if (a->op == Op::Neg) return makeFunc1S(Op::Cosh, a->a);
            break;
        case Op::Tanh:
            if (a->op == Op::Neg) return makeNegS(makeFunc1S(Op::Tanh, a->a));
            break;
        case Op::Asin:
        case Op::Atan:
        case Op::Asinh:
            if (a->op == Op::Neg) return makeNegS(makeFunc1S(op, a->a));
            break;
        case Op::Ln: {
            // ln(exp(u)) = u（定义域一致，恒成立）
            if (a->op == Op::Exp) return a->a;
            // ln(sqrt(u)) = ln(u)/2（需 u 恒正）
            if (a->op == Op::Sqrt && definitelyPositive(a->a)) {
                return makeMulS(mkNum(0.5), makeFunc1S(Op::Ln, a->a));
            }
            // ln(u^k) = k*ln(u)（需 u 恒正）
            double n = 0.0;
            if (a->op == Op::Pow && intValue(a->b, &n) && definitelyPositive(a->a)) {
                return makeMulS(a->b, makeFunc1S(Op::Ln, a->a));
            }
            break;
        }
        case Op::Exp:
            // exp(ln(u)) = u 仅在 u 恒正（定义域不扩大）时成立
            if (a->op == Op::Ln && definitelyPositive(a->a)) return a->a;
            break;
        case Op::Sqrt: {
            // sqrt(u^(2k)) = |u|^k（u 为偶数次幂时 u>=0，保域）
            double n = 0.0;
            if (a->op == Op::Pow && evenIntValue(a->b, &n) && n > 0.0) {
                if (n == 2.0) return makeFunc1S(Op::Abs, a->a);
                return makePowS(makeFunc1S(Op::Abs, a->a), mkNum(n / 2.0));
            }
            break;
        }
        case Op::Cbrt: {
            if (a->op == Op::Neg) return makeNegS(makeFunc1S(Op::Cbrt, a->a));
            double n = 0.0;
            if (a->op == Op::Pow && intValue(a->b, &n) && n == 3.0) return a->a;
            break;
        }
        case Op::Abs: {
            if (a->op == Op::Abs) return a;
            if (a->op == Op::Neg) return makeFunc1S(Op::Abs, a->a);
            if (nonnegIsh(a, 0)) return a;
            if (nonposIsh(a, 0)) return makeNegS(a);
            break;
        }
        case Op::Sign: {
            if (a->op == Op::Sign) return a;
            if (a->op == Op::Abs) {
                if (totalExpr(a->a) && definitelyNonzero(a->a)) return mkNum(1.0);
                break;
            }
            if (totalExpr(a)) {
                if (definitelyPositive(a)) return mkNum(1.0);
                if (signStrict(a, 0) < 0) return mkNum(-1.0);
            }
            break;
        }
        case Op::Floor:
        case Op::Ceil: {
            // floor(floor/ceil(u)) = 自身（整数已取整，保域）
            if (a->op == Op::Floor || a->op == Op::Ceil) return a;
            break;
        }
        case Op::Acos: {
            // acos(-u) = π - acos(u)（保域），仅在显式常量 pi 时使用
            break;
        }
        default:
            break;
    }
    return mkFunc1(op, a);
}

ExprP makeFunc2S(Op op, const ExprP& a, const ExprP& b) {
    if (!a || !b) return mkNum(0.0);
    if ((op == Op::Min || op == Op::Max || op == Op::Mod) && isConst(a) && isConst(b)) {
        ExprP node = mkFunc2(op, a, b);
        double v = 0.0;
        if (constRealValue(node, &v)) return mkNum(v);
    }
    if (op == Op::Min || op == Op::Max) {
        if (structurallyEqual(a, b) || keyOf(a, 0) == keyOf(b, 0)) return a;
        if (keyOf(a, 0) > keyOf(b, 0)) return mkFunc2(op, b, a);
        return mkFunc2(op, a, b);
    }
    if (op == Op::Mod) {
        if (isNum(a) && a->num == 0.0) {
            if (totalExpr(b) && definitelyNonzero(b)) return mkNum(0.0);
        }
    }
    return mkFunc2(op, a, b);
}

// ---------------- 单轮递归化简 ----------------
// 和式节点：先把整棵和式摊平成项（O(n)），逐项化简后再一次性合并，
// 避免左嵌套求和链在每一层重复摊平（O(n^2)）。
ExprP simplifySumNode(const ExprP& e, int depth) {
    std::vector<SumTerm> raw;
    collectSum(e, false, raw, 0);
    std::vector<SumTerm> flat;
    flat.reserve(raw.size());
    for (const auto& t : raw) {
        if (!t.node) continue;
        ExprP sn = simpRec(t.node, depth + 1);
        if (!sn) continue;
        collectSum(sn, t.neg, flat, 0);  // 化简结果本身可能又变成和式
    }
    return buildSum(flat);
}

// 乘积节点：同样先摊平再逐因子化简，最后一次性合并。
ExprP simplifyProductNode(const ExprP& e, int depth) {
    std::vector<MulFactor> raw;
    collectProduct(e, false, raw, 0);
    std::vector<MulFactor> flat;
    flat.reserve(raw.size());
    for (const auto& f : raw) {
        if (!f.node) continue;
        ExprP sn = simpRec(f.node, depth + 1);
        if (!sn) continue;
        collectProduct(sn, f.neg, flat, 0);  // 化简结果可能又变成乘积
    }
    return buildProductFromFactors(flat);
}

ExprP simpRec(const ExprP& e, int depth) {
    if (!e) return e;
    if (depth > kMaxDepth) return e;
    switch (e->op) {
        case Op::Num:
        case Op::Var:
        case Op::Pi:
        case Op::E:
        case Op::I:
            return e;
        case Op::Neg:
        case Op::Add:
        case Op::Sub:
            return simplifySumNode(e, depth);
        case Op::Mul:
            return simplifyProductNode(e, depth);
        case Op::Div:
            return makeDivS(simpRec(e->a, depth + 1), simpRec(e->b, depth + 1));
        case Op::Pow:
            return makePowS(simpRec(e->a, depth + 1), simpRec(e->b, depth + 1));
        case Op::Min:
        case Op::Max:
        case Op::Mod:
            return makeFunc2S(e->op, simpRec(e->a, depth + 1), simpRec(e->b, depth + 1));
        default:
            return makeFunc1S(e->op, e->a ? simpRec(e->a, depth + 1) : nullptr);
    }
}

ExprP simplifyImpl(const ExprP& e) {
    if (!e) return e;
    ExprP cur = e;
    for (int i = 0; i < kMaxIterations; ++i) {
        if (nodeCount(cur) > kMaxNodes) break;
        ExprP next = simpRec(cur, 0);
        if (structurallyEqual(cur, next)) return next;
        cur = next;
    }
    return cur;
}

// ---------------- 有理化（展示用，可选） ----------------
ExprP rawNode(Op op, const ExprP& a, const ExprP& b) {
    switch (op) {
        case Op::Neg:
            return mkNeg(a);
        case Op::Add:
            return mkAdd(a, b);
        case Op::Sub:
            return mkSub(a, b);
        case Op::Mul:
            return mkMul(a, b);
        case Op::Div:
            return mkDiv(a, b);
        case Op::Pow:
            return mkPow(a, b);
        default:
            break;
    }
    if (opArity(op) == 2) return mkFunc2(op, a, b);
    if (opArity(op) == 1) return mkFunc1(op, a);
    return mkNum(0.0);
}

// 拆分 a ± sqrt(u) 形式：成功时 rest 为另一部分，u 为根号内表达式
bool splitSqrtTerm(const ExprP& e, ExprP& u, ExprP& rest) {
    if (!e) return false;
    if (e->op == Op::Sqrt) {
        u = e->a;
        rest = nullptr;
        return true;
    }
    if (e->op == Op::Neg && e->a && e->a->op == Op::Sqrt) {
        u = e->a->a;
        rest = nullptr;
        return true;
    }
    if (e->op == Op::Add || e->op == Op::Sub) {
        if (e->b && (e->b->op == Op::Sqrt || (e->b->op == Op::Neg && e->b->a->op == Op::Sqrt))) {
            u = (e->b->op == Op::Sqrt) ? e->b->a : e->b->a->a;
            rest = e->a;
            return true;
        }
        if (e->a && e->a->op == Op::Sqrt) {
            u = e->a->a;
            rest = (e->op == Op::Sub) ? rawNode(Op::Neg, e->b, nullptr) : e->b;
            return true;
        }
        if (e->a && e->a->op == Op::Neg && e->a->a->op == Op::Sqrt) {
            u = e->a->a->a;
            rest = e->b;
            return true;
        }
    }
    return false;
}

ExprP tryRationalizeDiv(const ExprP& num, const ExprP& den) {
    if (!num || !den) return nullptr;
    if (den->op == Op::Sqrt) {
        // a/sqrt(u) = a*sqrt(u)/u
        return mkDiv(mkMul(num, den), den->a);
    }
    if (den->op == Op::Mul) {
        std::vector<MulFactor> fs;
        flattenMulFactors(den, false, fs, 0);
        int sqrtIdx = -1;
        for (size_t i = 0; i < fs.size(); ++i) {
            if (!fs[i].neg && fs[i].node && fs[i].node->op == Op::Sqrt) {
                sqrtIdx = static_cast<int>(i);
                break;
            }
        }
        if (sqrtIdx >= 0) {
            ExprP sq = fs[sqrtIdx].node;
            std::vector<MulFactor> others;
            for (size_t i = 0; i < fs.size(); ++i) {
                if (static_cast<int>(i) != sqrtIdx) others.push_back(fs[i]);
            }
            ExprP newDen = mkMul(rebuildProduct(others), sq->a);
            return mkDiv(mkMul(num, sq), newDen);
        }
        return nullptr;
    }
    ExprP u;
    ExprP rest;
    if (splitSqrtTerm(den, u, rest) && rest) {
        // 乘以共轭：a/(c±sqrt(u)) = a*(c∓sqrt(u))/(c^2-u)
        ExprP conj = (den->op == Op::Sub) ? mkAdd(rest, mkFunc1(Op::Sqrt, u))
                                          : mkSub(rest, mkFunc1(Op::Sqrt, u));
        return mkDiv(mkMul(num, conj), mkSub(mkPow(rest, mkNum(2.0)), u));
    }
    return nullptr;
}

ExprP rationalizeRec(const ExprP& e, int depth) {
    if (!e || depth > kMaxDepth) return e;
    if (!e->a && !e->b) return e;
    ExprP a = e->a ? rationalizeRec(e->a, depth + 1) : nullptr;
    ExprP b = e->b ? rationalizeRec(e->b, depth + 1) : nullptr;
    ExprP r = rawNode(e->op, a, b);
    if (r && r->op == Op::Div && nodeCount(r) <= 200) {
        ExprP fixed = tryRationalizeDiv(r->a, r->b);
        if (fixed) return fixed;
    }
    return r;
}

}  // namespace

// =======================================================================
// 公共接口
// =======================================================================

ExprP simplify(const ExprP& e) { return simplifyImpl(e); }

ExprP simplifyOnce(const ExprP& e) { return simpRec(e, 0); }

bool isZeroSimplified(const ExprP& e) { return isZero(simplify(e)); }

bool symbolicallyEqual(const ExprP& x, const ExprP& y) {
    if (!x || !y) return false;
    if (structurallyEqual(x, y)) return true;
    ExprP diff = simplify(mkSub(x, y));
    if (isNum(diff) && diff->num == 0.0) return true;
    // 符号化简失败：多随机点数值采样兜底
    return numericallyEqual(x, y, {});
}

std::string canonicalKey(const ExprP& e) { return keyOf(e, 0); }

bool numericallyEqual(const ExprP& x, const ExprP& y, const std::vector<std::string>& vars,
                      double tol) {
    if (!x || !y) return false;
    if (structurallyEqual(x, y)) return true;
    std::vector<std::string> vs = vars;
    if (vs.empty()) {
        std::set<std::string> s = collectVars(x);
        std::set<std::string> s2 = collectVars(y);
        s.insert(s2.begin(), s2.end());
        vs.assign(s.begin(), s.end());
    }
    if (vs.empty()) {
        VarMap m;
        auto rx = evaluate(x, m);
        auto ry = evaluate(y, m);
        if (!rx.ok || !ry.ok) return false;
        double dr = std::fabs(rx.value.re - ry.value.re);
        double di = std::fabs(rx.value.im - ry.value.im);
        double scale = 1.0 + rx.value.abs() + ry.value.abs();
        return dr + di <= tol * scale;
    }
    if (vs.size() > 4) vs.resize(4);

    std::mt19937 rng(0x5EEDF00Du);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    const double scales[5] = {1.0, 0.3, 3.0, 0.07, 7.0};
    const int trials = 160;
    int valid = 0;
    for (int t = 0; t < trials; ++t) {
        VarMap m;
        for (size_t i = 0; i < vs.size(); ++i) {
            double s = scales[(t + static_cast<int>(i)) % 5];
            double val = uni(rng) * s;
            if (std::fabs(val) < 1e-6) val = 0.31 * s;  // 避开精确零点/极点
            m[vs[i]] = Cplx(val);
        }
        auto rx = evaluate(x, m);
        auto ry = evaluate(y, m);
        if (!rx.ok || !ry.ok) continue;
        if (std::fabs(rx.value.im) > 1e-9 || std::fabs(ry.value.im) > 1e-9) continue;
        double vx = rx.value.re;
        double vy = ry.value.re;
        // 极点附近跳过
        if (std::fabs(vx) > 1e6 || std::fabs(vy) > 1e6) continue;
        double scale = 1.0 + std::fabs(vx) + std::fabs(vy);
        if (std::fabs(vx - vy) > tol * scale) return false;
        ++valid;
    }
    return valid >= 12;
}

ExprP rationalize(const ExprP& e) {
    if (!e) return e;
    return simplifyImpl(rationalizeRec(e, 0));
}

}  // namespace fcg
