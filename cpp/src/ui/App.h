// App.h — 游戏界面（由 ui 负责实现）
#pragma once
namespace fcg { namespace ui {
struct App {
    void init();
    void shutdown();
};
void frame(App& app);
}}
