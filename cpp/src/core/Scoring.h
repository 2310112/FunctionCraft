// Scoring.h — 多维度评分
#pragma once

#include <string>
#include <vector>

#include "Constraint.h"
#include "Level.h"

namespace fcg {

struct ScoreBreakdown {
    bool correct = false;  // 所有硬约束通过

    // 各维度（0..100）
    double precision = 0.0;   // 精确度（边界值误差）
    double elegance = 0.0;    // 优雅度（AST 复杂度）
    double robustness = 0.0;  // 鲁棒性（约束边界附近稳定）
    double speed = 0.0;       // 限时加成（无限制时满分）
    double total = 0.0;       // 最终总分 0..100

    char grade = 'D';  // S/A/B/C/D
    std::string gradeLabel;

    std::vector<ConstraintReport> reports;
    std::vector<std::string> feedback;  // 额外提示（中文）

    int nodeCnt = 0;
    int funcCalls = 0;
    double elapsed = 0.0;
};

// 评级阈值
char gradeOf(double total);
const char* gradeLabel(char grade);

// 评分主体
ScoreBreakdown evaluateSubmission(const ExprP& f, const LevelDef& level, double elapsedSec);

// 只做正确性检查（用于实时预览/无尽模式判定）
bool checkAllConstraints(const ExprP& f, const LevelDef& level,
                         std::vector<ConstraintReport>* reports);

}  // namespace fcg
