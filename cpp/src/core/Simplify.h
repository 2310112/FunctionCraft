// Simplify.h — 符号化简与符号等价判定
#pragma once

#include <string>
#include <vector>

#include "Expr.h"

namespace fcg {

// 完整化简（多次迭代直到收敛）
ExprP simplify(const ExprP& e);

// 化简一轮（便于测试）
ExprP simplifyOnce(const ExprP& e);

// 是否符号意义上为 0（化简后为数值 0）
bool isZeroSimplified(const ExprP& e);

// 符号等价：simplify(a - b) == 0
bool symbolicallyEqual(const ExprP& x, const ExprP& y);

// 规范化文本键（可交换运算排序后），用于快速结构比较/哈希
std::string canonicalKey(const ExprP& e);

// 数值等价兜底：在若干个随机点上采样比较（符号化简失败时使用）
bool numericallyEqual(const ExprP& x, const ExprP& y, const std::vector<std::string>& vars,
                      double tol = 1e-9);

// 约分/整理：把表达式写成分母不含根式的形式（用于展示，可选）
ExprP rationalize(const ExprP& e);

}  // namespace fcg
