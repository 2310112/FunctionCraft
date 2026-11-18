// Diff.cpp — 符号求导（由 engine-simplify 实现）
//
// 覆盖全部 Op：
//   - 常量 / 变量 / + - * / / Pow（幂法则 + 一般幂指函数）
//   - 三角、反三角、双曲、反双曲、exp/ln/log2/log10/sqrt/cbrt
//   - 非解析函数给出"广义导数"（几乎处处）：
//       abs(x)'      = sign(x) * x'
//       sign/floor/ceil 的导数 = 0（几乎处处）
//       min(a,b)' / max(a,b)' 用 (a±b 与 |a-b|) 的等价形式求导
//       mod(a,b)'    = a' - floor(a/b) * b'（几乎处处）
// -----------------------------------------------------------------------

#include "Diff.h"

#include <cmath>
#include <string>

#include "Simplify.h"

namespace fcg {
namespace {

constexpr int kMaxDiffDepth = 300;  // 递归深度保护（防止病态输入爆栈）

ExprP D(const ExprP& f, const std::string& var, int depth);

ExprP dNum(double v) { return mkNum(v); }

// 一阶导的 raw 构造（不做化简）
ExprP D(const ExprP& f, const std::string& var, int depth) {
    if (!f) return dNum(0.0);
    if (depth > kMaxDiffDepth) return dNum(0.0);
    const int d = depth + 1;
    switch (f->op) {
        case Op::Num:
        case Op::Pi:
        case Op::E:
        case Op::I:
            return dNum(0.0);
        case Op::Var:
            return dNum(f->var == var ? 1.0 : 0.0);
        case Op::Neg:
            return mkNeg(D(f->a, var, d));
        case Op::Add:
            return mkAdd(D(f->a, var, d), D(f->b, var, d));
        case Op::Sub:
            return mkSub(D(f->a, var, d), D(f->b, var, d));
        case Op::Mul: {
            ExprP da = D(f->a, var, d);
            ExprP db = D(f->b, var, d);
            // da*b + a*db
            return mkAdd(mkMul(da, f->b), mkMul(f->a, db));
        }
        case Op::Div: {
            ExprP da = D(f->a, var, d);
            ExprP db = D(f->b, var, d);
            // (da*b - a*db) / b^2
            return mkDiv(mkSub(mkMul(da, f->b), mkMul(f->a, db)),
                         mkPow(f->b, mkNum(2.0)));
        }
        case Op::Pow: {
            ExprP da = D(f->a, var, d);
            ExprP db = D(f->b, var, d);
            bool expHasVar = hasVar(f->b);
            bool baseHasVar = hasVar(f->a);
            if (!expHasVar) {
                // d/dx a^n = n * a^(n-1) * a'
                if (isNum(f->b) && f->b->num == 0.0) return dNum(0.0);
                ExprP nMinus1;
                if (isNum(f->b)) {
                    nMinus1 = mkNum(f->b->num - 1.0);
                } else {
                    nMinus1 = mkSub(f->b, mkNum(1.0));
                }
                return mkMul(mkMul(f->b, mkPow(f->a, nMinus1)), da);
            }
            if (!baseHasVar) {
                // d/dx c^u = c^u * ln(c) * u'
                return mkMul(mkMul(mkPow(f->a, f->b), mkFunc1(Op::Ln, f->a)), db);
            }
            // 一般幂指函数：f = a^b * (b'*ln(a) + b*a'/a)
            ExprP term = mkAdd(mkMul(db, mkFunc1(Op::Ln, f->a)),
                               mkDiv(mkMul(f->b, da), f->a));
            return mkMul(mkPow(f->a, f->b), term);
        }
        case Op::Mod: {
            // fmod(a,b) = a - b*floor(a/b)，几乎处处：a' - floor(a/b)*b'
            ExprP da = D(f->a, var, d);
            ExprP db = D(f->b, var, d);
            return mkSub(da, mkMul(mkFunc1(Op::Floor, mkDiv(f->a, f->b)), db));
        }
        case Op::Sin: {
            ExprP da = D(f->a, var, d);
            return mkMul(mkFunc1(Op::Cos, f->a), da);
        }
        case Op::Cos: {
            ExprP da = D(f->a, var, d);
            return mkMul(mkNeg(mkFunc1(Op::Sin, f->a)), da);
        }
        case Op::Tan: {
            ExprP da = D(f->a, var, d);
            ExprP c = mkFunc1(Op::Cos, f->a);
            return mkDiv(da, mkMul(c, c));
        }
        case Op::Exp: {
            ExprP da = D(f->a, var, d);
            return mkMul(mkFunc1(Op::Exp, f->a), da);
        }
        case Op::Ln: {
            ExprP da = D(f->a, var, d);
            return mkDiv(da, f->a);
        }
        case Op::Log2: {
            ExprP da = D(f->a, var, d);
            return mkDiv(da, mkMul(f->a, mkNum(std::log(2.0))));
        }
        case Op::Log10: {
            ExprP da = D(f->a, var, d);
            return mkDiv(da, mkMul(f->a, mkNum(std::log(10.0))));
        }
        case Op::Sqrt: {
            ExprP da = D(f->a, var, d);
            return mkDiv(da, mkMul(mkNum(2.0), mkFunc1(Op::Sqrt, f->a)));
        }
        case Op::Cbrt: {
            ExprP da = D(f->a, var, d);
            ExprP c = mkFunc1(Op::Cbrt, f->a);
            return mkDiv(da, mkMul(mkNum(3.0), mkMul(c, c)));
        }
        case Op::Abs: {
            ExprP da = D(f->a, var, d);
            return mkMul(mkFunc1(Op::Sign, f->a), da);
        }
        case Op::Asin: {
            ExprP da = D(f->a, var, d);
            ExprP inner = mkFunc1(Op::Sqrt, mkSub(mkNum(1.0), mkPow(f->a, mkNum(2.0))));
            return mkDiv(da, inner);
        }
        case Op::Acos: {
            ExprP da = D(f->a, var, d);
            ExprP inner = mkFunc1(Op::Sqrt, mkSub(mkNum(1.0), mkPow(f->a, mkNum(2.0))));
            return mkDiv(mkNeg(da), inner);
        }
        case Op::Atan: {
            ExprP da = D(f->a, var, d);
            return mkDiv(da, mkAdd(mkNum(1.0), mkPow(f->a, mkNum(2.0))));
        }
        case Op::Sinh: {
            ExprP da = D(f->a, var, d);
            return mkMul(mkFunc1(Op::Cosh, f->a), da);
        }
        case Op::Cosh: {
            ExprP da = D(f->a, var, d);
            return mkMul(mkFunc1(Op::Sinh, f->a), da);
        }
        case Op::Tanh: {
            ExprP da = D(f->a, var, d);
            ExprP c = mkFunc1(Op::Cosh, f->a);
            return mkDiv(da, mkMul(c, c));
        }
        case Op::Asinh: {
            ExprP da = D(f->a, var, d);
            ExprP inner = mkFunc1(Op::Sqrt, mkAdd(mkPow(f->a, mkNum(2.0)), mkNum(1.0)));
            return mkDiv(da, inner);
        }
        case Op::Acosh: {
            ExprP da = D(f->a, var, d);
            ExprP inner = mkFunc1(Op::Sqrt, mkSub(mkPow(f->a, mkNum(2.0)), mkNum(1.0)));
            return mkDiv(da, inner);
        }
        case Op::Atanh: {
            ExprP da = D(f->a, var, d);
            return mkDiv(da, mkSub(mkNum(1.0), mkPow(f->a, mkNum(2.0))));
        }
        case Op::Floor:
        case Op::Ceil:
        case Op::Sign:
            return dNum(0.0);  // 几乎处处为 0
        case Op::Min:
        case Op::Max: {
            // min(a,b) = (a+b-|a-b|)/2, max(a,b) = (a+b+|a-b|)/2
            ExprP da = D(f->a, var, d);
            ExprP db = D(f->b, var, d);
            ExprP s = mkFunc1(Op::Sign, mkSub(f->a, f->b));
            ExprP sum = mkAdd(da, db);
            ExprP diff = mkSub(da, db);
            ExprP inner = (f->op == Op::Min) ? mkSub(sum, mkMul(s, diff))
                                             : mkAdd(sum, mkMul(s, diff));
            return mkMul(mkNum(0.5), inner);
        }
        default:
            return dNum(0.0);
    }
}

}  // namespace

ExprP diffRaw(const ExprP& f, const std::string& var) { return D(f, var, 0); }

ExprP diff(const ExprP& f, const std::string& var) {
    if (!f) return mkNum(0.0);
    ExprP raw = diffRaw(f, var);
    return simplify(raw);
}

ExprP diffN(const ExprP& f, const std::string& var, int n) {
    if (!f) return mkNum(0.0);
    if (n <= 0) return f;
    ExprP cur = f;
    for (int i = 0; i < n; ++i) {
        cur = diff(cur, var);
        if (isNum(cur) && cur->num == 0.0) return cur;
        if (nodeCount(cur) > 100000) break;  // 病态输入保护
    }
    return cur;
}

}  // namespace fcg
