// Limit.h — 极限计算
#pragma once

#include <string>

#include "Expr.h"

namespace fcg {

struct LimitResult {
    enum class Kind {
        Finite,     // 有限极限
        PosInf,
        NegInf,
        Undefined,  // 趋于不同值 / 振荡 / 无定义
        Unknown,    // 无法判定
    };
    Kind kind = Kind::Unknown;
    double value = 0.0;
    std::string detail;

    bool ok() const { return kind == Kind::Finite; }
    std::string str() const;
};

enum class Direction { Both, Left, Right };

// x → target
LimitResult limitAt(const ExprP& f, const std::string& var, double target,
                    Direction dir = Direction::Both);

// x → ±∞ ；sign=+1 表示 +∞，-1 表示 -∞
LimitResult limitAtInfinity(const ExprP& f, const std::string& var, int sign);

// 水平渐近线：若 x→+∞ 或 -∞ 极限为有限值则返回该值
bool horizontalAsymptote(const ExprP& f, const std::string& var, int sign, double* out);

}  // namespace fcg
