// LevelSelect.cpp — 关卡选择界面（按章节分组的关卡列表 + 关卡详情 + 进度总览）
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "App.h"
#include "LevelUi.h"
#include "Theme.h"
#include "imgui.h"

namespace fcg {
namespace ui {

namespace {

constexpr float kPi = 3.14159265358979323846;

struct ChapterInfo {
    int id = 1;
    const char* name = "基础";
    const char* desc = "";
};

const ChapterInfo* ChapterOf(int chapter) {
    static const ChapterInfo chapters[] = {
        {1, "基础", "熟悉奇偶性、定义域与基本取值"},
        {2, "组合", "单调、值域、周期——把约束叠加起来"},
        {3, "进阶", "解析性、渐近线与开放约束"},
        {4, "大师", "多重约束下的极限构造"},
        {5, "扩展", "更多函数库与开放题型"},
        {6, "终章", "综合挑战"},
    };
    for (const auto& c : chapters)
        if (c.id == chapter) return &c;
    return &chapters[0];
}

const char* ChapterRoman(int chapter) {
    switch (chapter) {
        case 1: return "第一章";
        case 2: return "第二章";
        case 3: return "第三章";
        case 4: return "第四章";
        case 5: return "第五章";
        default: return "第六章";
    }
}

void DrawTopBar(App& app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    float w = ImGui::GetWindowSize().x;
    float barH = S(74.0f);

    dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + barH), ToU32(col::Panel));
    dl->AddLine(ImVec2(origin.x, origin.y + barH), ImVec2(origin.x + w, origin.y + barH),
                ToU32(col::Border), 1.0f);

    // 顶栏元素与内容区左右边界对齐（超宽屏不会贴到屏幕边缘）
    ContentArea ca = MakeContentArea(24.0f, 0.0f, 0.0f, 0.0f, 1600.0f, 1000.0f);
    const float bx = origin.x + S(ca.x);
    const float cxr = bx + S(ca.w);

    // 返回
    ImGui::SetCursorScreenPos(ImVec2(bx, origin.y + S(17.0f)));
    if (FunButton("ls_back", "返回", ImVec2(S(96.0f), S(40.0f)), StyleSecondary(), true, Icon::Left)) {
        app.goMenu();
    }

    // 标题
    dl->AddText(gFontHeading, S(27.0f), ImVec2(bx + S(118.0f), origin.y + S(12.0f)),
                ToU32(col::Text), "关卡模式");
    dl->AddText(gFontBody, S(14.0f), ImVec2(bx + S(120.0f), origin.y + S(46.0f)),
                ToU32(col::TextFaint), "选择关卡，满足约束即可通关");

    // 右侧进度
    int cleared = 0;
    for (const auto& e : app.levels)
        if (e.cleared) ++cleared;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "已通过 %d / %d", cleared,
                  static_cast<int>(app.levels.size()));
    ImVec2 ts = gFontBody->CalcTextSizeA(S(16.0f), FLT_MAX, 0.0f, buf);
    float rightX = cxr - S(2.0f);
    dl->AddCircleFilled(ImVec2(rightX - ts.x - S(16.0f), origin.y + S(37.0f)), S(4.0f),
                        ToU32(col::Green));
    dl->AddText(gFontBody, S(16.0f), ImVec2(rightX - ts.x, origin.y + S(28.0f)), ToU32(col::TextDim),
                buf);

    // 限时开关
    ImGui::SetCursorScreenPos(ImVec2(cxr - S(356.0f), origin.y + S(23.0f)));
    ToggleSwitch("ls_timeattack", &app.timeAttack, "限时模式", nullptr);
}

void DrawLevelItem(App& app, int index, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float h = S(82.0f);
    ImVec2 p0 = pos;
    ImVec2 p1(pos.x + width, pos.y + h);
    ImGui::PushID(index);
    ImGui::InvisibleButton("##lv", ImVec2(width, h));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImGui::PopID();

    LevelEntry& entry = app.levels[static_cast<size_t>(index)];
    if (hovered) app.hoveredLevel = index;

    ImU32 bg = ToU32(hovered ? col::CardHi : col::Card);
    dl->AddRectFilled(p0, p1, bg, S(12.0f));
    dl->AddRect(p0, p1, ToU32(hovered ? ImVec4(col::Cyan.x, col::Cyan.y, col::Cyan.z, 0.55f) : col::Border),
                    S(12.0f), 0, 1.0f);
    if (hovered) {
        dl->AddRectFilled(ImVec2(p0.x, p0.y + S(12.0f)), ImVec2(p0.x + S(3.0f), p1.y - S(12.0f)),
                          ToU32(col::Cyan), S(2.0f));
    }

    // 序号
    char idx[8];
    std::snprintf(idx, sizeof(idx), "%02d", index + 1);
    dl->AddText(gFontMono, S(22.0f), ImVec2(p0.x + S(16.0f), p0.y + S(18.0f)),
                ToU32(entry.cleared ? col::Cyan : col::TextFaint), idx);

    // 标题
    std::string title = Ellipsize(entry.def.title, width - S(190.0f));
    dl->AddText(gFontBody, S(19.0f), ImVec2(p0.x + S(60.0f), p0.y + S(14.0f)), ToU32(col::Text),
                title.c_str());
    // 约束数量
    char sub[64];
    std::snprintf(sub, sizeof(sub), "%d 条约束 · %s", static_cast<int>(entry.def.constraints.size()),
                  ChapterRoman(entry.def.chapter));
    dl->AddText(gFontBody, S(13.0f), ImVec2(p0.x + S(61.0f), p0.y + S(44.0f)), ToU32(col::TextFaint),
                sub);

    // 评级
    if (entry.cleared && entry.bestGrade) {
        const ImVec4& gc = GradeColor(entry.bestGrade);
        ImVec2 gp(p1.x - S(56.0f), p0.y + S(16.0f));
        ImVec2 gp1(gp.x + S(38.0f), gp.y + S(38.0f));
        dl->AddRectFilled(gp, gp1, GradeBg(entry.bestGrade), S(9.0f));
        dl->AddRect(gp, gp1, ToU32(ImVec4(gc.x, gc.y, gc.z, 0.55f)), S(9.0f), 0, 1.0f);
        const char* gname = GradeName(entry.bestGrade);
        ImVec2 gts = gFontScore->CalcTextSizeA(S(24.0f), FLT_MAX, 0.0f, gname);
        dl->AddText(gFontScore, S(24.0f),
                    ImVec2(gp.x + (S(38.0f) - gts.x) * 0.5f, gp.y + (S(38.0f) - gts.y) * 0.5f),
                    ToU32(gc), gname);
    } else {
        const char* t = "未通过";
        ImVec2 ts = gFontBody->CalcTextSizeA(S(13.0f), FLT_MAX, 0.0f, t);
        dl->AddText(gFontBody, S(13.0f), ImVec2(p1.x - ts.x - S(16.0f), p0.y + S(26.0f)),
                    ToU32(col::TextFaint), t);
    }

    if (hovered) {
        DrawIcon(dl, Icon::Right, ImVec2(p1.x - S(18.0f), p1.y - S(20.0f)), S(12.0f),
                 ToU32(Alpha(col::Cyan, 0.8f)), S(2.0f));
    }

    if (clicked) {
        app.openLevel(index);
    }
}

void DrawDetailPanel(App& app, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + width, p0.y + height);
    DrawCard(dl, p0, p1, S(16.0f), ToU32(col::Panel), ToU32(col::Border));
    ImGui::Dummy(ImVec2(width, height));

    if (app.levels.empty()) return;
    int idx = app.hoveredLevel;
    if (idx < 0 || idx >= static_cast<int>(app.levels.size())) idx = 0;
    const LevelEntry& entry = app.levels[static_cast<size_t>(idx)];
    const LevelDef& def = entry.def;
    const ChapterInfo* ch = ChapterOf(def.chapter);

    float x = p0.x + S(30.0f);
    float y = p0.y + S(22.0f);
    float innerW = width - S(60.0f);

    // 章节标签 + 评级
    char chap[80];
    std::snprintf(chap, sizeof(chap), "%s · %s", ChapterRoman(def.chapter), ch->name);
    float cw = gFontBody->CalcTextSizeA(S(13.0f), FLT_MAX, 0.0f, chap).x + S(20.0f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cw, y + S(26.0f)),
                      ToU32(Alpha(col::Amber, 0.12f)), S(13.0f));
    dl->AddText(gFontBody, S(13.0f), ImVec2(x + S(10.0f), y + S(5.0f)), ToU32(col::Amber), chap);
    if (entry.cleared) {
        ImVec2 gp(x + cw + S(10.0f), y + S(2.0f));
        dl->AddRectFilled(gp, ImVec2(gp.x + S(62.0f), gp.y + S(24.0f)), GradeBg(entry.bestGrade),
                          S(12.0f));
        char g[32];
        std::snprintf(g, sizeof(g), "评级 %s", GradeName(entry.bestGrade));
        dl->AddText(gFontBody, S(13.0f), ImVec2(gp.x + S(9.0f), gp.y + S(4.0f)),
                    ToU32(GradeColor(entry.bestGrade)), g);
    }
    y += S(38.0f);

    // 标题（标题字体包含全部关卡标题字符）
    dl->AddText(gFontHeading, S(28.0f), ImVec2(x, y), ToU32(col::Text),
                EllipsizeFont(gFontHeading, S(28.0f), def.title, innerW).c_str());
    y += S(42.0f);

    // 元信息行
    {
        char meta[160];
        std::snprintf(meta, sizeof(meta), "%d 条约束 · 目标评级 B 级以上",
                      static_cast<int>(def.constraints.size()));
        dl->AddText(gFontBody, S(13.0f), ImVec2(x, y), ToU32(col::TextFaint), meta);
        y += S(26.0f);
    }

    // 引导文字
    if (!def.intro.empty()) {
        float introH = ImGui::CalcTextSize(def.intro.c_str(), nullptr, false, innerW).y;
        introH = Min(introH, S(100.0f));
        dl->PushClipRect(ImVec2(x, y), ImVec2(x + innerW, y + introH + S(6.0f)), true);
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::PushTextWrapPos(x + innerW);
        ImGui::PushStyleColor(ImGuiCol_Text, col::TextDim);
        ImGui::TextUnformatted(def.intro.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        dl->PopClipRect();
        y += introH + S(16.0f);
    }

    // 约束列表标题
    dl->AddText(gFontHeading, S(20.0f), ImVec2(x, y), ToU32(col::Text), "约束条件");
    char cnt[32];
    std::snprintf(cnt, sizeof(cnt), "%d 条", static_cast<int>(def.constraints.size()));
    dl->AddText(gFontBody, S(13.0f), ImVec2(x + S(102.0f), y + S(7.0f)), ToU32(col::TextFaint), cnt);
    y += S(34.0f);

    // 动态行高：先测量，再按可用高度决定显示条数
    static thread_local std::vector<float> heights;
    heights.clear();
    for (const auto& c : def.constraints) {
        std::string desc = ConstraintDesc(c);
        float h = gFontBody->CalcTextSizeA(S(16.0f), innerW - S(62.0f), innerW - S(62.0f), desc.c_str()).y +
                  S(22.0f);
        heights.push_back(Max(S(44.0f), h));
    }
    float availH = p1.y - S(100.0f) - y;
    int shown = 0;
    float usedH = 0.0f;
    for (size_t i = 0; i < heights.size(); ++i) {
        if (usedH + heights[i] + S(8.0f) > availH) break;
        usedH += heights[i] + S(8.0f);
        ++shown;
    }
    if (shown == 0 && !heights.empty()) shown = 1;

    for (int i = 0; i < shown; ++i) {
        const Constraint& c = def.constraints[static_cast<size_t>(i)];
        std::string desc = ConstraintDesc(c);
        float itemH = heights[static_cast<size_t>(i)];
        ImVec2 ip0(x, y);
        ImVec2 ip1(x + innerW, y + itemH);
        dl->AddRectFilled(ip0, ip1, ToU32(col::Card), S(10.0f));
        dl->AddRect(ip0, ip1, ToU32(col::Border), S(10.0f), 0, 1.0f);
        dl->AddCircleFilled(ImVec2(ip0.x + S(24.0f), ip0.y + itemH * 0.5f), S(11.0f),
                            ToU32(Alpha(col::Cyan, 0.14f)));
        char num[8];
        std::snprintf(num, sizeof(num), "%d", i + 1);
        ImVec2 nts = gFontBody->CalcTextSizeA(S(13.0f), FLT_MAX, 0.0f, num);
        dl->AddText(gFontBody, S(13.0f),
                    ImVec2(ip0.x + S(24.0f) - nts.x * 0.5f, ip0.y + itemH * 0.5f - nts.y * 0.5f),
                    ToU32(col::Cyan), num);
        ImGui::SetCursorScreenPos(ImVec2(ip0.x + S(46.0f), ip0.y + S(9.0f)));
        ImGui::PushTextWrapPos(ip0.x + innerW - S(16.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col::Text);
        ImGui::TextUnformatted(desc.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        y += itemH + S(8.0f);
    }
    if (shown < static_cast<int>(def.constraints.size())) {
        char more[80];
        std::snprintf(more, sizeof(more), "…… 还有 %d 条约束",
                      static_cast<int>(def.constraints.size()) - shown);
        dl->AddText(gFontBody, S(13.0f), ImVec2(x + S(4.0f), y + S(4.0f)), ToU32(col::TextFaint), more);
    }

    // 评分标准（填补详情卡空白）
    if (p1.y - S(86.0f) - y > 78.0f) {
        dl->AddText(gFontHeading, S(18.0f), ImVec2(x, y), ToU32(col::Text), "评分维度");
        y += S(30.0f);
        struct Dim {
            const char* name;
            int weight;
            const ImVec4* color;
        };
        Dim dims[4] = {{"精确度", 35, &col::Cyan},
                       {"优雅度", 30, &col::Amber},
                       {"鲁棒性", 25, &col::Green},
                       {"时间", 10, &col::Purple}};
        float cardW = (innerW - S(30.0f)) / 4.0f;
        float cx = x;
        for (int i = 0; i < 4; ++i) {
            ImVec2 q0(cx, y);
            ImVec2 q1(cx + cardW, y + S(44.0f));
            dl->AddRectFilled(q0, q1, ToU32(col::Card), S(9.0f));
            dl->AddRect(q0, q1, ToU32(Alpha(*dims[i].color, 0.22f)), S(9.0f), 0, 1.0f);
            dl->AddText(gFontBody, S(14.0f), ImVec2(q0.x + S(12.0f), q0.y + S(6.0f)), ToU32(col::TextDim),
                        dims[i].name);
            char wbuf[16];
            std::snprintf(wbuf, sizeof(wbuf), "%d%%", dims[i].weight);
            ImVec2 ws2 = gFontBody->CalcTextSizeA(S(15.0f), FLT_MAX, 0.0f, wbuf);
            dl->AddText(gFontBody, S(15.0f), ImVec2(q0.x + cardW - S(12.0f) - ws2.x, q0.y + S(21.0f)),
                        ToU32(*dims[i].color), wbuf);
            cx += cardW + S(10.0f);
        }
        y += S(58.0f);
    }

    // 开始挑战 + 提示
    ImGui::SetCursorScreenPos(ImVec2(x, p1.y - S(74.0f)));
    if (FunButton("ls_start", "开始挑战", ImVec2(S(190.0f), S(50.0f)), StylePrimary(), true,
                  Icon::Play)) {
        app.openLevel(idx);
    }
    ImGui::SetCursorScreenPos(ImVec2(x + S(208.0f), p1.y - S(64.0f)));
    ImGui::PushTextWrapPos(p1.x - S(26.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, col::TextFaint);
    ImGui::TextUnformatted("通关条件：满足全部约束。评级取决于表达式复杂度、鲁棒性与用时。");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
}

void DrawProgressCard(App& app, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + width, p0.y + height);
    DrawCard(dl, p0, p1, S(16.0f), ToU32(col::Panel), ToU32(col::Border));
    ImGui::Dummy(ImVec2(width, height));

    float x = p0.x + S(26.0f);
    float y = p0.y + S(13.0f);
    dl->AddText(gFontHeading, S(19.0f), ImVec2(x, y), ToU32(col::Text), "进度总览");
    y += S(32.0f);

    int total = static_cast<int>(app.levels.size());
    int cleared = 0;
    int grades[5] = {0, 0, 0, 0, 0};
    for (const auto& e : app.levels) {
        if (!e.cleared) continue;
        ++cleared;
        switch (e.bestGrade) {
            case 'S': ++grades[0]; break;
            case 'A': ++grades[1]; break;
            case 'B': ++grades[2]; break;
            case 'C': ++grades[3]; break;
            default: ++grades[4]; break;
        }
    }
    float ratio = total > 0 ? static_cast<float>(cleared) / total : 0.0f;

    // 总进度
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d / %d", cleared, total);
    ImVec2 b0(x, y + S(4.0f));
    ImVec2 b1(x + width - S(150.0f), y + S(14.0f));
    dl->AddRectFilled(b0, b1, ToU32(Alpha(col::White, 0.06f)), S(5.0f));
    if (ratio > 0)
        dl->AddRectFilled(b0, ImVec2(b0.x + (b1.x - b0.x) * ratio, b1.y), ToU32(col::Cyan), S(5.0f));
    ImVec2 bs = gFontBody->CalcTextSizeA(S(14.0f), FLT_MAX, 0.0f, buf);
    dl->AddText(gFontBody, S(14.0f), ImVec2(p0.x + width - S(26.0f) - bs.x, y - 1.0f),
                ToU32(col::TextDim), buf);
    y += S(28.0f);

    // 评级分布
    static const char* names[5] = {"S", "A", "B", "C", "D"};
    static const char gradesCh[5] = {'S', 'A', 'B', 'C', 'D'};
    float bx = x;
    for (int i = 0; i < 5; ++i) {
        ImVec2 q0(bx, y);
        ImVec2 q1(bx + S(50.0f), y + S(40.0f));
        dl->AddRectFilled(q0, q1, ToU32(col::Card), S(9.0f));
        dl->AddRect(q0, q1, ToU32(col::Border), S(9.0f), 0, 1.0f);
        ImVec2 ts = gFontScore->CalcTextSizeA(S(18.0f), FLT_MAX, 0.0f, names[i]);
        dl->AddText(gFontScore, S(18.0f), ImVec2(q0.x + (S(50.0f) - ts.x) * 0.5f, q0.y + 1.0f),
                    ToU32(grades[i] > 0 ? GradeColor(gradesCh[i]) : col::TextFaint), names[i]);
        char cn[8];
        std::snprintf(cn, sizeof(cn), "×%d", grades[i]);
        ImVec2 cs = gFontBody->CalcTextSizeA(S(10.5f), FLT_MAX, 0.0f, cn);
        dl->AddText(gFontBody, S(10.5f), ImVec2(q0.x + (S(50.0f) - cs.x) * 0.5f, q0.y + S(23.0f)),
                    ToU32(col::TextFaint), cn);
        bx += S(56.0f);
    }
    y += S(48.0f);

    // 章节进度（单行紧凑布局）
    dl->AddLine(ImVec2(x, y - S(6.0f)), ImVec2(p1.x - S(26.0f), y - S(6.0f)), ToU32(col::Border), 1.0f);
    int maxChapter = 1;
    for (const auto& e : app.levels) maxChapter = Max(maxChapter, e.def.chapter);
    for (int ch = 1; ch <= maxChapter; ++ch) {
        int chTotal = 0, chCleared = 0;
        for (const auto& e : app.levels) {
            if (e.def.chapter != ch) continue;
            ++chTotal;
            if (e.cleared) ++chCleared;
        }
        if (chTotal == 0) continue;
        const ChapterInfo* info = ChapterOf(ch);
        char line[64];
        std::snprintf(line, sizeof(line), "%s · %s", ChapterRoman(ch), info->name);
        dl->AddText(gFontBody, S(13.5f), ImVec2(x, y), ToU32(col::TextDim), line);
        // 进度条（章节名右侧）
        float barX = x + S(158.0f);
        float barW = Max(S(80.0f), width - S(158.0f) - S(130.0f));
        ImVec2 s0(barX, y + S(9.0f));
        ImVec2 s1(barX + barW, y + S(15.0f));
        dl->AddRectFilled(s0, s1, ToU32(Alpha(col::White, 0.06f)), S(3.0f));
        float r = chTotal > 0 ? static_cast<float>(chCleared) / chTotal : 0.0f;
        if (r > 0)
            dl->AddRectFilled(s0, ImVec2(s0.x + (s1.x - s0.x) * r, s1.y),
                              ToU32(chCleared == chTotal ? col::Green : col::Cyan), S(3.0f));
        char num[32];
        std::snprintf(num, sizeof(num), "%d / %d", chCleared, chTotal);
        ImVec2 ns = gFontBody->CalcTextSizeA(S(12.0f), FLT_MAX, 0.0f, num);
        dl->AddText(gFontBody, S(12.0f), ImVec2(p1.x - S(26.0f) - ns.x, y + 1.0f),
                    ToU32(chCleared == chTotal ? col::Green : col::TextFaint), num);
        y += S(24.0f);
        if (y > p1.y - S(20.0f)) break;
    }
}

}  // namespace

void DrawLevelSelect(App& app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    float w = size.x;
    float h = size.y;

    // 背景
    dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h), ToU32(col::Bg));
    DrawTopBar(app);

    if (app.hoveredLevel < 0) app.hoveredLevel = 0;

    const bool compact = UiIsCompact();
    // 内容区：顶栏以下，最大 1600 逻辑像素并居中
    ContentArea ca = MakeContentArea(24.0f, 0.0f, 90.0f, 24.0f, 1600.0f, 1000.0f);
    float top = origin.y + S(ca.y);
    float bottom = origin.y + S(ca.y + ca.h);
    float contentH = bottom - top;
    const float bx = origin.x + S(ca.x);
    const float cw = S(ca.w);

    // ---------- 左侧关卡列表 ----------
    float leftW = compact ? cw : Clamp(cw * 0.31f, S(360.0f), S(460.0f));
    float listX = bx;
    ImVec2 listP0(listX, top);
    ImVec2 listP1(listX + leftW, bottom);
    DrawCard(dl, listP0, listP1, S(16.0f), ToU32(col::Panel), ToU32(col::Border));
    dl->AddText(gFontHeading, S(21.0f), ImVec2(listP0.x + S(22.0f), listP0.y + S(16.0f)),
                ToU32(col::Text), "关卡列表");
    char all[32];
    std::snprintf(all, sizeof(all), "共 %d 关", static_cast<int>(app.levels.size()));
    dl->AddText(gFontBody, S(13.0f), ImVec2(listP1.x - S(22.0f) - TextWidth(all), listP0.y + S(22.0f)),
                ToU32(col::TextFaint), all);
    dl->AddLine(ImVec2(listP0.x + S(18.0f), listP0.y + S(52.0f)),
                ImVec2(listP1.x - S(18.0f), listP0.y + S(52.0f)), ToU32(col::Border), 1.0f);

    ImGui::SetCursorScreenPos(ImVec2(listP0.x + S(14.0f), listP0.y + S(62.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Sv(6, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, S(8.0f));
    ImGui::BeginChild("##level_list", ImVec2(leftW - S(28.0f), contentH - S(74.0f)), false,
                      ImGuiWindowFlags_NoBackground);
        ImDrawList* cdl = ImGui::GetWindowDrawList();  // 子窗口裁剪用
    {
        int lastChapter = -1;
        float innerW = ImGui::GetContentRegionAvail().x;
        for (size_t i = 0; i < app.levels.size(); ++i) {
            const LevelDef& def = app.levels[i].def;
            if (def.chapter != lastChapter) {
                lastChapter = def.chapter;
                const ChapterInfo* info = ChapterOf(def.chapter);
                char title[96];
                std::snprintf(title, sizeof(title), "%s · %s", ChapterRoman(def.chapter),
                              info->name);
                cdl->AddText(gFontBody, S(16.0f), ImGui::GetCursorScreenPos(), ToU32(col::TextDim),
                            title);
                // 章说明
                ImVec2 p = ImGui::GetCursorScreenPos();
                cdl->AddText(gFontBody, S(12.0f), ImVec2(p.x, p.y + S(22.0f)), ToU32(col::TextFaint),
                            info->desc);
                ImGui::Dummy(ImVec2(innerW, S(48.0f)));
            }
            DrawLevelItem(app, static_cast<int>(i), innerW);
            ImGui::Dummy(ImVec2(1.0f, S(4.0f)));
        }
        // 底部留白
        ImGui::Dummy(ImVec2(1.0f, S(8.0f)));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    // ---------- 右侧：详情 + 进度（小窗口隐藏，只保留列表） ----------
    if (compact) return;
    float rightX = listP1.x + S(18.0f);
    float rightW = bx + cw - rightX;
    float detailH = contentH * 0.63f;
    ImGui::SetCursorScreenPos(ImVec2(rightX, top));
    DrawDetailPanel(app, rightW, detailH);
    ImGui::SetCursorScreenPos(ImVec2(rightX, top + detailH + S(18.0f)));
    DrawProgressCard(app, rightW, contentH - detailH - S(18.0f));
}

}  // namespace ui
}  // namespace fcg
