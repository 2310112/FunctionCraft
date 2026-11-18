// MenuView.cpp — 主菜单
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "App.h"
#include "LevelUi.h"
#include "Theme.h"
#include "imgui.h"

namespace fcg {
namespace ui {

namespace {

void DrawBackground(ImDrawList* dl, const ImVec2& origin, const ImVec2& size) {
    // 纵向渐变底色
    const int bands = 48;
    ImVec4 top(0.043f, 0.051f, 0.070f, 1.0f);
    ImVec4 bottom(0.075f, 0.082f, 0.106f, 1.0f);
    for (int i = 0; i < bands; ++i) {
        float t0 = static_cast<float>(i) / bands;
        float t1 = static_cast<float>(i + 1) / bands;
        ImU32 c = ToU32(Lerp(top, bottom, t0));
        dl->AddRectFilled(ImVec2(origin.x, origin.y + size.y * t0),
                          ImVec2(origin.x + size.x, origin.y + size.y * t1 + 1.0f), c);
    }
    // 右上角青色光晕
    ImVec2 glow(origin.x + size.x * 0.78f, origin.y + size.y * 0.42f);
    for (int i = 22; i >= 1; --i) {
        float r = i * 26.0f;
        dl->AddCircleFilled(glow, r, ToU32(Alpha(col::Cyan, 0.0065f)), 64);
    }
    // 左下角琥珀微光
    ImVec2 glow2(origin.x + size.x * 0.10f, origin.y + size.y * 0.92f);
    for (int i = 16; i >= 1; --i) {
        float r = i * 30.0f;
        dl->AddCircleFilled(glow2, r, ToU32(Alpha(col::Amber, 0.004f)), 64);
    }
    // 顶部 / 底部细线
    dl->AddLine(ImVec2(origin.x, origin.y), ImVec2(origin.x + size.x, origin.y),
                ToU32(Alpha(col::Cyan, 0.16f)), 1.0f);
    dl->AddLine(ImVec2(origin.x, origin.y + size.y - 1),
                ImVec2(origin.x + size.x, origin.y + size.y - 1),
                ToU32(Alpha(col::White, 0.05f)), 1.0f);
}

void DrawSpacedText(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 color,
                    const char* text, float spacing) {
    float x = pos.x;
    const char* p = text;
    while (*p) {
        unsigned char b = static_cast<unsigned char>(*p);
        int len = 1;
        if (b >= 0xF0)
            len = 4;
        else if (b >= 0xE0)
            len = 3;
        else if (b >= 0xC0)
            len = 2;
        char buf[5] = {0, 0, 0, 0, 0};
        for (int i = 0; i < len && p[i]; ++i) buf[i] = p[i];
        ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, buf);
        dl->AddText(font, size, ImVec2(x, pos.y), color, buf);
        x += ts.x + spacing;
        p += len;
    }
}

std::string ProgressText(const App& app) {
    int cleared = 0;
    for (const auto& e : app.levels)
        if (e.cleared) ++cleared;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "共 %d 关 · 已通过 %d", static_cast<int>(app.levels.size()),
                  cleared);
    return buf;
}

}  // namespace

void DrawMenu(App& app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    DrawBackground(dl, origin, size);

    const float w = size.x;
    const float h = size.y;

    // 小窗口紧凑布局（至少保证不溢出、可操作）
    const bool compact = UiIsCompact();
    const float titleSize = compact ? (h < 520.0f ? 32.0f : 38.0f) : 50.0f;
    const float btnH = compact ? 58.0f : 76.0f;

    // ------------------------------------------------ 左侧文字区
    float leftX = origin.x + (compact ? 28.0f : Max(70.0f, w * 0.058f));
    float top = origin.y + (compact ? 16.0f : h * 0.175f);
    float contentW = compact ? Max(280.0f, Min(420.0f, w - leftX - 24.0f))
                             : Min(440.0f, w * 0.36f);

    // 顶部小徽章
    {
        ImVec2 bp(leftX, top);
        const char* badgeText = "符号计算 · 函数构造";
        float bw = gFontBody->CalcTextSizeA(15.0f, FLT_MAX, 0.0f, badgeText).x + 26.0f;
        dl->AddRectFilled(bp, ImVec2(bp.x + bw, bp.y + 30.0f), ToU32(Alpha(col::Cyan, 0.10f)),
                          15.0f);
        dl->AddRect(bp, ImVec2(bp.x + bw, bp.y + 30.0f), ToU32(Alpha(col::Cyan, 0.30f)),
                    15.0f, 0, 1.0f);
        dl->AddCircleFilled(ImVec2(bp.x + 15.0f, bp.y + 15.0f), 3.5f, ToU32(col::Cyan));
        dl->AddText(gFontBody, 15.0f, ImVec2(bp.x + 24.0f, bp.y + 6.0f), ToU32(col::Cyan),
                    badgeText);
    }

    // 主标题
    float titleY = top + (compact ? 44.0f : 52.0f);
    float accentY = titleY + titleSize * 1.24f;
    dl->AddText(gFontTitle, titleSize, ImVec2(leftX + 2.0f, titleY + 3.0f),
                ToU32(Alpha(col::Black, 0.45f)), "函数工坊");
    dl->AddText(gFontTitle, titleSize, ImVec2(leftX, titleY), ToU32(col::Text), "函数工坊");
    // 标题下强调条
    dl->AddRectFilled(ImVec2(leftX, accentY), ImVec2(leftX + 64.0f, accentY + 4.0f),
                      ToU32(col::Amber), 2.0f);

    // 英文副标题（字距拉开）
    float enSize = compact ? 15.0f : 19.0f;
    float enSpacing = compact ? 3.5f : 5.0f;
    DrawSpacedText(dl, gFontMono, enSize, ImVec2(leftX + 1.0f, accentY + 10.0f),
                   ToU32(col::Amber), "FUNCTIONCRAFT", enSpacing);

    // 一句话副标题
    const char* tagline = "在约束之中，构造属于你的函数。";
    float taglineY = accentY + (compact ? 38.0f : 46.0f);
    dl->AddText(gFontBody, compact ? 16.0f : 19.0f, ImVec2(leftX, taglineY), ToU32(col::TextDim),
                tagline);

    // 分隔线
    float lineY = taglineY + (compact ? 30.0f : 40.0f);
    dl->AddLine(ImVec2(leftX, lineY), ImVec2(leftX + contentW * 0.82f, lineY),
                ToU32(Alpha(col::White, 0.08f)), 1.0f);

    // ------------------------------------------------ 按钮组
    float by = lineY + (compact ? 14.0f : 26.0f);
    ImGui::SetCursorScreenPos(ImVec2(leftX, by));
    ImVec2 modeSize(Min(430.0f, contentW), btnH);
    std::string levelSub = ProgressText(app);
    if (MenuButton("menu_level", "关卡模式", levelSub.c_str(), modeSize, StylePrimary(),
                   Icon::Grid)) {
        app.startLevelMode();
    }
    by += modeSize.y + (compact ? 10.0f : 14.0f);

    ImGui::SetCursorScreenPos(ImVec2(leftX, by));
    if (MenuButton("menu_endless", "无尽模式", "程序化生成 · 每 10 关 Boss 挑战", modeSize,
                   StyleSecondary(), Icon::Infinity)) {
        app.startEndlessMode();
    }
    by += modeSize.y + (compact ? 12.0f : 18.0f);

    // 限时开关
    ImGui::SetCursorScreenPos(ImVec2(leftX + 2.0f, by));
    ToggleSwitch("menu_timeattack", &app.timeAttack, "限时模式",
                 compact ? nullptr : "每关 3 分钟 · 剩余时间加分");
    by += compact ? 42.0f : 50.0f;

    // 退出 / 重置进度
    ImGui::SetCursorScreenPos(ImVec2(leftX, by));
    ImVec2 quitSize(compact ? 116.0f : 148.0f, compact ? 38.0f : 44.0f);
    if (FunButton("menu_quit", "退出游戏", quitSize, StyleGhost(), true, Icon::None)) {
        app.requestQuit();
    }
    ImGui::SetCursorScreenPos(ImVec2(leftX + quitSize.x + 10.0f, by));
    if (FunButton("menu_reset", "重置进度", ImVec2(compact ? 116.0f : 130.0f, quitSize.y),
                  StyleGhost())) {
        app.resetConfirmOpen = true;
    }

    // 左下角特性标签（填补空白，增强产品感）
    if (!compact) {
        struct Feature {
            const char* text;
            Icon icon;
            const ImVec4* color;
        };
        Feature feats[] = {{"符号引擎验证", Icon::Sparkle, &col::Cyan},
                           {"实时函数绘图", Icon::Grid, &col::Amber},
                           {"四维评分", Icon::Star, &col::Green}};
        float fx = leftX;
        float fy2 = by + 72.0f;
        for (const auto& f : feats) {
            float tw2 = gFontBody->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, f.text).x;
            float cw2 = tw2 + 46.0f;
            ImVec2 q0(fx, fy2);
            ImVec2 q1(fx + cw2, fy2 + 34.0f);
            dl->AddRectFilled(q0, q1, ToU32(Alpha(col::Card, 0.75f)), 9.0f);
            dl->AddRect(q0, q1, ToU32(Alpha(*f.color, 0.22f)), 9.0f, 0, 1.0f);
            DrawIcon(dl, f.icon, ImVec2(q0.x + 19.0f, q0.y + 17.0f), 13.0f, ToU32(*f.color), 2.0f);
            dl->AddText(gFontBody, 13.0f, ImVec2(q0.x + 33.0f, q0.y + 9.0f), ToU32(col::TextDim),
                        f.text);
            fx += cw2 + 10.0f;
        }
    }

    // 底部信息
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "v0.1 · 符号引擎驱动 · 单机版 · ImGui + DX11");
        dl->AddText(gFontBody, 13.0f, ImVec2(origin.x + 26.0f, origin.y + h - 34.0f),
                    ToU32(col::TextFaint), buf);
        const char* right = "鼠标操作 · 回车提交 · Esc 返回";
        ImVec2 ts = gFontBody->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, right);
        dl->AddText(gFontBody, 13.0f, ImVec2(origin.x + w - ts.x - 26.0f, origin.y + h - 34.0f),
                    ToU32(col::TextFaint), right);
    }

    // ------------------------------------------------ 右侧预览卡片（小窗口隐藏）
    if (compact) return;
    float cardW = Min(500.0f, w * 0.36f);
    float cardX = origin.x + w - cardW - Max(64.0f, w * 0.052f);
    float cardY = origin.y + h * 0.145f;
    float cardH = h * 0.70f;
    ImVec2 c0(cardX, cardY);
    ImVec2 c1(cardX + cardW, cardY + cardH);

    // 背景装饰卡（偏移阴影层）
    DrawCard(dl, ImVec2(c0.x + 14.0f, c0.y + 16.0f), ImVec2(c1.x + 14.0f, c1.y + 16.0f), 18.0f,
             ToU32(Alpha(col::Card, 0.35f)), ToU32(Alpha(col::Border, 0.35f)), false);
    DrawCard(dl, c0, c1, 18.0f, ToU32(col::Panel), ToU32(col::Border), true);

    // 卡片头
    dl->AddText(gFontHeading, 23.0f, ImVec2(c0.x + 24.0f, c0.y + 20.0f), ToU32(col::Text),
                "实时预览");
    {
        const char* live = "LIVE";
        float lw = gFontBody->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, live).x;
        ImVec2 lp(c1.x - 24.0f - lw - 18.0f, c0.y + 24.0f);
        dl->AddCircleFilled(ImVec2(lp.x + 4.0f, lp.y + 8.0f), 3.5f, ToU32(col::Green));
        dl->AddText(gFontBody, 13.0f, ImVec2(lp.x + 13.0f, lp.y), ToU32(col::Green), live);
    }
    dl->AddLine(ImVec2(c0.x + 24.0f, c0.y + 58.0f), ImVec2(c1.x - 24.0f, c0.y + 58.0f),
                ToU32(Alpha(col::White, 0.06f)), 1.0f);

    // 绘图区
    ImVec2 plot0(c0.x + 20.0f, c0.y + 76.0f);
    ImVec2 plot1(c1.x - 20.0f, c1.y - 158.0f);
    dl->AddRectFilled(ImVec2(plot0.x - 6.0f, plot0.y - 6.0f),
                      ImVec2(plot1.x + 6.0f, plot1.y + 6.0f),
                      ToU32(ImVec4(0.047f, 0.055f, 0.074f, 1.0f)), 12.0f);
    dl->AddRect(ImVec2(plot0.x - 6.0f, plot0.y - 6.0f), ImVec2(plot1.x + 6.0f, plot1.y + 6.0f),
                ToU32(col::Border), 12.0f, 0, 1.0f);
    dl->PushClipRect(plot0, plot1, true);
    DrawMenuCanvas(app, plot0, plot1);
    dl->PopClipRect();

    // 表达式示例
    float fy = plot1.y + 26.0f;
    dl->AddText(gFontMono, 18.0f, ImVec2(c0.x + 24.0f, fy), ToU32(col::Cyan),
                "f(x) = sin(x) + 0.5·cos(2x)");
    dl->AddText(gFontBody, 14.0f, ImVec2(c0.x + 24.0f, fy + 28.0f), ToU32(col::TextFaint),
                "输入表达式 → 实时采样绘制 → 提交验证约束");

    // 约束示例 chips
    float chipY = c1.y - 58.0f;
    struct Chip {
        const char* text;
        bool ok;
    };
    Chip chips[] = {{"f(0) = 0", true}, {"奇函数", true}, {"周期 2π", true}};
    float cx = c0.x + 24.0f;
    for (const auto& chip : chips) {
        float tw = gFontBody->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, chip.text).x;
        float cw = tw + 40.0f;
        ImVec2 p0(cx, chipY);
        ImVec2 p1(cx + cw, chipY + 30.0f);
        dl->AddRectFilled(p0, p1, ToU32(Alpha(col::Card, 0.9f)), 8.0f);
        dl->AddRect(p0, p1, ToU32(Alpha(col::Green, 0.30f)), 8.0f, 0, 1.0f);
        DrawIcon(dl, Icon::Check, ImVec2(p0.x + 16.0f, chipY + 15.0f), 13.0f,
                 ToU32(col::Green), 2.0f);
        dl->AddText(gFontBody, 14.0f, ImVec2(p0.x + 28.0f, chipY + 6.0f), ToU32(col::TextDim),
                    chip.text);
        cx += cw + 8.0f;
    }

    // 右侧装饰的小字
    const char* motto = "约束不是限制，而是方向。";
    ImVec2 ms = gFontBody->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, motto);
    dl->AddText(gFontBody, 13.0f, ImVec2(c1.x - ms.x - 24.0f, c1.y - 30.0f),
                ToU32(col::TextFaint), motto);
}

// ---------------------------------------------------------------
// 重置进度确认框
// ---------------------------------------------------------------
void DrawResetConfirm(App& app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ToU32(ImVec4(0, 0, 0, 0.66f)));

    float cw = Min(520.0f, ws.x - 80.0f);
    float ch = 250.0f;
    ImVec2 c0(wp.x + (ws.x - cw) * 0.5f, wp.y + (ws.y - ch) * 0.5f);
    ImVec2 c1(c0.x + cw, c0.y + ch);
    DrawCard(dl, c0, c1, 18.0f, ToU32(col::Panel), ToU32(col::Border));

    DrawIcon(dl, Icon::Lock, ImVec2(c0.x + 40.0f, c0.y + 38.0f), 17.0f, ToU32(col::Red), 2.0f);
    dl->AddText(gFontHeading, 24.0f, ImVec2(c0.x + 60.0f, c0.y + 22.0f), ToU32(col::Text),
                "重置进度");
    dl->AddLine(ImVec2(c0.x + 26.0f, c0.y + 66.0f), ImVec2(c1.x - 26.0f, c0.y + 66.0f),
                ToU32(col::Border), 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(c0.x + 26.0f, c0.y + 84.0f));
    ImGui::PushTextWrapPos(c1.x - 26.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, col::TextDim);
    ImGui::TextUnformatted("将清除全部关卡评级、无尽模式最高纪录与提交统计，此操作无法撤销。");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();

    float by = c1.y - 70.0f;
    ImGui::SetCursorScreenPos(ImVec2(c0.x + 26.0f, by));
    if (FunButton("reset_cancel", "取消", ImVec2(110.0f, 44.0f), StyleSecondary())) {
        app.resetConfirmOpen = false;
    }
    ImGui::SetCursorScreenPos(ImVec2(c1.x - 26.0f - 160.0f, by));
    if (FunButton("reset_ok", "确认重置", ImVec2(160.0f, 44.0f), StyleDanger())) {
        app.resetConfirmOpen = false;
        app.resetProgress();
    }
}

}  // namespace ui
}  // namespace fcg
