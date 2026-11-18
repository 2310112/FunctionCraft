// Scoring.cpp — 待实现（由 gameplay 负责）
#include "Scoring.h"
namespace fcg {
char gradeOf(double) { return 'D'; }
const char* gradeLabel(char) { return ""; }
ScoreBreakdown evaluateSubmission(const ExprP&, const LevelDef&, double) { return ScoreBreakdown(); }
bool checkAllConstraints(const ExprP&, const LevelDef&, std::vector<ConstraintReport>*) { return false; }
}  // namespace fcg
