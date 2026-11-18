// Limit.cpp — 待实现（由 engine-analysis 负责）
#include "Limit.h"
namespace fcg {
std::string LimitResult::str() const { return "?"; }
LimitResult limitAt(const ExprP&, const std::string&, double, Direction) { return LimitResult(); }
LimitResult limitAtInfinity(const ExprP&, const std::string&, int) { return LimitResult(); }
bool horizontalAsymptote(const ExprP&, const std::string&, int, double*) { return false; }
}  // namespace fcg
