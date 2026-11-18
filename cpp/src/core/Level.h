// Level.h — 关卡数据模型与加载
#pragma once

#include <string>
#include <vector>

#include "Constraint.h"
#include "Json.h"

namespace fcg {

struct LevelDef {
    std::string id;
    std::string title;
    std::string hint;
    int chapter = 1;

    std::vector<std::string> vars{"x"};
    std::vector<Constraint> constraints;

    double timeLimit = 0.0;   // 秒；0 = 不限时
    double parScore = 85.0;   // 参考解分数
    std::string reference;    // 参考解表达式（用于提示/对比）
    std::string intro;        // 关卡引导文字

    // 校验：所有约束是否可判定
    bool valid(std::string* err) const;
};

// 从 JSON 文件加载关卡数组
std::vector<LevelDef> loadLevels(const std::string& path, std::string* err = nullptr);

// 解析单个关卡 JSON
LevelDef levelFromJson(const Json& j, std::string* err);

}  // namespace fcg
