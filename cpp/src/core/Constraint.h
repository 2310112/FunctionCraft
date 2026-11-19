// Constraint.h — 约束模型与验证器
#pragma once

#include <string>
#include <vector>

#include "Complex.h"
#include "Domain.h"
#include "Expr.h"

namespace fcg {

enum class ConstraintKind {
    AllowedFunctions,  // 只能使用给定函数
    DomainOn,          // 在 [a,b] 上有定义（实数域）
    AnalyticOn,        // 在 (a,b) 上解析
    BoundaryValue,     // f(point) = value
    RangeOn,           // 在 [a,b] 上 f(x) ∈ [lo,hi]
    PositiveOn,        // 在 (a,b) 上 f(x) > 0
    Monotonic,         // 在 [a,b] 上单调
    Parity,            // 偶函数 / 奇函数
    Periodic,          // 周期 T
    Asymptote,         // x→±∞ 水平渐近线 y = value
    Extremum,          // 在 point 处取极大/极小值
    SymmetryAxis,      // 关于 x = axis 对称
    ZeroCount,         // 在 [a,b] 内零点个数 = count
    ComplexityLimit,   // 函数调用数 <= maxFuncCalls
    PeriodicSymmetry,  // 关于点对称（可选扩展）

    // ---------- 复变函数专用 ----------
    HolomorphicOn,       // 在复平面区域上解析（全纯）
    NoSingularityIn,     // 区域内无奇点
    PoleOrderAt,         // 在 z0 处是 order 阶极点
    ResidueAt,           // 在 z0 处的留数 = cvalue
    ContourIntegralValue,// ∮_{|z-c|=r} f dz = cvalue
    ZeroCountInRegion,   // 区域内零点个数 = count
    ModulusRangeOn,      // 区域内 |f(z)| ∈ [lo, hi]
    ComplexBoundaryValue,// f(z0) = w0（复数点/值）
};

const char* constraintKindName(ConstraintKind k);
const char* constraintKindDisplay(ConstraintKind k);  // 中文
bool constraintKindSupported(ConstraintKind k);

struct Constraint {
    ConstraintKind kind = ConstraintKind::DomainOn;
    std::string var = "x";  // 作用变量

    // 通用区间/点
    Interval interval = Interval::all();
    double point = 0.0;
    double value = 0.0;
    double lo = 0.0;
    double hi = 0.0;

    // 形态参数
    bool increasing = true;  // Monotonic
    bool strict = false;     // Monotonic: 严格单调
    bool even = true;        // Parity
    int asymptoteSign = 0;   // 0=双侧, +1=+∞, -1=-∞
    bool isMax = true;       // Extremum
    int count = 0;           // ZeroCount
    int maxFuncCalls = 0;    // ComplexityLimit
    std::vector<Op> allowedOps;  // AllowedFunctions

    // ---------- 复变专用 ----------
    ComplexRegion region;          // 作用区域
    Cplx cpoint{0.0, 0.0};         // 复数点 z0（围道积分时作圆心）
    Cplx cvalue{0.0, 0.0};         // 复数目标值 w0（围道积分时作积分期望值）
    double radius = 1.0;           // 围道半径
    int order = 0;                 // 极点阶数

    // 元信息
    std::string label;    // 玩家可见描述
    bool required = true;  // false = 软约束（加分项）

    std::string describe() const;  // 生成中文描述（label 为空时）
};

struct ConstraintReport {
    ConstraintKind kind = ConstraintKind::DomainOn;
    std::string label;
    bool ok = false;
    std::string detail;  // 失败原因（中文）
};

// 验证单条约束
ConstraintReport checkConstraint(const Constraint& c, const ExprP& f);

// 检查 Player 表达式是否满足"可用函数"限制
ConstraintReport checkAllowedFunctions(const std::vector<Op>& allowed, const ExprP& f);

}  // namespace fcg
