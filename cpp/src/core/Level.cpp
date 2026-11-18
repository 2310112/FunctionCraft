// Level.cpp — 待实现（由 gameplay 负责）
#include "Level.h"
#include "Json.h"
namespace fcg {
bool LevelDef::valid(std::string*) const { return true; }
std::vector<LevelDef> loadLevels(const std::string&, std::string*) { return {}; }
LevelDef levelFromJson(const Json&, std::string*) { return LevelDef(); }
}  // namespace fcg
