// App.cpp — 待实现（由 ui 负责）
#include "App.h"
#include "imgui.h"
namespace fcg { namespace ui {
void App::init() {}
void App::shutdown() {}
void frame(App&) {
    ImGui::Begin("FunctionCraft");
    ImGui::Text("UI 待实现");
    ImGui::End();
}
}}
