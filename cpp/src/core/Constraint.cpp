// Constraint.cpp — 待实现（由 gameplay 负责）
#include "Constraint.h"
namespace fcg {
const char* constraintKindName(ConstraintKind) { return "unknown"; }
const char* constraintKindDisplay(ConstraintKind) { return "未实现"; }
bool constraintKindSupported(ConstraintKind) { return false; }
std::string Constraint::describe() const { return label; }
ConstraintReport checkConstraint(const Constraint& c, const ExprP&) {
    ConstraintReport r; r.kind = c.kind; r.label = c.label; r.ok = false; r.detail = "未实现"; return r;
}
ConstraintReport checkAllowedFunctions(const std::vector<Op>&, const ExprP&) {
    ConstraintReport r; r.ok = false; r.detail = "未实现"; return r;
}
}  // namespace fcg
