// Simplify.cpp — 待实现（由 engine-simplify 负责）
#include "Simplify.h"
namespace fcg {
ExprP simplify(const ExprP& e) { return e; }
ExprP simplifyOnce(const ExprP& e) { return e; }
bool isZeroSimplified(const ExprP& e) { return isZero(e); }
bool symbolicallyEqual(const ExprP& x, const ExprP& y) { return structurallyEqual(x, y); }
std::string canonicalKey(const ExprP& e) { return e ? e->toStr() : ""; }
bool numericallyEqual(const ExprP&, const ExprP&, const std::vector<std::string>&, double) { return false; }
ExprP rationalize(const ExprP& e) { return e; }
}  // namespace fcg
