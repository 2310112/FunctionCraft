// ShotConfig.h — 命令行调试/截图配置（platform shell 写入，UI 可选读取）
#pragma once

#include <string>

namespace fcg {
namespace ui {

// 由 main.cpp 根据命令行参数填充：
//   --shot=<png路径>   渲染 N 帧后截图并退出
//   --frames=<n>       截图前渲染的帧数（默认 10）
//   --scene=<状态>     menu | levels | play | result | endless
//   --level=<id>       指定关卡 id（scene=play 时）
//   --expr=<表达式>    预填表达式（scene=play 时）
struct ShotConfig {
    std::string scene;
    std::string levelId;
    std::string expression;
};

extern ShotConfig g_shotConfig;  // 定义在 main.cpp

}  // namespace ui
}  // namespace fcg
