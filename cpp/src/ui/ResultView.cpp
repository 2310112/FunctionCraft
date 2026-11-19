// ResultView.cpp — 提交结果覆盖层：总分 / 评级 / 四维评分 / 约束检查 / 操作按钮
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

bool AllStubDetails(const std::vector<ConstraintReport>& reports) {
    if (reports.empty()) return false;
    for (const auto& r : reports)
        if (r.detail != "未实现" && r.detail != "未实现。") return false;
    return true;
}

}  // namespace

void DrawResultOverlay(App& app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ToU32(ImVec4(0, 0, 0, 0.70f)));

    // 是否同帧刚打开（截图模式下保证动画/布局稳定，这里无需处理）
    const bool compact = UiIsCompact();
    float cw = compact ? Min(S(820.0f), ws.x - S(36.0f)) : Min(S(820.0f), ws.x - S(120.0f));
    // 非紧凑模式给足高度，尽量一屏展示全部约束（6 条约需 780px）
    float ch = compact ? Min(S(660.0f), ws.y - S(44.0f)) : Min(S(800.0f), ws.y - S(56.0f));
    ImVec2 c0(wp.x + (ws.x - cw) * 0.5f, wp.y + (ws.y - ch) * 0.5f);
    ImVec2 c1(c0.x + cw, c0.y + ch);
    DrawCard(dl, c0, c1, S(20.0f), ToU32(col::Panel), ToU32(col::Border));

    char grade = app.result.grade ? app.result.grade : (app.correct ? 'B' : 'D');
    if (app.correct && (grade == 'D' || grade == 0)) grade = 'B';
    const ImVec4& gc = GradeColor(grade);
    const ImVec4& scoreCol = app.correct ? gc : col::TextDim;

    float pad = compact ? S(18.0f) : S(34.0f);
    float innerW = cw - pad * 2.0f;
    float y = c0.y + (compact ? S(16.0f) : S(30.0f));

    // ---------- 头部：评级圆 + 总分 ----------
    float gradeR = compact ? S(32.0f) : S(48.0f);
    ImVec2 circleCenter(c0.x + pad + gradeR, y + gradeR);
    dl->AddCircleFilled(circleCenter, gradeR, GradeBg(grade), 48);
    dl->AddCircle(circleCenter, gradeR, ToU32(ImVec4(gc.x, gc.y, gc.z, 0.75f)), 48, S(2.2f));
    {
        const char* gname = GradeName(grade);
        float gs = compact ? S(34.0f) : S(52.0f);
        ImVec2 ts = gFontScore->CalcTextSizeA(gs, FLT_MAX, 0.0f, gname);
        dl->AddText(gFontScore, gs,
                    ImVec2(circleCenter.x - ts.x * 0.5f, circleCenter.y - ts.y * 0.5f),
                    ToU32(gc), gname);
    }
    float tx = circleCenter.x + gradeR + (compact ? S(16.0f) : S(20.0f));
    const char* modeLabel = app.endless ? "无尽模式 · 本次评级"
                                        : (app.isComplexLevel() ? "复数域 · 本次评级" : "本次评级");
    dl->AddText(gFontBody, S(13.0f), ImVec2(tx, y + S(6.0f)), ToU32(col::TextFaint), modeLabel);
    dl->AddText(gFontHeading, S(30.0f), ImVec2(tx, y + S(22.0f)), ToU32(gc), GradeLabel(grade));
    if (app.isComplexLevel()) {
        const char* vlab = "f(z)";
        float vw = gFontBody->CalcTextSizeA(S(13.0f), FLT_MAX, 0.0f, vlab).x;
        float lw = gFontBody->CalcTextSizeA(S(30.0f), FLT_MAX, 0.0f, GradeLabel(grade)).x;
        ImVec2 b0(tx + lw + S(12.0f), y + S(28.0f));
        ImVec2 b1(b0.x + vw + S(16.0f), b0.y + S(24.0f));
        dl->AddRectFilled(b0, b1, ToU32(Alpha(col::Purple, 0.16f)), S(6.0f));
        dl->AddText(gFontBody, S(13.0f), ImVec2(b0.x + S(8.0f), b0.y + S(4.0f)),
                    ToU32(col::Purple), vlab);
    }

    // 总分（右上）
    {
        char total[32];
        std::snprintf(total, sizeof(total), "%.0f", app.result.total);
        float scoreSize = compact ? S(40.0f) : S(64.0f);
        ImVec2 ts = gFontScore->CalcTextSizeA(scoreSize, FLT_MAX, 0.0f, total);
        float scx = c1.x - pad - ts.x - S(54.0f);
        dl->AddText(gFontScore, scoreSize, ImVec2(scx, y + S(4.0f)), ToU32(scoreCol), total);
        dl->AddText(gFontBody, compact ? S(15.0f) : S(18.0f),
                    ImVec2(scx + ts.x + S(6.0f), y + (compact ? S(18.0f) : S(34.0f))),
                    ToU32(col::TextDim), "分");
        dl->AddText(gFontBody, S(11.0f), ImVec2(scx + ts.x + S(6.0f), y + (compact ? S(34.0f) : S(56.0f))),
                    ToU32(col::TextFaint), "/ 100");
    }

    y += compact ? S(82.0f) : S(104.0f);
    // ---------- 状态横幅 ----------
    {
        ImVec2 b0(c0.x + pad, y);
        ImVec2 b1(c1.x - pad, y + (compact ? S(34.0f) : S(42.0f)));
        ImU32 bg = ToU32(Alpha(app.correct ? col::Green : col::Red, 0.10f));
        ImU32 fg = ToU32(app.correct ? col::Green : col::Red);
        dl->AddRectFilled(b0, b1, bg, S(10.0f));
        dl->AddRect(b0, b1, ToU32(Alpha(app.correct ? col::Green : col::Red, 0.32f)), S(10.0f), 0,
                    1.0f);
        float bannerMid = compact ? S(17.0f) : S(21.0f);
        if (app.correct) {
            DrawIcon(dl, Icon::Check, ImVec2(b0.x + S(24.0f), b0.y + bannerMid), S(15.0f), fg, S(2.2f));
            dl->AddText(gFontBody, compact ? S(14.0f) : S(16.0f),
                        ImVec2(b0.x + S(40.0f), b0.y + (compact ? S(6.0f) : S(11.0f))), fg,
                        app.endless ? "挑战成功！可以继续下一关" : "全部约束通过！");
        } else {
            DrawIcon(dl, Icon::Cross, ImVec2(b0.x + S(24.0f), b0.y + bannerMid), S(14.0f), fg, S(2.2f));
            std::string msg = "未通过全部约束，调整表达式后重试";
            if (app.timeUp) msg = "时间耗尽，本次挑战失败";
            dl->AddText(gFontBody, compact ? S(14.0f) : S(16.0f),
                        ImVec2(b0.x + S(40.0f), b0.y + (compact ? S(6.0f) : S(11.0f))), fg, msg.c_str());
        }
        // 右侧信息
        char info[160];
        if (app.endless) {
            std::snprintf(info, sizeof(info), "第 %d 关 · 用时 %s", app.endlessIndex,
                          FormatTime(app.submitElapsed).c_str());
        } else {
            std::snprintf(info, sizeof(info), "用时 %s · 节点 %d · 函数调用 %d",
                          FormatTime(app.submitElapsed).c_str(),
                          app.ast ? nodeCount(app.ast) : 0,
                          app.ast ? funcCallCount(app.ast) : 0);
        }
        if (!compact) {
            float tw = gFontBody->CalcTextSizeA(S(13.0f), FLT_MAX, 0.0f, info).x;
            dl->AddText(gFontBody, S(13.0f), ImVec2(b1.x - S(16.0f) - tw, b0.y + S(14.0f)),
                        ToU32(col::TextDim), info);
        }
    }
    y += compact ? S(44.0f) : S(58.0f);

    // ---------- 四维评分（紧凑模式隐藏，保证约束与按钮可见） ----------
    if (!compact)
    dl->AddText(gFontHeading, S(19.0f), ImVec2(c0.x + pad, y), ToU32(col::Text), "评分维度");
    if (!compact) {
    y += S(32.0f);
    {
        struct Dim {
            const char* label;
            double value;
            const ImVec4* color;
        };
        // 未通过时维度不参与计分，统一显示为 0 / 灰色（避免误导玩家）
        Dim dims[4] = {{"精确度", app.correct ? app.result.precision : 0.0, &col::Cyan},
                       {"优雅度", app.correct ? app.result.elegance : 0.0, &col::Amber},
                       {"鲁棒性", app.correct ? app.result.robustness : 0.0, &col::Green},
                       {"时间", app.correct ? app.result.speed : 0.0, &col::Purple}};
        for (int i = 0; i < 4; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(c0.x + pad, y));
            const ImVec4& barColor = app.correct ? *(dims[i].color) : col::TextFaint;
            ScoreBar(dims[i].label, static_cast<float>(dims[i].value / 100.0), barColor,
                     ImVec2(innerW, S(24.0f)));
            y += S(30.0f);
        }
    }
    y += S(2.0f);
    }

    // 反馈提示
    if (!compact && !app.result.feedback.empty()) {
        for (size_t i = 0; i < app.result.feedback.size() && i < 2; ++i) {
            std::string fb = "· " + app.result.feedback[i];
            dl->AddText(gFontBody, S(13.0f), ImVec2(c0.x + pad, y), ToU32(col::Amber),
                        Ellipsize(fb, innerW).c_str());
            y += S(20.0f);
        }
        y += S(4.0f);
    }
    if (AllStubDetails(app.reports)) {
        dl->AddText(gFontBody, S(13.0f), ImVec2(c0.x + pad, y), ToU32(col::AmberHi),
                    "提示：约束判定引擎尚未就绪，本次结果仅用于界面演示。");
        y += S(22.0f);
    }

    // ---------- 约束检查 ----------
    float btnH = compact ? S(44.0f) : S(54.0f);
    float buttonsY = c1.y - btnH - (compact ? S(16.0f) : S(26.0f));
    if (!app.reports.empty() && compact) {
        // 紧凑模式：逐条紧凑列出（放不下才截断），避免大片空白
        int passed = 0;
        for (const auto& r : app.reports)
            if (r.ok) ++passed;
        char pass[64];
        std::snprintf(pass, sizeof(pass), "%d / %d 通过", passed,
                      static_cast<int>(app.reports.size()));
        dl->AddText(gFontBody, S(16.0f), ImVec2(c0.x + pad, y), ToU32(col::Text), "约束检查");
        float pw = gFontBody->CalcTextSizeA(S(13.0f), FLT_MAX, 0.0f, pass).x;
        dl->AddText(gFontBody, S(13.0f), ImVec2(c1.x - pad - pw, y + S(2.0f)),
                    ToU32(passed == static_cast<int>(app.reports.size()) ? col::Green : col::Red),
                    pass);
        y += S(24.0f);
        const float limitY = buttonsY - S(6.0f);
        for (const auto& r : app.reports) {
            if (y > limitY - S(18.0f)) break;
            std::string label = r.label.empty() ? std::string("约束") : r.label;
            // 注意：字体不含 ✓/✗ 字形，用颜色 + "通过/未通过" 表达
            std::string head = label;
            dl->AddText(gFontBody, S(13.0f), ImVec2(c0.x + pad, y),
                        ToU32(r.ok ? Alpha(col::Green, 0.95f) : Alpha(col::Red, 0.95f)),
                        Ellipsize(r.ok ? head : (head + "（未通过）"), innerW).c_str());
            y += S(19.0f);
            if (!r.ok && !r.detail.empty() && r.detail != "未实现" && y <= limitY - S(18.0f)) {
                dl->AddText(gFontBody, S(12.0f), ImVec2(c0.x + pad + S(12.0f), y),
                            ToU32(col::TextDim),
                            Ellipsize("原因：" + r.detail, innerW - S(12.0f)).c_str());
                y += S(18.0f);
            }
        }
    }
    if (!app.reports.empty() && !compact) {
        dl->AddText(gFontHeading, S(19.0f), ImVec2(c0.x + pad, y), ToU32(col::Text), "约束检查");
        char pass[64];
        int passed = 0;
        for (const auto& r : app.reports)
            if (r.ok) ++passed;
        std::snprintf(pass, sizeof(pass), "%d / %d 通过", passed,
                      static_cast<int>(app.reports.size()));
        float pw = gFontBody->CalcTextSizeA(S(13.0f), FLT_MAX, 0.0f, pass).x;
        dl->AddText(gFontBody, S(13.0f), ImVec2(c1.x - pad - pw, y + S(6.0f)),
                    ToU32(passed == static_cast<int>(app.reports.size()) ? col::Green : col::Red),
                    pass);
        y += S(30.0f);
        float listH = buttonsY - y - S(28.0f);
        if (listH > 40.0f) {
            ImGui::SetCursorScreenPos(ImVec2(c0.x + pad, y));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Sv(2, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, S(8.0f));
            // 内容超高时强制显示滚动条，避免玩家看不出下面还有条目
            float needH = S(4.0f);
            for (const auto& r : app.reports) needH += (r.ok ? S(34.0f) : S(54.0f)) + S(6.0f);
            ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoBackground;
            if (needH > listH) childFlags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
            ImGui::BeginChild("##result_constraints", ImVec2(innerW, listH), false, childFlags);
            ImDrawList* cdl = ImGui::GetWindowDrawList();  // 子窗口裁剪用
            {
                float rowW = ImGui::GetContentRegionAvail().x - S(4.0f);
                for (size_t i = 0; i < app.reports.size(); ++i) {
                    const ConstraintReport& r = app.reports[i];
                    ImVec2 rp = ImGui::GetCursorScreenPos();
                    float rowH = compact ? (r.ok ? S(28.0f) : S(42.0f)) : (r.ok ? S(34.0f) : S(54.0f));
                    ImVec2 rp1(rp.x + rowW, rp.y + rowH);
                    cdl->AddRectFilled(rp, rp1,
                                      ToU32(Alpha(r.ok ? col::Green : col::Red, 0.05f)), S(8.0f));
                    cdl->AddRect(rp, rp1,
                                ToU32(Alpha(r.ok ? col::Green : col::Red, 0.22f)), S(8.0f), 0,
                                1.0f);
                    StatusDot(cdl, ImVec2(rp.x + S(20.0f), rp.y + rowH * 0.5f), S(9.0f), r.ok);
                    std::string label = r.label.empty()
                                            ? ConstraintDesc(app.level.constraints[i])
                                            : r.label;
                    ImGui::SetCursorScreenPos(ImVec2(rp.x + S(40.0f), rp.y + (compact ? S(3.0f) : S(7.0f))));
                    ImGui::PushTextWrapPos(rp.x + rowW - S(10.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, r.ok ? col::Text : col::TextDim);
                    ImGui::TextUnformatted(Ellipsize(label, rowW - S(54.0f)).c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopTextWrapPos();
                    if (!r.ok && !r.detail.empty() && r.detail != "未实现") {
                        cdl->AddText(gFontBody, S(11.0f), ImVec2(rp.x + S(40.0f), rp.y + (compact ? S(23.0f) : S(30.0f))),
                                    ToU32(col::Red), Ellipsize("原因：" + r.detail, rowW - S(54.0f)).c_str());
                    }
                    ImGui::SetCursorScreenPos(ImVec2(rp.x, rp1.y + S(6.0f)));
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
        }
    }

    // ---------- 按钮 ----------
    ImGui::SetCursorScreenPos(ImVec2(c0.x + pad, buttonsY));
    // 返回
    const char* backLabel = app.endless ? "返回菜单" : "返回列表";
    if (FunButton("res_back", backLabel, ImVec2(compact ? S(112.0f) : S(140.0f), btnH), StyleGhost(), true,
                  app.endless ? Icon::Home : Icon::List)) {
        app.hasResult = false;
        app.backFromGame();
    }

    // 重试（通关时作为次要操作）
    if (app.correct) {
        ImGui::SetCursorScreenPos(ImVec2(c1.x - pad - S(340.0f), buttonsY));
        if (FunButton("res_retry", "重试", ImVec2(S(130.0f), btnH), StyleSecondary(), true,
                      Icon::Retry)) {
            app.hasResult = false;
            app.retryLevel();
        }
    }

    // 主行动
    ImGui::SetCursorScreenPos(ImVec2(c1.x - pad - S(194.0f), buttonsY));
    if (app.correct && (app.endless || app.hasNextLevel())) {
        if (FunButton("res_next", "下一关", ImVec2(S(194.0f), btnH), StylePrimary(), true,
                      Icon::Play)) {
            app.hasResult = false;
            app.nextLevel();
        }
    } else if (app.correct) {
        if (FunButton("res_finish", "完成并返回", ImVec2(S(194.0f), btnH), StylePrimary(), true,
                      Icon::Check)) {
            app.hasResult = false;
            app.goLevelSelect();
        }
    } else {
        if (FunButton("res_retry_primary", "重新挑战", ImVec2(S(194.0f), btnH), StylePrimary(), true,
                      Icon::Retry)) {
            app.hasResult = false;
            app.retryLevel();
        }
    }

    // 底部快捷键提示
    if (!compact) {
        const char* tip = app.correct ? "回车 下一关 · Esc 返回" : "回车 重新挑战 · Esc 返回";
        ImVec2 ts = gFontBody->CalcTextSizeA(S(12.0f), FLT_MAX, 0.0f, tip);
        dl->AddText(gFontBody, S(12.0f),
                    ImVec2(c0.x + (cw - ts.x) * 0.5f, c1.y - S(22.0f)), ToU32(col::TextFaint), tip);
    }
}

}  // namespace ui
}  // namespace fcg
