// EndlessGen.cpp — 待实现（由 gameplay 负责）
#include "EndlessGen.h"
namespace fcg {
LevelDef generateEndlessLevel(int, uint64_t) { return LevelDef(); }
bool isBossLevel(int) { return false; }
std::string difficultyLabel(int) { return ""; }
bool selfTestGenerator(int, uint64_t, std::string*) { return false; }
}  // namespace fcg
