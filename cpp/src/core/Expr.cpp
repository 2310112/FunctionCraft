// Expr.cpp — AST 实现：打印、哈希、结构比较、遍历
#include "Expr.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <functional>
#include <sstream>

namespace fcg {

namespace {

struct OpInfo {
    const char* name;
    const char* display;
    int arity;
    bool analytic;
    bool elementary;
};

// 顺序必须与 Op 枚举一致
const OpInfo kOpInfo[kOpCount] = {
    {"num", "num", 0, true, false},      // Num
    {"var", "var", 0, true, false},      // Var
    {"neg", "-", 1, true, true},         // Neg
    {"add", "+", 2, true, true},         // Add
    {"sub", "-", 2, true, true},         // Sub
    {"mul", "*", 2, true, true},         // Mul
    {"div", "/", 2, true, true},         // Div
    {"pow", "^", 2, true, true},         // Pow
    {"mod", "mod", 2, false, true},      // Mod
    {"sin", "sin", 1, true, true},       // Sin
    {"cos", "cos", 1, true, true},       // Cos
    {"tan", "tan", 1, true, true},       // Tan
    {"exp", "exp", 1, true, true},       // Exp
    {"ln", "ln", 1, true, true},         // Ln
    {"log2", "log2", 1, true, true},     // Log2
    {"log10", "log10", 1, true, true},   // Log10
    {"sqrt", "sqrt", 1, true, true},     // Sqrt
    {"cbrt", "cbrt", 1, true, true},     // Cbrt
    {"abs", "abs", 1, false, true},      // Abs
    {"asin", "asin", 1, true, true},     // Asin
    {"acos", "acos", 1, true, true},     // Acos
    {"atan", "atan", 1, true, true},     // Atan
    {"sinh", "sinh", 1, true, true},     // Sinh
    {"cosh", "cosh", 1, true, true},     // Cosh
    {"tanh", "tanh", 1, true, true},     // Tanh
    {"asinh", "asinh", 1, true, true},   // Asinh
    {"acosh", "acosh", 1, true, true},   // Acosh
    {"atanh", "atanh", 1, true, true},   // Atanh
    {"floor", "floor", 1, false, true},  // Floor
    {"ceil", "ceil", 1, false, true},    // Ceil
    {"sign", "sign", 1, false, true},    // Sign
    {"min", "min", 2, false, true},      // Min
    {"max", "max", 2, false, true},      // Max
    {"pi", "pi", 0, true, false},        // Pi
    {"e", "e", 0, true, false},          // E
    {"i", "i", 0, true, false},          // I
};

constexpr int kPrecAdd = 1;
constexpr int kPrecMul = 2;
constexpr int kPrecUnary = 3;
constexpr int kPrecPow = 4;
constexpr int kPrecAtom = 10;

// 打印辅助：数字格式化
std::string numToString(double v) {
    if (!std::isfinite(v)) return v > 0 ? "inf" : (v < 0 ? "-inf" : "nan");
    if (v == 0.0) return "0";
    double r = std::round(v);
    if (std::fabs(v - r) < 1e-12 && std::fabs(r) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", r);
        return buf;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.12g", v);
    return buf;
}

int precOf(Op op) {
    switch (op) {
        case Op::Add:
        case Op::Sub:
            return kPrecAdd;
        case Op::Mul:
        case Op::Div:
            return kPrecMul;
        case Op::Neg:
            return kPrecUnary;
        case Op::Pow:
            return kPrecPow;
        default:
            return kPrecAtom;
    }
}

// 判断一个节点是否是"原子"（渲染时不需要括号）
bool isAtomicForPrint(Op op) {
    switch (op) {
        case Op::Num:
        case Op::Var:
        case Op::Pi:
        case Op::E:
        case Op::I:
            return true;
        default:
            return opArity(op) == 1 && op != Op::Neg;  // 函数调用形式自带括号
    }
}

std::string renderPtr(const Expr* node, int minPrec);

// 子节点在给定上下文中的最小优先级要求
bool needsParen(const Expr* child, int minPrec) {
    if (!child) return false;
    // 负项：仅在"右操作数 / 幂底数"上下文加括号（-a·b、-a+b 无需括号）
    if (child->op == Op::Neg) return minPrec >= kPrecMul + 1;
    return precOf(child->op) < minPrec;
}

std::string renderChild(const Expr* child, int minPrec) {
    if (!child) return "?";
    std::string s = renderPtr(child, minPrec);
    if (needsParen(child, minPrec)) return "(" + s + ")";
    return s;
}

std::string renderPtr(const Expr* e, int minPrec) {
    if (!e) return "?";
    switch (e->op) {
        case Op::Num:
            return numToString(e->num);
        case Op::Var:
            return e->var;
        case Op::Pi:
            return "π";
        case Op::E:
            return "e";
        case Op::I:
            return "i";
        case Op::Neg: {
            // 一元负号优先级高于 + - ，低于幂与函数
            return "-" + renderChild(e->a.get(), kPrecUnary);
        }
        case Op::Add: {
            std::string l = renderChild(e->a.get(), kPrecAdd);
            // a + (-b) 打印成 a - b
            if (e->b && e->b->op == Op::Neg)
                return l + " - " + renderChild(e->b->a.get(), kPrecMul);
            return l + " + " + renderChild(e->b.get(), kPrecAdd + 1);
        }
        case Op::Sub: {
            std::string l = renderChild(e->a.get(), kPrecAdd);
            // a - (-b) 打印成 a + b
            if (e->b && e->b->op == Op::Neg)
                return l + " + " + renderChild(e->b->a.get(), kPrecMul);
            return l + " - " + renderChild(e->b.get(), kPrecAdd + 1);
        }
        case Op::Mul: {
            std::string l = renderChild(e->a.get(), kPrecMul);
            std::string r = renderChild(e->b.get(), kPrecMul + 1);
            // 数字后面跟非数字原子时省略乘号：2x、2π、2sin(x)、2(x+1)
            if (e->a && e->a->op == Op::Num && e->b && e->b->op != Op::Num) return l + r;
            return l + "·" + r;
        }
        case Op::Div: {
            std::string l = renderChild(e->a.get(), kPrecMul);
            std::string r = renderChild(e->b.get(), kPrecMul + 1);
            return l + "/" + r;
        }
        case Op::Pow: {
            std::string l = renderChild(e->a.get(), kPrecPow + 1);
            std::string r = renderChild(e->b.get(), kPrecPow);
            return l + "^" + r;
        }
        default: {
            if (opArity(e->op) == 2) {
                std::string l = renderChild(e->a.get(), 0);
                std::string r = renderChild(e->b.get(), 0);
                if (e->op == Op::Min) return "min(" + l + ", " + r + ")";
                if (e->op == Op::Max) return "max(" + l + ", " + r + ")";
                if (e->op == Op::Mod) return "mod(" + l + ", " + r + ")";
                return std::string(opName(e->op)) + "(" + l + ", " + r + ")";
            }
            if (opArity(e->op) == 1) {
                std::string inner = renderChild(e->a.get(), 0);
                if (e->op == Op::Sqrt) return "√(" + inner + ")";
                return std::string(opName(e->op)) + "(" + inner + ")";
            }
            return std::string(opName(e->op));
        }
    }
}

}  // namespace

// ---------------------------------------------------------------
// 元信息
// ---------------------------------------------------------------
const char* opName(Op op) {
    int i = static_cast<int>(op);
    if (i < 0 || i >= kOpCount) return "?";
    return kOpInfo[i].name;
}

const char* opDisplay(Op op) {
    int i = static_cast<int>(op);
    if (i < 0 || i >= kOpCount) return "?";
    return kOpInfo[i].display;
}

int opArity(Op op) {
    int i = static_cast<int>(op);
    if (i < 0 || i >= kOpCount) return 0;
    return kOpInfo[i].arity;
}

bool opIsUnaryFunc(Op op) { return opArity(op) == 1 && op != Op::Neg; }
bool opIsBinaryFunc(Op op) {
    return opArity(op) == 2 && op != Op::Add && op != Op::Sub && op != Op::Mul &&
           op != Op::Div && op != Op::Pow;
}

bool opIsAnalytic(Op op) {
    int i = static_cast<int>(op);
    if (i < 0 || i >= kOpCount) return false;
    return kOpInfo[i].analytic;
}

bool opIsElementary(Op op) {
    int i = static_cast<int>(op);
    if (i < 0 || i >= kOpCount) return false;
    return kOpInfo[i].elementary;
}

std::string Expr::toStr() const {
    // 最外层不额外加括号
    return renderPtr(this, 0);
}

// ---------------------------------------------------------------
// 工厂
// ---------------------------------------------------------------
namespace {
ExprP makeNode(Op op) {
    auto e = std::make_shared<Expr>();
    e->op = op;
    return e;
}
}  // namespace

ExprP mkNum(double v) {
    auto e = std::make_shared<Expr>();
    e->op = Op::Num;
    e->num = v;
    return e;
}

ExprP mkVar(std::string name) {
    auto e = std::make_shared<Expr>();
    e->op = Op::Var;
    e->var = std::move(name);
    return e;
}

ExprP mkPi() {
    auto e = std::make_shared<Expr>();
    e->op = Op::Pi;
    return e;
}

ExprP mkE() {
    auto e = std::make_shared<Expr>();
    e->op = Op::E;
    return e;
}

ExprP mkI() {
    auto e = std::make_shared<Expr>();
    e->op = Op::I;
    return e;
}

ExprP mkNeg(ExprP a) {
    auto e = std::make_shared<Expr>();
    e->op = Op::Neg;
    e->a = std::move(a);
    return e;
}

#define FCG_BINOP(NAME, OPCODE)                    \
    ExprP NAME(ExprP a, ExprP b) {                 \
        auto e = std::make_shared<Expr>();          \
        e->op = OPCODE;                             \
        e->a = std::move(a);                        \
        e->b = std::move(b);                        \
        return e;                                   \
    }

FCG_BINOP(mkAdd, Op::Add)
FCG_BINOP(mkSub, Op::Sub)
FCG_BINOP(mkMul, Op::Mul)
FCG_BINOP(mkDiv, Op::Div)
FCG_BINOP(mkPow, Op::Pow)
#undef FCG_BINOP

ExprP mkFunc1(Op op, ExprP a) {
    auto e = std::make_shared<Expr>();
    e->op = op;
    e->a = std::move(a);
    return e;
}

ExprP mkFunc2(Op op, ExprP a, ExprP b) {
    auto e = std::make_shared<Expr>();
    e->op = op;
    e->a = std::move(a);
    e->b = std::move(b);
    return e;
}

ExprP mkAddAll(const std::vector<ExprP>& terms) {
    ExprP acc;
    for (const auto& t : terms) {
        if (!t) continue;
        if (isZero(t)) continue;
        acc = acc ? mkAdd(acc, t) : t;
    }
    return acc ? acc : mkNum(0.0);
}

ExprP mkMulAll(const std::vector<ExprP>& factors) {
    ExprP acc;
    for (const auto& f : factors) {
        if (!f) continue;
        if (isOne(f)) continue;
        if (isZero(f)) return mkNum(0.0);
        acc = acc ? mkMul(acc, f) : f;
    }
    return acc ? acc : mkNum(1.0);
}

// ---------------------------------------------------------------
// 检查
// ---------------------------------------------------------------
bool isNum(const ExprP& e) { return e && e->op == Op::Num; }

bool isNum(const ExprP& e, double v, double tol) {
    return isNum(e) && std::fabs(e->num - v) <= tol;
}

bool isZero(const ExprP& e) { return isNum(e) && e->num == 0.0; }
bool isOne(const ExprP& e) { return isNum(e) && e->num == 1.0; }

bool isVar(const ExprP& e) { return e && e->op == Op::Var; }

bool isVar(const ExprP& e, const std::string& name) {
    return isVar(e) && e->var == name;
}

bool isConst(const ExprP& e) {
    if (!e) return true;
    switch (e->op) {
        case Op::Num:
        case Op::Pi:
        case Op::E:
        case Op::I:
            return true;
        case Op::Var:
            return false;
        default:
            break;
    }
    if (e->a && !isConst(e->a)) return false;
    if (e->b && !isConst(e->b)) return false;
    return true;
}

bool hasVar(const ExprP& e) { return !isConst(e); }

double asNum(const ExprP& e) { return isNum(e) ? e->num : 0.0; }

// ---------------------------------------------------------------
// 结构工具
// ---------------------------------------------------------------
uint64_t hashOf(const ExprP& e) {
    if (!e) return 0;
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    mix(static_cast<uint64_t>(e->op) + 1);
    if (e->op == Op::Num) {
        uint64_t bits = 0;
        double v = e->num;
        if (v == 0.0) v = 0.0;  // 归一 -0
        std::memcpy(&bits, &v, sizeof(bits));
        mix(bits);
    } else if (e->op == Op::Var) {
        for (unsigned char c : e->var) mix(c);
    }
    if (e->a) mix(hashOf(e->a));
    if (e->b) mix(hashOf(e->b));
    return h;
}

bool structurallyEqual(const ExprP& x, const ExprP& y) {
    if (x == y) return true;
    if (!x || !y) return false;
    if (x->op != y->op) return false;
    if (x->op == Op::Num) {
        if (x->num != y->num) return false;
    } else if (x->op == Op::Var) {
        if (x->var != y->var) return false;
    }
    if (!structurallyEqual(x->a, y->a)) return false;
    if (!structurallyEqual(x->b, y->b)) return false;
    return true;
}

int nodeCount(const ExprP& e) {
    if (!e) return 0;
    return 1 + nodeCount(e->a) + nodeCount(e->b);
}

int funcCallCount(const ExprP& e) {
    if (!e) return 0;
    int n = 0;
    if (opIsUnaryFunc(e->op) || opIsBinaryFunc(e->op)) n = 1;
    return n + funcCallCount(e->a) + funcCallCount(e->b);
}

int maxDepth(const ExprP& e) {
    if (!e) return 0;
    return 1 + std::max(maxDepth(e->a), maxDepth(e->b));
}

void collectVarsImpl(const ExprP& e, std::set<std::string>& out) {
    if (!e) return;
    if (e->op == Op::Var) out.insert(e->var);
    collectVarsImpl(e->a, out);
    collectVarsImpl(e->b, out);
}

std::set<std::string> collectVars(const ExprP& e) {
    std::set<std::string> out;
    collectVarsImpl(e, out);
    return out;
}

ExprP substitute(const ExprP& e, const std::string& var, const ExprP& repl) {
    if (!e) return e;
    if (e->op == Op::Var) {
        if (e->var == var) return repl;
        return e;
    }
    if (!e->a && !e->b) return e;
    ExprP na = e->a ? substitute(e->a, var, repl) : nullptr;
    ExprP nb = e->b ? substitute(e->b, var, repl) : nullptr;
    if (na == e->a && nb == e->b) return e;
    auto n = std::make_shared<Expr>(*e);
    n->a = na;
    n->b = nb;
    return n;
}

void visit(const ExprP& e, const std::function<void(const ExprP&)>& fn) {
    if (!e) return;
    fn(e);
    if (e->a) visit(e->a, fn);
    if (e->b) visit(e->b, fn);
}

}  // namespace fcg
