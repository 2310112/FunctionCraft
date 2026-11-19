// GameView.cpp — 游戏主界面：约束面板 / 表达式输入 / 计算器键盘 / 函数图像 / 底部操作栏
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "App.h"
#include "LevelUi.h"
#include "core/EndlessGen.h"
#include "Theme.h"
#include "imgui.h"

namespace fcg {
namespace ui {

namespace {

constexpr float kPi = 3.14159265358979323846;

// ---------------------------------------------------------------
// 顶栏
// ---------------------------------------------------------------
void DrawTopBar(App& app, float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    float barH = S(66.0f);
    dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + barH), ToU32(col::Panel));
    dl->AddLine(ImVec2(origin.x, origin.y + barH), ImVec2(origin.x + w, origin.y + barH),
                ToU32(col::Border), 1.0f);

    // 顶栏元素与内容区左右边界对齐（超宽屏不会贴到屏幕边缘）
    ContentArea ca = MakeContentArea(18.0f, 0.0f, 0.0f, 0.0f, 1600.0f, 1000.0f);
    const float bx = origin.x + S(ca.x);
    const float cxr = bx + S(ca.w);

    ImGui::SetCursorScreenPos(ImVec2(bx, origin.y + S(14.0f)));
    if (FunButton("gv_back", "返回", ImVec2(S(92.0f), S(38.0f)), StyleSecondary(), true, Icon::Left)) {
        app.backFromGame();
    }

    float x = bx + S(112.0f);
    std::string title = app.level.title.empty() ? "未命名关卡" : app.level.title;
    title = Ellipsize(title, w * 0.34f);
    dl->AddText(gFontHeading, S(24.0f), ImVec2(x, origin.y + S(8.0f)), ToU32(col::Text),
                title.c_str());

    // 副标题：章节 / 模式 / 已提交状态
    char sub[160];
    if (app.endless) {
        std::snprintf(sub, sizeof(sub), "无尽模式 · 第 %d 关 · 最高纪录 第 %d 关", app.endlessIndex,
                      std::max(app.endlessBest, 0));
    } else {
        std::snprintf(sub, sizeof(sub), "%s · 关卡 %d / %d", "关卡模式", app.levelIndex + 1,
                      static_cast<int>(app.levels.size()));
    }
    dl->AddText(gFontBody, S(13.0f), ImVec2(x + 1.0f, origin.y + S(40.0f)), ToU32(col::TextFaint), sub);

    // 右侧状态徽章（小窗口只保留最重要的信息）
    if (UiIsCompact()) return;
    float rx = cxr;
    if (app.isComplexLevel()) {
        const char* tag = "复数域";
        float tw = TextWidth(tag) + S(22.0f);
        rx -= tw;
        ImVec2 p0(rx, origin.y + S(20.0f));
        ImVec2 p1(rx + tw, origin.y + S(46.0f));
        dl->AddRectFilled(p0, p1, ToU32(Alpha(col::Purple, 0.16f)), S(7.0f));
        dl->AddRect(p0, p1, ToU32(Alpha(col::Purple, 0.40f)), S(7.0f), 0, S(1.0f));
        dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + S(11.0f), p0.y + S(5.0f)),
                    ToU32(col::Purple), tag);
        rx -= S(10.0f);
    }
    if (app.timeLimit() > 0.0) {
        const char* tag = "限时";
        float tw = TextWidth(tag) + S(34.0f);
        rx -= tw;
        ImVec2 p0(rx, origin.y + S(20.0f));
        ImVec2 p1(rx + tw, origin.y + S(46.0f));
        dl->AddRectFilled(p0, p1, ToU32(Alpha(col::Amber, 0.14f)), S(7.0f));
        DrawIcon(dl, Icon::Clock, ImVec2(p0.x + S(15.0f), p0.y + S(13.0f)), S(11.0f),
                 ToU32(col::Amber), S(1.8f));
        dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + S(25.0f), p0.y + S(5.0f)), ToU32(col::Amber), tag);
        rx -= S(10.0f);
    }
    ImGui::PushFont(gFontBody);
    // 通过情况
    if (!app.reports.empty()) {
        int passed = 0;
        for (const auto& r : app.reports)
            if (r.ok) ++passed;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "通过 %d/%d", passed,
                      static_cast<int>(app.reports.size()));
        bool allOk = passed == static_cast<int>(app.reports.size());
        float tw = TextWidth(buf) + S(22.0f);
        rx -= tw;
        ImVec2 p0(rx, origin.y + S(20.0f));
        ImVec2 p1(rx + tw, origin.y + S(46.0f));
        dl->AddRectFilled(p0, p1, ToU32(Alpha(allOk ? col::Green : col::Red, 0.12f)), S(7.0f));
        dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + S(11.0f), p0.y + S(5.0f)),
                    ToU32(allOk ? col::Green : col::Red), buf);
        rx -= S(10.0f);
    }
    // 难度 / Boss
    if (app.endless) {
        const char* diff = difficultyLabel(app.endlessIndex).c_str();
        std::string diffStr = diff && diff[0] ? std::string(diff) : std::string(FallbackDifficultyLabel(app.endlessIndex));
        bool boss = isBossLevel(app.endlessIndex) || FallbackIsBoss(app.endlessIndex);
        if (boss) {
            float tw = TextWidth(diffStr.c_str()) + S(46.0f);
            rx -= tw;
            ImVec2 p0(rx, origin.y + S(20.0f));
            ImVec2 p1(rx + tw, origin.y + S(46.0f));
            dl->AddRectFilled(p0, p1, ToU32(Alpha(col::Amber, 0.14f)), S(7.0f));
            DrawIcon(dl, Icon::Crown, ImVec2(p0.x + S(15.0f), p0.y + S(13.0f)), S(13.0f), ToU32(col::Amber),
                     S(2.0f));
            dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + S(29.0f), p0.y + S(5.0f)), ToU32(col::Amber),
                        diffStr.c_str());
            rx -= S(10.0f);
        } else {
            float tw = TextWidth(diffStr.c_str()) + S(22.0f);
            rx -= tw;
            ImVec2 p0(rx, origin.y + S(20.0f));
            ImVec2 p1(rx + tw, origin.y + S(46.0f));
            dl->AddRectFilled(p0, p1, ToU32(Alpha(col::Cyan, 0.10f)), S(7.0f));
            dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + S(11.0f), p0.y + S(5.0f)), ToU32(col::Cyan),
                        diffStr.c_str());
            rx -= S(10.0f);
        }
        // 模式标签
        const char* tag = "无尽模式";
        float tw = TextWidth(tag) + S(22.0f);
        rx -= tw;
        ImVec2 p0(rx, origin.y + S(20.0f));
        ImVec2 p1(rx + tw, origin.y + S(46.0f));
        dl->AddRectFilled(p0, p1, ToU32(Alpha(col::Purple, 0.13f)), S(7.0f));
        dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + S(11.0f), p0.y + S(5.0f)), ToU32(col::Purple), tag);
        rx -= S(10.0f);
    } else {
        const char* tag = "关卡模式";
        float tw = TextWidth(tag) + S(22.0f);
        rx -= tw;
        ImVec2 p0(rx, origin.y + S(20.0f));
        ImVec2 p1(rx + tw, origin.y + S(46.0f));
        dl->AddRectFilled(p0, p1, ToU32(Alpha(col::Cyan, 0.10f)), S(7.0f));
        dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + S(11.0f), p0.y + S(5.0f)), ToU32(col::Cyan), tag);
    }
    ImGui::PopFont();
}

// ---------------------------------------------------------------
// 表达式输入卡片
// ---------------------------------------------------------------
void DrawExprCard(App& app, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);

    ImU32 border = ToU32(col::Border);
    if (!app.expr.empty() && !app.exprValid) border = ToU32(Alpha(col::Red, 0.65f));
    DrawCard(dl, p0, p1, S(14.0f), ToU32(col::Card), border);

    const float pad = S(18.0f);
    // 标题行
    dl->AddText(gFontBody, S(13.0f), ImVec2(p0.x + pad, p0.y + S(10.0f)), ToU32(col::TextFaint),
                "函数表达式");
    {
        const char* hint = app.isComplexLevel() ? "复变函数：变量 z · 回车提交"
                                                : "回车提交 · 可直接键入字母与数字";
        float tw = gFontBody->CalcTextSizeA(S(12.0f), FLT_MAX, 0.0f, hint).x;
        dl->AddText(gFontBody, S(12.0f), ImVec2(p1.x - pad - tw, p0.y + S(11.0f)),
                    ToU32(col::TextFaint), hint);
    }

    // f(x) = / f(z) =
    const std::string fnLabel = "f(" + app.level.primaryVar() + ") =";
    float baseY = p0.y + S(46.0f);
    float exprFontSize = gFontMono ? gFontMono->FontSize : S(27.0f);
    dl->AddText(gFontMono, exprFontSize, ImVec2(p0.x + pad, baseY), ToU32(col::Cyan),
                fnLabel.c_str());
    float labelW = gFontMono->CalcTextSizeA(exprFontSize, FLT_MAX, 0.0f,
                                           (fnLabel + " ").c_str()).x;
    float textX = p0.x + pad + labelW + S(8.0f);
    float textRight = p1.x - pad - S(8.0f);

    // 表达式文本 + 光标（限制在可视区域内，超出时滚动到光标处）
    std::string& e = app.expr;
    float charW = gFontMono->CalcTextSizeA(exprFontSize, FLT_MAX, 0.0f, "0").x;
    int cursor = app.cursor;
    if (cursor < 0) cursor = 0;
    if (cursor > static_cast<int>(e.size())) cursor = static_cast<int>(e.size());
    float cursorX = textX + static_cast<float>(cursor) * charW;
    float viewLeft = textX;
    float viewRight = textRight;
    float shift = 0.0f;
    if (cursorX > viewRight) shift = cursorX - viewRight;
    if (cursorX - shift < viewLeft) shift = cursorX - viewLeft;
    if (shift < 0) shift = 0;

    if (e.empty()) {
        const char* placeholder = app.isComplexLevel() ? "1/(z - 1)" : "sin(x) + cos(2*x)";
        dl->AddText(gFontMono, exprFontSize, ImVec2(textX, baseY),
                    Alpha(col::TextFaint, 0.62f), placeholder);
    } else {
        dl->PushClipRect(ImVec2(textX - S(2.0f), p0.y + S(26.0f)), ImVec2(textRight, p1.y - S(6.0f)),
                         true);
        dl->AddText(gFontMono, exprFontSize, ImVec2(textX - shift, baseY),
                    ToU32(app.exprValid ? col::Text : col::Red), e.c_str());
        dl->PopClipRect();
    }
    // 闪烁光标
    double t = ImGui::GetTime();
    float alpha = 0.55f + 0.45f * static_cast<float>(std::sin(t * 6.0));
    if (app.expr.empty()) alpha = 0.0f;
    if (app.hasResult) alpha = 0.25f;
    dl->AddRectFilled(
        ImVec2(textX - shift + static_cast<float>(cursor) * charW, baseY - S(2.0f)),
        ImVec2(textX - shift + static_cast<float>(cursor) * charW + S(2.0f),
               baseY + exprFontSize + S(2.0f)),
        ToU32(Alpha(col::Cyan, alpha)));

    // 点击定位光标
    ImGui::SetCursorScreenPos(ImVec2(textX - S(4.0f), p0.y + S(24.0f)));
    ImGui::InvisibleButton("##expr_click", ImVec2(textRight - textX + S(4.0f), S(42.0f)));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float mx = ImGui::GetMousePos().x + shift - textX;
        int pos = static_cast<int>(std::lround(mx / charW));
        app.cursor = std::max(0, std::min(static_cast<int>(e.size()), pos));
    }

    // 错误提示
    if (!app.expr.empty() && !app.exprValid) {
        std::string err = "输入错误：" + app.exprError;
        dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + pad, p1.y - S(30.0f)), ToU32(col::Red),
                    Ellipsize(err, size.x - pad * 2).c_str());
    } else if (app.exprValid) {
        std::string info = "表达式语法正确";
        if (app.ast) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "语法正确 · %d 个节点 · %d 次函数调用", nodeCount(app.ast),
                          funcCallCount(app.ast));
            info = buf;
        }
        dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + pad, p1.y - S(30.0f)),
                    ToU32(Alpha(col::Green, 0.85f)), info.c_str());
    } else if (app.expr.empty()) {
        dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + pad, p1.y - S(30.0f)), ToU32(col::TextFaint),
                    "点击下方键盘输入，或直接用键盘键入 sin(x)");
    }
}

// ---------------------------------------------------------------
// 计算器键盘
// ---------------------------------------------------------------
enum class KeyKind { Func, Var, Num, Op, Util, Submit };

struct KeyDef {
    const char* label;
    KeyKind kind;
    const char* insert;  // 为空则用 label
    bool isFunction;     // 需要自动补全括号
    Icon icon;
    int action;  // 0=输入 1=左移 2=右移 3=删除 4=清空 5=提交
};

void DrawKeypad(App& app, const ImVec2& origin, float width, float height) {
    const bool complexMode = app.isComplexLevel();
    const int cols = 6;
    const int rows = 6;
    const int totalRows = complexMode ? rows + 1 : rows;
    const float gap = S(8.0f);
    float kw = (width - gap * (cols - 1)) / cols;
    float kh = (height - gap * (totalRows - 1)) / totalRows;

    // 复数专用键行：i / Re( / Im( / conj( / arg(
    if (complexMode) {
        struct CKey {
            const char* label;
            const char* insert;
            bool isFn;
        };
        static const CKey ckeys[6] = {
            {"i", "i", false},       {"Re(", "re", true},   {"Im(", "im", true},
            {"conj(", "conj", true}, {"arg(", "arg", true}, {"|z|", "abs", true}};
        for (int c = 0; c < cols; ++c) {
            const CKey& k = ckeys[c];
            ImVec2 pos(origin.x + c * (kw + gap), origin.y);
            ImGui::SetCursorScreenPos(pos);
            char id[64];
            std::snprintf(id, sizeof(id), "##ckey_%d", c);
            if (FunButton(id, k.label, ImVec2(kw, kh), StyleKeyFunc(), true, Icon::None)) {
                if (!app.hasResult) {
                    if (k.isFn)
                        app.insertFunction(k.insert);
                    else
                        app.insertText(k.insert);
                }
            }
        }
    }
    const float gridY = origin.y + (complexMode ? (kh + gap) : 0.0f);
    const std::string varNameStr = app.level.primaryVar();
    const char* varName = varNameStr.c_str();

    static const KeyDef keys[rows][cols] = {
        // 第一行：三角函数与指数
        {{"sin", KeyKind::Func, "sin", true, Icon::None, 0},
         {"cos", KeyKind::Func, "cos", true, Icon::None, 0},
         {"tan", KeyKind::Func, "tan", true, Icon::None, 0},
         {"ln", KeyKind::Func, "ln", true, Icon::None, 0},
         {"exp", KeyKind::Func, "exp", true, Icon::None, 0},
         {"√", KeyKind::Func, "sqrt", true, Icon::None, 0}},
        // 第二行：常量与括号
        {{"abs", KeyKind::Func, "abs", true, Icon::None, 0},
         {"^", KeyKind::Op, "^", false, Icon::None, 0},
         {"π", KeyKind::Func, "pi", false, Icon::None, 0},
         {"e", KeyKind::Func, "e", false, Icon::None, 0},
         {"(", KeyKind::Op, "(", true, Icon::None, 0},
         {")", KeyKind::Op, ")", false, Icon::None, 0}},
        // 第三行：变量与光标
        {{"x", KeyKind::Var, "x", false, Icon::None, 0},
         {"tanh", KeyKind::Func, "tanh", true, Icon::None, 0},
         {"sinh", KeyKind::Func, "sinh", true, Icon::None, 0},
         {"log2", KeyKind::Func, "log2", true, Icon::None, 0},
         {"", KeyKind::Util, "", false, Icon::Left, 1},
         {"", KeyKind::Util, "", false, Icon::Right, 2}},
        // 第四行：数字 7-9 与除法/删除
        {{"7", KeyKind::Num, "7", false, Icon::None, 0},
         {"8", KeyKind::Num, "8", false, Icon::None, 0},
         {"9", KeyKind::Num, "9", false, Icon::None, 0},
         {"÷", KeyKind::Op, "/", false, Icon::None, 0},
         {"删除", KeyKind::Util, "", false, Icon::Backspace, 3},
         {"清空", KeyKind::Util, "", false, Icon::None, 4}},
        // 第五行：数字 4-6 与乘加
        {{"4", KeyKind::Num, "4", false, Icon::None, 0},
         {"5", KeyKind::Num, "5", false, Icon::None, 0},
         {"6", KeyKind::Num, "6", false, Icon::None, 0},
         {"×", KeyKind::Op, "*", false, Icon::None, 0},
         {"+", KeyKind::Op, "+", false, Icon::None, 0},
         {"-", KeyKind::Op, "-", false, Icon::None, 0}},
        // 第六行：数字 1-3 / 小数点 / 0 / 提交
        {{"1", KeyKind::Num, "1", false, Icon::None, 0},
         {"2", KeyKind::Num, "2", false, Icon::None, 0},
         {"3", KeyKind::Num, "3", false, Icon::None, 0},
         {".", KeyKind::Num, ".", false, Icon::None, 0},
         {"0", KeyKind::Num, "0", false, Icon::None, 0},
         {"=", KeyKind::Op, "", false, Icon::None, 5}},
    };

    std::string buttonId = "##key";
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const KeyDef& k = keys[r][c];
            ImVec2 pos(origin.x + c * (kw + gap), gridY + r * (kh + gap));
            ImGui::SetCursorScreenPos(pos);
            char id[64];
            std::snprintf(id, sizeof(id), "##key_%d_%d", r, c);

            const ButtonStyle* st = &StyleKey();
            switch (k.kind) {
                case KeyKind::Func: st = &StyleKeyFunc(); break;
                case KeyKind::Var: st = &StyleKeyFunc(); break;
                case KeyKind::Num: st = &StyleKey(); break;
                case KeyKind::Op: st = &StyleKeyOp(); break;
                case KeyKind::Util: st = &StyleKeyUtil(); break;
                case KeyKind::Submit: st = &StylePrimary(); break;
            }
            ImFont* labelFont = (k.kind == KeyKind::Num) ? gFontMono : gFontBody;
            const char* label = (k.kind == KeyKind::Var) ? varName : k.label;
            bool clicked = false;
            if (label[0] == 0)
                clicked = FunButton(id, "", ImVec2(kw, kh), *st, true, k.icon);
            else
                clicked = FunButton(id, label, ImVec2(kw, kh), *st, true, k.icon, labelFont);

            if (!clicked || app.hasResult) continue;
            switch (k.action) {
                case 0:
                    if (k.kind == KeyKind::Var)
                        app.insertText(app.level.primaryVar());
                    else if (k.isFunction)
                        app.insertFunction(k.insert);
                    else
                        app.insertText(k.insert);
                    break;
                case 1: app.moveCursor(-1); break;
                case 2: app.moveCursor(1); break;
                case 3: app.backspace(); break;
                case 4: app.clearExpr(); break;
                case 5: app.submit(); break;
            }
        }
    }
}

// ---------------------------------------------------------------
// 底部操作栏
// ---------------------------------------------------------------
void DrawBottomBar(App& app, float w) {
    const bool compact = UiIsCompact();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    float h = ImGui::GetWindowSize().y;
    float barH = S(86.0f);
    ImVec2 p0(origin.x, origin.y + h - barH);
    ImVec2 p1(origin.x + w, origin.y + h);
    dl->AddRectFilled(p0, p1, ToU32(col::Panel));
    dl->AddLine(p0, ImVec2(p1.x, p0.y), ToU32(col::Border), 1.0f);

    float by = p0.y + S(16.0f);
    // 提示
    ImGui::SetCursorScreenPos(ImVec2(p0.x + S(20.0f), by));
    if (FunButton("gv_hint", "提示", ImVec2(S(104.0f), S(52.0f)), StyleSecondary(), true, Icon::Bulb)) {
        app.hintOpen = !app.hintOpen;
    }
    // 重新开始
    if (!compact) {
        ImGui::SetCursorScreenPos(ImVec2(p0.x + S(134.0f), by));
        if (FunButton("gv_retry", "重新开始", ImVec2(S(118.0f), S(52.0f)), StyleGhost(), true,
                      Icon::Retry)) {
            app.retryLevel();
        }
    }

    // 计时 / 倒计时
    double lim = app.timeLimit();
    double shown = lim > 0 ? app.remainingTime() : app.elapsed();
    bool timed = lim > 0;
    bool danger = timed && shown < 30.0;
    ImVec2 t0(p0.x + (compact ? S(134.0f) : S(274.0f)), by + S(3.0f));
    ImVec2 t1(t0.x + S(156.0f), by + S(49.0f));
    dl->AddRectFilled(t0, t1, ToU32(col::Card), S(10.0f));
    dl->AddRect(t0, t1, ToU32(Alpha(danger ? col::Red : col::Border, 0.9f)), S(10.0f), 0, 1.0f);
    DrawIcon(dl, Icon::Clock, ImVec2(t0.x + S(22.0f), t0.y + S(23.0f)), S(15.0f),
             ToU32(danger ? col::Red : col::TextDim), S(2.0f));
    dl->AddText(gFontBody, S(12.0f), ImVec2(t0.x + S(38.0f), t0.y + S(6.0f)), ToU32(col::TextFaint),
                timed ? (danger ? "剩余时间" : "限时倒计时") : "已用时间");
    std::string timeStr = FormatTime(shown);
    dl->AddText(gFontMono, S(24.0f), ImVec2(t0.x + S(38.0f), t0.y + S(19.0f)),
                ToU32(danger ? col::Red : col::Text), timeStr.c_str());

    // 右侧提交（主 CTA）
    float submitW = compact ? S(168.0f) : S(210.0f);
    ImGui::SetCursorScreenPos(ImVec2(p1.x - S(22.0f) - submitW, by + (compact ? S(2.0f) : 0.0f)));
    if (FunButton("gv_submit", "提交验证", ImVec2(submitW, compact ? S(48.0f) : S(54.0f)),
                  StylePrimary(), true, Icon::Check)) {
        app.submit();
    }

    // 键盘提示
    if (!compact) {
        const char* tips = app.isComplexLevel()
                               ? "回车 提交 · i 可直接键入 · 拖拽/滚轮缩放复平面 · Esc 返回"
                               : "回车 提交 · Backspace 删除 · ← → 移动光标 · Esc 返回";
        ImVec2 ts = gFontBody->CalcTextSizeA(S(12.0f), FLT_MAX, 0.0f, tips);
        dl->AddText(gFontBody, S(12.0f), ImVec2(p1.x - S(22.0f) - S(210.0f) - ts.x - S(24.0f), by + S(30.0f)),
                    ToU32(col::TextFaint), tips);
    }
}

}  // namespace

// ---------------------------------------------------------------
// 提示弹窗
// ---------------------------------------------------------------
void DrawHintModal(App& app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ToU32(ImVec4(0, 0, 0, 0.62f)));

    float cw = Min(S(620.0f), ws.x * 0.56f);
    const char* hintText = app.level.hint.empty() ? "这一关没有额外提示，观察约束条件即可。"
                                                  : app.level.hint.c_str();
    float hintH = ImGui::CalcTextSize(hintText, nullptr, false, cw - S(56.0f)).y;
    float ch = Min(ws.y - S(140.0f), Max(S(250.0f), S(150.0f) + hintH + S(96.0f)));
    ImVec2 c0(wp.x + (ws.x - cw) * 0.5f, wp.y + (ws.y - ch) * 0.5f);
    ImVec2 c1(c0.x + cw, c0.y + ch);
    DrawCard(dl, c0, c1, S(18.0f), ToU32(col::Panel), ToU32(col::Border));

    dl->AddText(gFontHeading, S(26.0f), ImVec2(c0.x + S(26.0f), c0.y + S(22.0f)), ToU32(col::Amber),
                "提示");
    // 灯泡图标
    DrawIcon(dl, Icon::Bulb, ImVec2(c1.x - S(44.0f), c0.y + S(40.0f)), S(18.0f),
             ToU32(Alpha(col::Amber, 0.9f)), S(2.0f));
    dl->AddLine(ImVec2(c0.x + S(24.0f), c0.y + S(62.0f)),
                ImVec2(c1.x - S(24.0f), c0.y + S(62.0f)), ToU32(col::Border), S(1.0f));

    ImGui::SetCursorScreenPos(ImVec2(c0.x + S(26.0f), c0.y + S(78.0f)));
    ImGui::PushTextWrapPos(c1.x - S(26.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, col::Text);
    ImGui::TextUnformatted(hintText);
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();

    // 参考解（可选显示）
    float btnY = c1.y - S(66.0f);
    ImGui::SetCursorScreenPos(ImVec2(c0.x + S(26.0f), btnY));
    if (FunButton("hint_toggle_ref", app.referenceOpen ? "隐藏参考解" : "显示参考解",
                  ImVec2(S(150.0f), S(42.0f)), StyleGhost(), true,
                  app.referenceOpen ? Icon::Cross : Icon::Sparkle)) {
        app.referenceOpen = !app.referenceOpen;
    }
    if (app.referenceOpen) {
        std::string ref = app.level.reference.empty() ? "（暂无参考解）" : app.level.reference;
        dl->AddText(gFontMono, S(18.0f), ImVec2(c0.x + S(190.0f), btnY + S(10.0f)), ToU32(col::Cyan),
                    Ellipsize(ref, c1.x - c0.x - S(420.0f)).c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(c1.x - S(26.0f) - S(110.0f), btnY));
    if (FunButton("hint_close", "知道了", ImVec2(S(110.0f), S(42.0f)), StyleSecondary())) {
        app.hintOpen = false;
    }
}


// ---------------------------------------------------------------
// 约束卡片列表（正式实现）
// ---------------------------------------------------------------
void DrawConstraintCardsList(App& app, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    DrawCard(dl, p0, p1, S(16.0f), ToU32(col::Panel), ToU32(col::Border));
    ImGui::Dummy(size);

    const float pad = S(18.0f);
    float x = p0.x + pad;
    float y = p0.y + S(16.0f);

    dl->AddText(gFontHeading, S(21.0f), ImVec2(x, y), ToU32(col::Text), "约束条件");
    char cnt[32];
    std::snprintf(cnt, sizeof(cnt), "%d 条", static_cast<int>(app.level.constraints.size()));
    float tw = TextWidth(cnt);
    dl->AddText(gFontBody, S(13.0f), ImVec2(p1.x - pad - tw - S(2.0f), y + S(6.0f)), ToU32(col::TextFaint),
                cnt);
    y += S(30.0f);
    dl->AddText(gFontBody, S(13.0f), ImVec2(x, y), ToU32(col::TextFaint),
                app.hasResult ? "逐条检查结果如下" : "满足全部约束即可通过");
    y += S(24.0f);
    dl->AddLine(ImVec2(x - S(4.0f), y), ImVec2(p1.x - pad + S(4.0f), y), ToU32(col::Border), 1.0f);
    y += S(12.0f);

    // 底部预留「关卡导引」卡片（小窗口时隐藏，优先显示约束）
    const bool compactPanel = UiIsCompact();
    const float infoH = compactPanel ? 0.0f : S(118.0f);
    float listH = p1.y - y - infoH - (compactPanel ? S(10.0f) : S(24.0f));
    // 结果/提示覆盖层打开时，本列表改为静态绘制：
    // ImGui 子窗口会渲染在父窗口之后，若仍用 BeginChild 会盖住覆盖层。
    const bool overlayOpen = app.hasResult || app.hintOpen;
    const float innerListW = size.x - (x - p0.x) + S(6.0f);
    auto drawRows = [&](ImDrawList* rdl, float itemW) {
        for (size_t i = 0; i < app.level.constraints.size(); ++i) {
            const Constraint& c = app.level.constraints[i];
            bool checked = app.hasResult && i < app.reports.size();
            bool ok = checked && app.reports[i].ok;
            std::string desc = ConstraintDesc(c);

            ImVec2 ip = ImGui::GetCursorScreenPos();
            float textStartX = ip.x + S(42.0f);
            float textW = itemW - S(42.0f) - S(10.0f);
            ImVec2 ts = gFontBody->CalcTextSizeA(S(17.0f), textW, textW, desc.c_str());
            float itemH = ts.y + S(22.0f);
            if (checked && !ok && i < app.reports.size() && !app.reports[i].detail.empty()) {
                itemH += S(26.0f);
            }
            ImVec2 ip1(ip.x + itemW, ip.y + itemH);

            ImU32 bg = ToU32(col::Card);
            ImU32 border = ToU32(col::Border);
            if (checked) {
                bg = ToU32(Alpha(ok ? col::Green : col::Red, 0.055f));
                border = ToU32(Alpha(ok ? col::Green : col::Red, 0.38f));
            }
            rdl->AddRectFilled(ip, ip1, bg, S(11.0f));
            rdl->AddRect(ip, ip1, border, S(11.0f), 0, 1.0f);

            ImVec2 iconCenter(ip.x + S(21.0f), ip.y + S(21.0f));
            if (checked) {
                StatusDot(rdl, iconCenter, S(11.0f), ok);
            } else {
                rdl->AddCircleFilled(iconCenter, S(11.0f), ToU32(Alpha(col::Cyan, 0.12f)));
                rdl->AddCircle(iconCenter, S(11.0f), ToU32(Alpha(col::Cyan, 0.45f)), 20, S(1.2f));
                char num[8];
                std::snprintf(num, sizeof(num), "%d", static_cast<int>(i) + 1);
                ImVec2 nts = gFontBody->CalcTextSizeA(S(12.0f), FLT_MAX, 0.0f, num);
                rdl->AddText(gFontBody, S(12.0f),
                             ImVec2(iconCenter.x - nts.x * 0.5f, iconCenter.y - nts.y * 0.5f),
                             ToU32(col::Cyan), num);
            }

            ImGui::SetCursorScreenPos(ImVec2(textStartX, ip.y + S(10.0f)));
            ImGui::PushTextWrapPos(textStartX + textW);
            ImGui::PushStyleColor(ImGuiCol_Text, ok ? col::Text : (checked ? col::TextDim : col::Text));
            ImGui::TextUnformatted(desc.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();

            if (checked && !ok && i < app.reports.size() && !app.reports[i].detail.empty()) {
                std::string detail = "原因：" + app.reports[i].detail;
                ImGui::SetCursorScreenPos(ImVec2(textStartX, ip.y + ts.y + S(14.0f)));
                ImGui::PushTextWrapPos(textStartX + textW);
                ImGui::PushStyleColor(ImGuiCol_Text, Alpha(col::Red, 0.95f));
                ImGui::TextUnformatted(Ellipsize(detail, textW).c_str());
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
            }
            ImGui::SetCursorScreenPos(ImVec2(ip.x, ip1.y + S(8.0f)));
        }

        if (app.hasResult && !app.reports.empty()) {
            int passed = 0;
            for (const auto& r : app.reports)
                if (r.ok) ++passed;
            ImVec2 sp = ImGui::GetCursorScreenPos();
            float sw = itemW;
            ImVec2 sp1(sp.x + sw, sp.y + S(42.0f));
            bool allOk = passed == static_cast<int>(app.reports.size());
            rdl->AddRectFilled(sp, sp1, ToU32(Alpha(allOk ? col::Green : col::Amber, 0.10f)),
                               S(10.0f));
            rdl->AddText(gFontBody, S(15.0f), ImVec2(sp.x + S(14.0f), sp.y + S(12.0f)),
                         ToU32(allOk ? col::Green : col::Amber),
                         allOk ? "全部约束通过！" : "仍有约束未满足，继续调整表达式");
            ImGui::Dummy(ImVec2(sw, S(42.0f)));
        }
    };

    if (overlayOpen) {
        ImGui::SetCursorScreenPos(ImVec2(x - S(6.0f), y));
        dl->PushClipRect(ImVec2(x - S(6.0f), y), ImVec2(p0.x + innerListW, y + listH), true);
        drawRows(dl, innerListW - S(12.0f));
        dl->PopClipRect();
    } else {
        ImGui::SetCursorScreenPos(ImVec2(x - S(6.0f), y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Sv(6, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, S(8.0f));
        ImGui::BeginChild("##constraint_cards", ImVec2(innerListW, listH), false,
                          ImGuiWindowFlags_NoBackground);
        ImDrawList* cdl = ImGui::GetWindowDrawList();
        drawRows(cdl, ImGui::GetContentRegionAvail().x - S(6.0f));
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

    // 关卡导引（底部信息卡，填充空白并给玩家方向感）
    if (!compactPanel) {
        ImVec2 q0(p0.x + pad - S(2.0f), p1.y - infoH - S(12.0f));
        ImVec2 q1(p1.x - pad + S(2.0f), p1.y - S(12.0f));
        dl->AddRectFilled(q0, q1, ToU32(col::Card), S(12.0f));
        dl->AddRect(q0, q1, ToU32(col::Border), S(12.0f), 0, 1.0f);
        DrawIcon(dl, Icon::Bulb, ImVec2(q0.x + S(22.0f), q0.y + S(24.0f)), S(14.0f),
                 ToU32(Alpha(col::Amber, 0.9f)), S(2.0f));
        dl->AddText(gFontBody, S(15.0f), ImVec2(q0.x + S(36.0f), q0.y + S(14.0f)), ToU32(col::Amber),
                    "关卡导引");
        std::string meta = "章节 " + std::to_string(app.level.chapter) + " · " +
                           std::to_string(app.level.constraints.size()) + " 条约束";
        dl->AddText(gFontBody, S(12.0f), ImVec2(q0.x + S(36.0f), q0.y + S(38.0f)), ToU32(col::TextFaint),
                    meta.c_str());

        std::string intro = app.level.intro;
        if (intro.empty()) {
            intro = app.endless ? "满足全部约束即可进入下一关，每 10 关为 Boss 挑战。"
                                : "满足全部约束即可通关。点击底部「提示」查看本关提示。";
        }
        // 正文区域裁切，避免行数过多时顶到「目标评级」
        dl->PushClipRect(ImVec2(q0.x + S(14.0f), q0.y + S(56.0f)),
                         ImVec2(q1.x - S(14.0f), q1.y - S(24.0f)), true);
        ImGui::SetCursorScreenPos(ImVec2(q0.x + S(16.0f), q0.y + S(56.0f)));
        ImGui::PushTextWrapPos(q1.x - S(16.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(col::TextDim.x, col::TextDim.y, col::TextDim.z, 0.9f));
        ImGui::TextUnformatted(intro.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        dl->PopClipRect();

        // 目标评级
        const char* goal = "目标评级：B 级以上";
        ImVec2 gs = gFontBody->CalcTextSizeA(S(12.0f), FLT_MAX, 0.0f, goal);
        dl->AddText(gFontBody, S(12.0f), ImVec2(q1.x - S(16.0f) - gs.x, q1.y - S(22.0f)),
                    ToU32(Alpha(col::Cyan, 0.75f)), goal);
    }
}

void DrawGame(App& app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    float w = size.x;
    float h = size.y;

    dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h), ToU32(col::Bg));
    // 背景微弱网格
    for (float gx = origin.x; gx < origin.x + w; gx += S(48.0f))
        dl->AddLine(ImVec2(gx, origin.y + S(66.0f)), ImVec2(gx, origin.y + h - S(86.0f)),
                    ToU32(Alpha(col::White, 0.012f)), 1.0f);
    for (float gy = origin.y + S(66.0f); gy < origin.y + h - S(86.0f); gy += S(48.0f))
        dl->AddLine(ImVec2(origin.x, gy), ImVec2(origin.x + w, gy),
                    ToU32(Alpha(col::White, 0.012f)), 1.0f);

    DrawTopBar(app, w);

    // 键盘处理（结果覆盖层打开时由 App::draw 处理）
    if (!app.hasResult && !app.hintOpen) app.handleGameKeyboard();

    const bool compact = UiIsCompact();
    // 内容区：顶栏/底栏之外，最大 1600 逻辑像素并居中（超宽屏不拉散三栏）
    ContentArea ca = MakeContentArea(18.0f, 0.0f, 80.0f, 98.0f, 1600.0f, 1000.0f);
    const float bx = origin.x + S(ca.x);
    const float cw = S(ca.w);
    const float mainH = S(ca.h);
    float top = origin.y + S(ca.y);
    float availW = cw;
    float leftW = compact ? Clamp(cw * 0.32f, S(190.0f), S(250.0f))
                          : Clamp(cw * 0.235f, S(300.0f), S(350.0f));
    float rightW = compact ? 0.0f : Clamp(cw * 0.315f, S(380.0f), S(470.0f));
    float gap = compact ? S(8.0f) : S(14.0f);
    float centerW = availW - leftW - rightW - gap * 2.0f;
    if (!compact && centerW < S(420.0f)) {
        centerW = S(420.0f);
        rightW = availW - leftW - centerW - gap * 2.0f;
    }
    if (centerW < S(260.0f)) centerW = S(260.0f);
    float leftX = bx;
    float centerX = leftX + leftW + gap;
    float rightX = centerX + centerW + gap;

    // 左：约束条件
    ImGui::SetCursorScreenPos(ImVec2(leftX, top));
    DrawConstraintCardsList(app, ImVec2(leftW, mainH));

    // 中：表达式 + 键盘
    float exprH = compact ? S(104.0f) : S(122.0f);
    ImGui::SetCursorScreenPos(ImVec2(centerX, top));
    DrawExprCard(app, ImVec2(centerW, exprH));
    float keypadTop = top + exprH + S(12.0f);
    float tipH = S(20.0f);
    float keypadH = (top + mainH) - keypadTop - tipH - S(6.0f);
    DrawKeypad(app, ImVec2(centerX, keypadTop), centerW, keypadH);

    // 键盘下方提示
    {
        const char* tip = app.isComplexLevel()
                              ? "变量 z 取复数 · i 是虚数单位 · Re/Im/conj/arg 可用"
                              : "数字 / 字母可直接键入 · 函数键自动补全括号";
        ImVec2 ts = gFontBody->CalcTextSizeA(S(12.0f), FLT_MAX, 0.0f, tip);
        dl->AddText(gFontBody, S(12.0f),
                    ImVec2(centerX + (centerW - ts.x) * 0.5f, top + mainH - S(16.0f)),
                    ToU32(col::TextFaint), tip);
    }

    // 右：函数图像（实数域曲线 / 复数域着色图；小窗口隐藏）
    if (rightW > 200.0f) {
        ImGui::SetCursorScreenPos(ImVec2(rightX, top));
        if (app.isComplexLevel())
            DrawComplexPlotCard(app, ImVec2(rightW, mainH));
        else
            DrawPlotCard(app, ImVec2(rightW, mainH));
    }

    DrawBottomBar(app, w);
}

}  // namespace ui
}  // namespace fcg
