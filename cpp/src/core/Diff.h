// Diff.h — 符号求导
#pragma once

#include <string>

#include "Expr.h"

namespace fcg {

// 对 var 求一阶导数（结果未化简）
ExprP diffRaw(const ExprP& f, const std::string& var);

// 求导并化简
ExprP diff(const ExprP& f, const std::string& var);

// n 阶导数（n>=0）
ExprP diffN(const ExprP& f, const std::string& var, int n);

// 偏导（多变量时按变量名）
inline ExprP partial(const ExprP& f, const std::string& var) { return diff(f, var); }

}  // namespace fcg
