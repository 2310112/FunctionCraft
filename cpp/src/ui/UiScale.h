// UiScale.h — 界面缩放（DPI / 分辨率适配）
//
// 设计约定：
//   · 所有界面代码里的尺寸/字号都写成「逻辑像素」（= 1440x900 设计稿上的像素）
//   · 通过 S(v) 换算成实际像素；由 main.cpp 根据显示器 DPI 设置 gUiScale
//   · 例如 2560x1600 @175% 缩放 → gUiScale = 1.75，逻辑 19px 字 → 实际 33px，清晰不拉伸
#pragma once

#include "imgui.h"

namespace fcg {
namespace ui {

// 实际像素 / 逻辑像素。（1.0 = 96 DPI / 100% 缩放）
extern float gUiScale;

// 逻辑像素 → 实际像素
inline float S(float v) { return v * gUiScale; }
inline ImVec2 Sv(float x, float y) { return ImVec2(x * gUiScale, y * gUiScale); }
inline ImVec2 Sv(const ImVec2& v) { return ImVec2(v.x * gUiScale, v.y * gUiScale); }

// 实际像素 → 逻辑像素（用于判断窗口是否够大、紧凑布局阈值等）
inline float ToLogical(float v) { return gUiScale > 0.0f ? v / gUiScale : v; }
inline ImVec2 ToLogical(const ImVec2& v) {
    return ImVec2(ToLogical(v.x), ToLogical(v.y));
}

// DPI 变化时重建字体图集（实现在 main.cpp，内部调用 Theme::SetupFonts）
void RebuildFontsForScale();

}  // namespace ui
}  // namespace fcg
