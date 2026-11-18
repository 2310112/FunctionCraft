// Diff.cpp — 待实现（由 engine-simplify 负责）
#include "Diff.h"
namespace fcg {
ExprP diffRaw(const ExprP& f, const std::string&) { return f; }
ExprP diff(const ExprP& f, const std::string&) { return f; }
ExprP diffN(const ExprP& f, const std::string&, int) { return f; }
}  // namespace fcg
