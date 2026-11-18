// Theme.h — FunctionCraft 统一视觉主题
// 调色板 / 字体层级 / Style 配置 / 自绘控件辅助。
// 所有界面文件都应通过这里的函数取色、绘制，避免各页面风格漂移。
#pragma once

#include <string>
#include <vector>

#include "imgui.h"

namespace fcg {
namespace ui {

// ---------------------------------------------------------------
// 调色板（深色主题：#14161C 底 + 青色/琥珀强调）
// ---------------------------------------------------------------
namespace col {
inline const ImVec4 Bg(0.078f, 0.086f, 0.110f, 1.0f);  // #14161C
inline const ImVec4 BgDeep(0.047f, 0.055f, 0.078f, 1.0f);
inline const ImVec4 Panel(0.094f, 0.102f, 0.133f, 1.0f);   // #181A22
inline const ImVec4 Card(0.118f, 0.129f, 0.165f, 1.0f);    // #1E212A
inline const ImVec4 CardHi(0.153f, 0.169f, 0.212f, 1.0f);  // hover
inline const ImVec4 Border(0.180f, 0.200f, 0.251f, 1.0f);  // #2E3340
inline const ImVec4 BorderHi(0.259f, 0.290f, 0.361f, 1.0f);
inline const ImVec4 Text(0.902f, 0.918f, 0.949f, 1.0f);     // #E6EAF2
inline const ImVec4 TextDim(0.596f, 0.635f, 0.706f, 1.0f);  // #98A2B4
inline const ImVec4 TextFaint(0.396f, 0.427f, 0.494f, 1.0f);
inline const ImVec4 Cyan(0.239f, 0.831f, 0.933f, 1.0f);    // #3DD4EE
inline const ImVec4 CyanHi(0.478f, 0.910f, 0.984f, 1.0f);
inline const ImVec4 CyanDeep(0.078f, 0.443f, 0.522f, 1.0f);
inline const ImVec4 Amber(0.980f, 0.690f, 0.196f, 1.0f);   // #FAB032
inline const ImVec4 AmberHi(1.000f, 0.800f, 0.396f, 1.0f);
inline const ImVec4 AmberDeep(0.400f, 0.290f, 0.090f, 1.0f);
inline const ImVec4 Green(0.290f, 0.851f, 0.616f, 1.0f);   // #4AD99D
inline const ImVec4 Red(0.973f, 0.443f, 0.443f, 1.0f);     // #F87171
inline const ImVec4 RedDeep(0.400f, 0.180f, 0.180f, 1.0f);
inline const ImVec4 Purple(0.655f, 0.545f, 0.980f, 1.0f);
inline const ImVec4 Blue(0.396f, 0.616f, 0.980f, 1.0f);
inline const ImVec4 Black(0.0f, 0.0f, 0.0f, 1.0f);
inline const ImVec4 White(1.0f, 1.0f, 1.0f, 1.0f);
}  // namespace col

ImU32 ToU32(const ImVec4& c);
inline ImU32 ToU32(ImU32 c) { return c; }
ImU32 Alpha(const ImVec4& c, float a);          // 指定透明度
ImU32 AlphaU32(ImU32 c, float a);               // 修改已有颜色的 alpha
ImU32 LerpU32(ImU32 a, ImU32 b, float t);       // 颜色插值
ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t);

template <typename T>
inline T Clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

template <typename T>
inline T Min(T a, T b) { return a < b ? a : b; }

template <typename T>
inline T Max(T a, T b) { return a > b ? a : b; }

// ---------------------------------------------------------------
// 字体（init 时由 SetupFonts 构建；不可用时为 nullptr，退回默认字体）
// ---------------------------------------------------------------
extern ImFont* gFontBody;     // 19px 中文正文（默认字体）
extern ImFont* gFontHeading;  // 29px 中文标题
extern ImFont* gFontMono;     // 27px 等宽西文（表达式）
extern ImFont* gFontTitle;    // 44px 中文大标题（仅内置字库，用于固定文案）
extern ImFont* gFontScore;    // 60px 西文大数字（评级 / 分数）
extern ImFont* gFontTiny;     // 遗留：不使用（小字用 AddText 指定字号）
extern bool gFontsBuilt;

struct FontSetupResult {
    bool ok = false;
    int texW = 0, texH = 0;
    bool bodyHasPi = false, bodyHasSqrt = false, bodyHasCheck = false, bodyHasArrow = false;
    bool headingHasPi = false;
    std::string note;
};

// 构建字体图集（替换 main.cpp 已加载的默认字体，补全 π/√/✓/→ 等符号）。
// extraHeadingText: 需要在标题字体中出现的额外文本（例如关卡标题），可空。
FontSetupResult SetupFonts(const std::string& extraHeadingText, const std::string& logPath);

void ApplyTheme();  // 应用 ImGui Style（圆角、间距、配色）

// ---------------------------------------------------------------
// 文本辅助
// ---------------------------------------------------------------
void TextColored(const ImVec4& c, const char* fmt, ...);
void TextDim(const char* fmt, ...);
void TextFaint(const char* fmt, ...);
void TextWrappedColored(const ImVec4& c, const char* text);
void TextWrappedDim(const char* text);
void TextHeading(const char* fmt, ...);   // 标题字体
void TextMono(const ImVec4& c, const char* text, float size = 0.0f);

// 当前字体下的文本尺寸
ImVec2 CalcText(const char* text);
float TextWidth(const char* text);
float HeadingWidth(const char* text);

// ---------------------------------------------------------------
// 绘制辅助
// ---------------------------------------------------------------
// 圆角卡片背景（带柔和阴影）
void DrawCard(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding = 14.0f,
              ImU32 fill = 0, ImU32 border = 0, bool shadow = true);
// 发光描边（选中态）
void DrawGlow(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding, ImU32 color,
              float strength = 1.0f);

struct ButtonStyle {
    ImVec4 bg;
    ImVec4 bgHover;
    ImVec4 bgActive;
    ImVec4 border;
    ImVec4 text;
    bool glow = false;
};

const ButtonStyle& StylePrimary();   // 青色实心（主行动）
const ButtonStyle& StyleAmber();     // 琥珀实心
const ButtonStyle& StyleSecondary(); // 深色描边
const ButtonStyle& StyleGhost();     // 无底透明
const ButtonStyle& StyleDanger();    // 红
const ButtonStyle& StyleKey();        // 计算器数字键
const ButtonStyle& StyleKeyFunc();    // 计算器函数键（青调）
const ButtonStyle& StyleKeyOp();      // 计算器运算符（琥珀调）
const ButtonStyle& StyleKeyUtil();    // 计算器功能键（删除/清空）
const ButtonStyle& StyleToggle(bool on);

enum class Icon {
    None = 0,
    Left,
    Right,
    Backspace,
    Play,
    Check,
    Cross,
    Star,
    Crown,
    Flame,
    Infinity,
    Grid,
    Home,
    Clock,
    Bulb,
    Retry,
    List,
    Swords,
    Sparkle,
    Lock,
};

// 图标全部用 draw list 手绘，不依赖字体符号
void DrawIcon(ImDrawList* dl, Icon icon, const ImVec2& center, float size, ImU32 color,
              float thickness = 2.0f);

// 自绘按钮（返回是否被点击）；icon 可选。label 可为空（仅图标）。
bool FunButton(const char* id, const char* label, const ImVec2& size, const ButtonStyle& st,
               bool enabled = true, Icon icon = Icon::None, ImFont* font = nullptr,
               float rounding = 10.0f);

// 图标 + 文字横向居中排版的按钮（菜单大按钮用）
bool MenuButton(const char* id, const char* label, const char* sub, const ImVec2& size,
                const ButtonStyle& st, Icon icon, ImFont* font = nullptr);

// 开关小控件（返回是否点击切换）
bool ToggleSwitch(const char* id, bool* value, const char* label, const char* hint = nullptr);

// 小徽章（评级 / 难度 / Boss）
void Badge(const char* label, const ImVec4& fg, const ImVec4& bg, float height = 24.0f);

// 维度条（0..1，右侧显示百分比）
void ScoreBar(const char* label, float t, const ImVec4& fill, const ImVec2& size);

// 圆形勾/叉状态图标
void StatusDot(ImDrawList* dl, const ImVec2& center, float radius, bool ok);

// 分隔线
void HSep(float alpha = 1.0f);

// 顶部栏返回按钮（带图标）
bool BackButton(const char* id, const char* label, const ImVec2& size);

// 时间格式化 "02:35"
std::string FormatTime(double seconds);

// 评级相关
const ImVec4& GradeColor(char grade);
const char* GradeLabel(char grade);  // "完美" "优秀" ...
const char* GradeName(char grade);   // "S" "A" ...
ImU32 GradeBg(char grade);

// 悬停提示（延迟显示）
void HelpTooltip(const char* text);

// 中文字符串截断（按显示宽度，保留末尾 …）
std::string Ellipsize(const std::string& text, float maxWidth);
std::string EllipsizeFont(ImFont* font, float size, const std::string& text, float maxWidth);

}  // namespace ui
}  // namespace fcg
