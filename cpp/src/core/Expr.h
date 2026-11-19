// Expr.h — 符号表达式树（核心数据类型）
// 设计原则：变量类型无关（实数/复数/多变量共用同一套 AST）
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fcg {

// ---------------------------------------------------------------
// 运算符/函数枚举
// ---------------------------------------------------------------
enum class Op : uint8_t {
    Num = 0,  // 数值常量（num 字段）
    Var,      // 变量（var 字段）

    Neg,      // -a

    Add, Sub, Mul, Div, Pow,  // 二元运算
    Mod,                      // a mod b（非解析）

    Sin, Cos, Tan,
    Exp, Ln, Log2, Log10,
    Sqrt, Cbrt, Abs,
    Asin, Acos, Atan,
    Sinh, Cosh, Tanh, Asinh, Acosh, Atanh,

    Floor, Ceil, Sign,  // 非解析
    Min, Max,           // 非解析（二元）

    // ----- 复数域专用（均非全纯，用于打破解析性 / 取分量） -----
    Re,    // Re(z)
    Im,    // Im(z)
    Conj,  // z 的共轭
    Arg,   // 辐角 arg(z)

    Pi, E, I,  // 常量 π, e, i

    COUNT
};

constexpr int kOpCount = static_cast<int>(Op::COUNT);

const char* opName(Op op);     // 内部名 "sin"/"log"/"add"
const char* opDisplay(Op op);  // 显示名 "sin"/"ln"/"√"
int opArity(Op op);            // 0=常量, 1=一元, 2=二元
bool opIsUnaryFunc(Op op);     // sin, cos, ...
bool opIsBinaryFunc(Op op);    // min, max, mod
bool opIsAnalytic(Op op);      // 在其定义域内解析（排除 abs/floor/ceil/sign/min/max/mod）
bool opIsElementary(Op op);    // 属于初等函数库（游戏可选项）

// ---------------------------------------------------------------
// 表达式节点（不可变，共享指针）
// ---------------------------------------------------------------
struct Expr;
using ExprP = std::shared_ptr<const Expr>;

struct Expr {
    Op op = Op::Num;
    double num = 0.0;  // Op::Num
    std::string var;   // Op::Var
    ExprP a;           // 第一操作数
    ExprP b;           // 第二操作数

    int arity() const { return opArity(op); }
    std::string str() const { return toStr(); }
    std::string toStr() const;
};

// ---------------------------------------------------------------
// 构造工厂
// ---------------------------------------------------------------
ExprP mkNum(double v);
ExprP mkVar(std::string name);
ExprP mkPi();
ExprP mkE();
ExprP mkI();

ExprP mkNeg(ExprP a);
ExprP mkAdd(ExprP a, ExprP b);
ExprP mkSub(ExprP a, ExprP b);
ExprP mkMul(ExprP a, ExprP b);
ExprP mkDiv(ExprP a, ExprP b);
ExprP mkPow(ExprP a, ExprP b);
ExprP mkFunc1(Op op, ExprP a);
ExprP mkFunc2(Op op, ExprP a, ExprP b);

// 把若干项相加 / 相乘（自动跳过 0/1）
ExprP mkAddAll(const std::vector<ExprP>& terms);
ExprP mkMulAll(const std::vector<ExprP>& factors);

// ---------------------------------------------------------------
// 检查工具
// ---------------------------------------------------------------
bool isNum(const ExprP& e);
bool isNum(const ExprP& e, double v, double tol = 1e-12);
bool isZero(const ExprP& e);   // 结构上是数值 0
bool isOne(const ExprP& e);
bool isVar(const ExprP& e);    // 是变量
bool isVar(const ExprP& e, const std::string& name);
bool isConst(const ExprP& e);  // 不含变量
bool hasVar(const ExprP& e);   // 含有变量
double asNum(const ExprP& e);  // 非 Num 节点时为 0

// ---------------------------------------------------------------
// 结构工具
// ---------------------------------------------------------------
uint64_t hashOf(const ExprP& e);
bool structurallyEqual(const ExprP& x, const ExprP& y);
int nodeCount(const ExprP& e);
int funcCallCount(const ExprP& e);  // 函数调用次数（sin/cos/... 计 1，运算符不计）
int maxDepth(const ExprP& e);
std::set<std::string> collectVars(const ExprP& e);

// 用变量表替换（多变量/改名用）
ExprP substitute(const ExprP& e, const std::string& var, const ExprP& repl);

// 遍历：对每个节点调用 fn（前序）
void visit(const ExprP& e, const std::function<void(const ExprP&)>& fn);

}  // namespace fcg
