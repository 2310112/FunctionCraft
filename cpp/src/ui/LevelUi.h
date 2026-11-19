// LevelUi.h — 关卡相关的界面工具：约束中文描述、数字/区间格式化、内置兜底关卡
#pragma once

#include <string>
#include <vector>

#include "App.h"
#include "core/Constraint.h"
#include "core/Level.h"

namespace fcg {
namespace ui {

// 约束的中文描述（优先 label → describe() → UI 兜底生成）
std::string ConstraintDesc(const Constraint& c);

// 数字格式化：π 的倍数、整数、小数、∞
std::string FormatNumber(double v);
// 复数格式化："1"、"0.25+0.5i"、"-i"、"1-2i"
std::string FormatComplex(const Cplx& z);
// 复平面区域格式化（disk/annulus/rectangle/半平面/外部/全平面）
std::string FormatRegion(const ComplexRegion& r);
// 区间格式化："[0, 2π)" / "(-∞, +∞)"
std::string FormatInterval(const Interval& iv);
std::string FormatRange(double lo, double hi);

// 内置兜底关卡（data/levels.json 尚未就绪时使用，保证界面可独立运行）
std::vector<LevelEntry> MakeFallbackLevels();

// 内置复数域试炼关卡（关卡数据未就绪时用于界面验证/演示）
LevelDef MakeComplexTestLevel(const std::string& idHint);

// 无尽模式兜底关卡：当 generateEndlessLevel 返回空关卡时使用
LevelDef MakeEndlessFallbackLevel(int index, const std::vector<LevelDef>& pool);

// 无尽模式难度标签兜底（核心 difficultyLabel 为空时）
const char* FallbackDifficultyLabel(int index);
bool FallbackIsBoss(int index);

// 评级比较：'S' > 'A' > 'B' > 'C' > 'D' > 0
bool GradeBetter(char a, char b);
int GradeRank(char g);

}  // namespace ui
}  // namespace fcg
