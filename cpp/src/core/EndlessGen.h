// EndlessGen.h — 无尽模式关卡程序化生成
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Level.h"

namespace fcg {

struct EndlessConfig {
    uint64_t seed = 0;
    int startIndex = 1;
};

// 按关卡序号生成无尽关卡（index 从 1 开始；每 10 关为 Boss 关）
LevelDef generateEndlessLevel(int index, uint64_t seed);

// 该关是否为 Boss 关
bool isBossLevel(int index);

// 难度描述（UI 显示）
std::string difficultyLabel(int index);

// 生成器自检：验证生成的关卡"必然有解"（用参考解验证）
bool selfTestGenerator(int levels, uint64_t seed, std::string* err);

}  // namespace fcg
