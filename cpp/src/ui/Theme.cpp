// Theme.cpp — 主题实现：字体图集构建、ImGui Style、自绘控件
#include "Theme.h"

#include <cstdarg>
#include <cstdio>
#include <cfloat>
#include <cmath>
#include <algorithm>

#include "imgui.h"
#include "imgui_impl_dx11.h"

namespace fcg {
namespace ui {

ImFont* gFontBody = nullptr;
ImFont* gFontHeading = nullptr;
ImFont* gFontMono = nullptr;
ImFont* gFontTitle = nullptr;
ImFont* gFontScore = nullptr;
ImFont* gFontTiny = nullptr;
bool gFontsBuilt = false;

// ---------------------------------------------------------------
// 颜色工具
// ---------------------------------------------------------------
ImU32 ToU32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

ImU32 Alpha(const ImVec4& c, float a) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * a));
}

ImU32 AlphaU32(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w = Clamp(v.w * a, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 LerpU32(ImU32 a, ImU32 b, float t) {
    ImVec4 va = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 vb = ImGui::ColorConvertU32ToFloat4(b);
    return ToU32(Lerp(va, vb, t));
}

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

// ---------------------------------------------------------------
// 字体构建
// ---------------------------------------------------------------
namespace {

constexpr float kPiF = 3.14159265358979f;

const char* kSymbolChars =
    "←→↑↓↔↕⇒⇐√✓✗✔✘×÷·•—…–≈±∞≠≤≥⊂⊆∈∉∑∏∫∂∆∇°′″⌫⌦⏱⚡★☆▶◀▲▼●○◆◇■□αβγδεθλμπσφωΩΔΘΛΣΦΨ";

// 界面/关卡/约束文案里出现过的汉字（从源码与关卡数据抽取）：
// 与「常用简体 2500 字」取并集，既保证覆盖又不至于在高 DPI 下撑爆图集。
const char* kChineseCorpus =
    "一三上下不与专且业东丢两严个中串临为主举么义之乎乘也书了事二于互五些交产享亮什仅仍从他代令以件价任份优会传估伸似但位低住体何余作你使例供依侧便保信修倍倒候值假偏做停偶储像允元充先光免兜入全公六共关其具典兼内再冗写冲决况净准减凑几出击函分切列则刚创初删判别到制刻前剩剪副割功加务劣动助勾匀包化区十升半单占卡印即却厂历压原去参又叉及双反发取受变叠口句另只可台右号各合同名后向吞否吧含启吸周味命和品哈哪唯啰"
    "喜嗦器噪噬四回因困围固图圆圈在地场均坊坏坐块坡坦垂型域基堆塞填增声壳处备复外多够大天太失头奇奏套好如妙始子字存孤学它守安宋完定实家容宽密察寸对导封射将小少尖尚尝就尺尽尾局层居屏展属山峰嵌工左巧差己已布师希带帧常幂幅幕干平并广序库应底度延建开弃式引张弦弧弱弹强归当录形彩影往径待很律得循微徽心必忆志忘快态怎性总恒恭息恰悉悬情惩想意感慢戏成或战截戳所手才打扣执扩扫扬扰找技把抑抖抗折抛抢护抬抵抽拆拉拓拖拟"
    "择括拱拽拿持指按挑挪振损换据捷掉排探接控推描提插搜搬摊撑撤操擎支收改放效敏敛散数整文斜断斯新方施旋无既日旦时明易星映是显晕景晰暂暗曲更曼替最有服望期木未末本术朴机杂权束条来板极构析枚果柄染柔查柯标栈栏树校样核根格框案档桥桩梯检棒棵楚模横次欢止正此步殊残段母每比毕毫气水永求汉池没沿法泡波注洁洽流测浪浮消涉涡淡深混清渐渲游源溢滑滚满滤漂漏演漩灯灰炸点炼烁烘焙然照熟爆爬父片版牛物特状独猜献率玩环现珀理琥"
    "生用由画界留略病白百的盖盘目直相省看真眼着知矩短码砌破础硬确碰示神禁离秀种秒积称移程稍稳稿穷空穿窗立竖章端笑笔符第等答策签简算管箭类粗精糊系素索紧累红约级纪纯纵纹线练组细终经绑结绕绘给绝统继绪续维综缀缓编缘缩缺网罚罩置美考者而耗耦职聚背能脉脏自至致舍舒航般良色节芒若英范荡获菜落薄藏虚虽行补表衰被裁装西要覆见观规视览觉角解触计认让记许论设访证评识词试话该详语误说请读调谓谱谷象负贡责败质贴费赖走起超越"
    "趋足距跟跨路跳身车转轭轮软轴轻载较辅辐辑输辨边达过迎运近返还这进远连迟迭述追退适逆选透逐递通速造逻逼遇遍道遗遭遮避那邻部都配采释里重野量针钟钮链销错键镜镳长门闪闭问间阈防阴阶附际降限除随隐隔难雅集零需青静非靠面页顶项顺须顿预频题颜额风饰馈首驱验骤高鲁黎黑默鼠齐"
;
// 大标题字体只包含界面固定文案用到的汉字（节省图集空间，保证 44px 清晰）
const char* kTitleGlyphs =
    "函数工坊无尽试炼第关关卡选择模式挑战成功失败评分结果大师之约入门进阶基础组合"
    "初识正弦偶之峰值的定单调数值域控制周期美解析近形次创新纪录最高通过"
    "时间剩余用时尝试次数当前已获得新恭喜符号构造引擎版本退出限时开启关闭"
    "ABCDS0123456789·-—×÷";

const char* FindChineseFont() {
    static const char* candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",   "C:\\Windows\\Fonts\\msyhbd.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf", "C:\\Windows\\Fonts\\Deng.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
    };
    for (const char* p : candidates) {
        FILE* f = fopen(p, "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return nullptr;
}

const char* FindMonoFont() {
    static const char* candidates[] = {
        "C:\\Windows\\Fonts\\consola.ttf", "C:\\Windows\\Fonts\\consolab.ttf",
        "C:\\Windows\\Fonts\\lucon.ttf",   "C:\\Windows\\Fonts\\cour.ttf",
    };
    for (const char* p : candidates) {
        FILE* f = fopen(p, "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return nullptr;
}

const char* FindScoreFont() {
    static const char* candidates[] = {
        "C:\\Windows\\Fonts\\consolab.ttf", "C:\\Windows\\Fonts\\consola.ttf",
        "C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\seguisb.ttf",
        "C:\\Windows\\Fonts\\arialbd.ttf",
    };
    for (const char* p : candidates) {
        FILE* f = fopen(p, "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return nullptr;
}

void BuildRange(ImFontGlyphRangesBuilder& b, ImFontAtlas* atlas, bool /*chineseFull*/,
                const std::string& extra) {
    b.AddRanges(atlas->GetGlyphRangesChineseSimplifiedCommon());
    b.AddText(kChineseCorpus);
    b.AddRanges(atlas->GetGlyphRangesGreek());
    b.AddText(kSymbolChars);
    if (!extra.empty()) b.AddText(extra.c_str());
    // 常用标点/箭头/数学符号补充
    for (int c = 0x2010; c <= 0x206F; ++c) b.AddChar((ImWchar)c);
    for (int c = 0x2190; c <= 0x21FF; ++c) b.AddChar((ImWchar)c);
    for (int c = 0x2200; c <= 0x22FF; ++c) b.AddChar((ImWchar)c);
    for (int c = 0x25A0; c <= 0x25FF; ++c) b.AddChar((ImWchar)c);
    b.AddChar(0x2713);  // ✓
    b.AddChar(0x2717);  // ✗
    b.AddChar(0x232B);  // ⌫
    b.AddChar(0x23F1);  // ⏱
    b.AddChar(0x26A1);  // ⚡
}

std::string ReadAllOrEmpty(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

}  // namespace

// 标题字体需要的额外文本（DPI 变化时 main.cpp 以空参重建字体，这里复用首次缓存）
static std::string gLastHeadingExtra;

FontSetupResult SetupFonts(const std::string& extraHeadingText, const std::string& logPath) {
    FontSetupResult r;
    ImGuiIO& io = ImGui::GetIO();
    if (!extraHeadingText.empty()) gLastHeadingExtra = extraHeadingText;
    const std::string headingExtra =
        extraHeadingText.empty() ? gLastHeadingExtra : extraHeadingText;
    const float sc = gUiScale > 0.0f ? gUiScale : 1.0f;
    const float bodyPx = kFontBodySize * sc;
    const float headingPx = kFontHeadingSize * sc;
    const float monoPx = kFontMonoSize * sc;
    const float titlePx = kFontTitleSize * sc;
    const float scorePx = kFontScoreSize * sc;
    ImFontAtlas* atlas = io.Fonts;

    FILE* log = logPath.empty() ? nullptr : fopen(logPath.c_str(), "w");
    auto L = [&](const char* fmt, ...) {
        if (!log) return;
        va_list ap;
        va_start(ap, fmt);
        vfprintf(log, fmt, ap);
        va_end(ap);
        fflush(log);
    };

    L("[fonts] initial atlas: %dx%d fonts=%d\n", atlas->TexWidth, atlas->TexHeight,
      atlas->Fonts.Size);

    const char* cnPath = FindChineseFont();
    const char* monoPath = FindMonoFont();
    L("[fonts] chinese=%s mono=%s\n", cnPath ? cnPath : "(none)",
      monoPath ? monoPath : "(none)");

    if (!cnPath) {
        r.note = "no chinese font";
        if (log) fclose(log);
        return r;
    }

    // 清空 main.cpp 已加载的字体，重新构建（补全 π/√/✓/→ 等符号）
    atlas->Clear();

    static ImVector<ImWchar> bodyRanges;
    static ImVector<ImWchar> headRanges;
    bodyRanges.clear();
    headRanges.clear();
    {
        ImFontGlyphRangesBuilder b;
        BuildRange(b, atlas, /*chineseFull=*/true, std::string());
        b.BuildRanges(&bodyRanges);
    }
    {
        ImFontGlyphRangesBuilder b;
        BuildRange(b, atlas, /*chineseFull=*/false, headingExtra);
        b.BuildRanges(&headRanges);
    }

    ImFontConfig bodyCfg;
    bodyCfg.OversampleH = (gUiScale > 1.25f) ? 1 : 2;
    bodyCfg.OversampleV = 1;
    bodyCfg.PixelSnapH = false;
    gFontBody = atlas->AddFontFromFileTTF(cnPath, bodyPx, &bodyCfg, bodyRanges.Data);
    if (!gFontBody) {
        atlas->Clear();
        gFontBody = atlas->AddFontFromFileTTF(cnPath, bodyPx, &bodyCfg,
                                              atlas->GetGlyphRangesChineseSimplifiedCommon());
    }

    ImFontConfig headCfg;
    headCfg.OversampleH = 1;
    headCfg.OversampleV = 1;
    headCfg.PixelSnapH = true;
    gFontHeading = atlas->AddFontFromFileTTF(cnPath, headingPx, &headCfg, headRanges.Data);

    if (monoPath) {
        ImFontConfig monoCfg;
        monoCfg.OversampleH = 2;
        monoCfg.OversampleV = 1;
        monoCfg.PixelSnapH = true;
        gFontMono = atlas->AddFontFromFileTTF(monoPath, monoPx, &monoCfg,
                                              atlas->GetGlyphRangesDefault());
    }
    if (!gFontMono) gFontMono = gFontBody;

    // 44px 中文大标题（仅内置固定文案字符，保证清晰且不撑爆图集）
    static ImVector<ImWchar> titleRanges;
    titleRanges.clear();
    {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(atlas->GetGlyphRangesDefault());
        b.AddText(kTitleGlyphs);
        b.BuildRanges(&titleRanges);
    }
    ImFontConfig titleCfg;
    titleCfg.OversampleH = 1;
    titleCfg.OversampleV = 1;
    titleCfg.PixelSnapH = true;
    gFontTitle = atlas->AddFontFromFileTTF(cnPath, titlePx, &titleCfg, titleRanges.Data);

    // 60px 西文大数字（评级 / 总分）
    const char* scorePath = FindScoreFont();
    if (scorePath) {
        ImFontConfig scoreCfg;
        scoreCfg.OversampleH = 2;
        scoreCfg.OversampleV = 2;
        scoreCfg.PixelSnapH = true;
        gFontScore = atlas->AddFontFromFileTTF(scorePath, scorePx, &scoreCfg,
                                               atlas->GetGlyphRangesDefault());
    }

    bool ok = atlas->Build();
    r.texW = atlas->TexWidth;
    r.texH = atlas->TexHeight;
    L("[fonts] build=%d atlas=%dx%d fonts=%d\n", ok ? 1 : 0, r.texW, r.texH,
      atlas->Fonts.Size);

    if (!ok) {
        // 回退：去掉标题字体重建，保证程序可用
        L("[fonts] build failed, falling back to body+mono only\n");
        atlas->Clear();
        ImFontConfig bcfg;
        bcfg.OversampleH = 1;
        bcfg.OversampleV = 1;
        bcfg.GlyphRanges = bodyRanges.Data;
        bcfg.GlyphRanges = bodyRanges.Data;
        gFontBody = atlas->AddFontFromFileTTF(cnPath, bodyPx, &bcfg, bodyRanges.Data);
        if (monoPath) {
            ImFontConfig mcfg;
            mcfg.OversampleH = 2;
            mcfg.PixelSnapH = true;
            gFontMono = atlas->AddFontFromFileTTF(monoPath, monoPx, &mcfg,
                                                  atlas->GetGlyphRangesDefault());
        }
        if (!gFontMono) gFontMono = gFontBody;
        ok = atlas->Build();
        gFontHeading = gFontBody;
        r.texW = atlas->TexWidth;
        r.texH = atlas->TexHeight;
        L("[fonts] fallback build=%d atlas=%dx%d\n", ok ? 1 : 0, r.texW, r.texH);
    }

    if (!gFontBody) {
        r.note = "font build failed";
        if (log) fclose(log);
        return r;
    }
    if (!gFontHeading) gFontHeading = gFontBody;
    if (!gFontTitle) gFontTitle = gFontHeading;
    if (!gFontScore) gFontScore = gFontMono ? gFontMono : gFontBody;
    io.FontDefault = gFontBody;
    gFontsBuilt = true;
    r.ok = ok;

    // 重新上传字体纹理
    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui_ImplDX11_CreateDeviceObjects();

    // 符号覆盖探针
    auto has = [](ImFont* f, ImWchar c) {
        return f && f->FindGlyphNoFallback(c) != nullptr;
    };
    r.bodyHasPi = has(gFontBody, 0x03C0);
    r.bodyHasSqrt = has(gFontBody, 0x221A);
    r.bodyHasCheck = has(gFontBody, 0x2713);
    r.bodyHasArrow = has(gFontBody, 0x2192);
    r.headingHasPi = has(gFontHeading, 0x03C0);
    L("[fonts] glyphs: pi=%d sqrt=%d check=%d arrow=%d headingPi=%d\n", r.bodyHasPi,
      r.bodyHasSqrt, r.bodyHasCheck, r.bodyHasArrow, r.headingHasPi);
    if (log) fclose(log);
    return r;
}

// ---------------------------------------------------------------
// Style
// ---------------------------------------------------------------
void ApplyTheme() {
    // 重置为默认 Style 再缩放：DPI 变化时 ApplyTheme 会被重复调用，不能累乘
    ImGui::GetStyle() = ImGuiStyle();
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    // 先按 DPI 整体缩放内置尺寸，再写入自定义尺寸（同样按逻辑像素换算）
    s.ScaleAllSizes(gUiScale > 0.0f ? gUiScale : 1.0f);

    s.WindowPadding = Sv(16, 14);
    s.FramePadding = Sv(12, 8);
    s.CellPadding = Sv(8, 6);
    s.ItemSpacing = Sv(10, 10);
    s.ItemInnerSpacing = Sv(8, 6);
    s.TouchExtraPadding = ImVec2(0, 0);
    s.IndentSpacing = S(18);
    s.ScrollbarSize = S(10);
    s.GrabMinSize = S(10);

    s.WindowBorderSize = 0;
    s.ChildBorderSize = S(1);
    s.PopupBorderSize = S(1);
    s.FrameBorderSize = 0;

    s.WindowRounding = S(14);
    s.ChildRounding = S(14);
    s.FrameRounding = S(8);
    s.PopupRounding = S(14);
    s.ScrollbarRounding = S(8);
    s.GrabRounding = S(6);
    s.TabRounding = S(8);

    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize = S(1);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col::Text;
    c[ImGuiCol_TextDisabled] = col::TextFaint;
    c[ImGuiCol_WindowBg] = col::Bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = col::Panel;
    c[ImGuiCol_Border] = col::Border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = col::Card;
    c[ImGuiCol_FrameBgHovered] = col::CardHi;
    c[ImGuiCol_FrameBgActive] = col::CardHi;
    c[ImGuiCol_TitleBg] = col::Panel;
    c[ImGuiCol_TitleBgActive] = col::Panel;
    c[ImGuiCol_TitleBgCollapsed] = col::Panel;
    c[ImGuiCol_MenuBarBg] = col::Panel;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = col::BorderHi;
    c[ImGuiCol_ScrollbarGrabHovered] = col::TextFaint;
    c[ImGuiCol_ScrollbarGrabActive] = col::CyanDeep;
    c[ImGuiCol_CheckMark] = col::Cyan;
    c[ImGuiCol_SliderGrab] = col::Cyan;
    c[ImGuiCol_SliderGrabActive] = col::CyanHi;
    c[ImGuiCol_Button] = col::Card;
    c[ImGuiCol_ButtonHovered] = col::CardHi;
    c[ImGuiCol_ButtonActive] = col::CyanDeep;
    c[ImGuiCol_Header] = col::Card;
    c[ImGuiCol_HeaderHovered] = col::CardHi;
    c[ImGuiCol_HeaderActive] = col::CardHi;
    c[ImGuiCol_Separator] = col::Border;
    c[ImGuiCol_SeparatorHovered] = col::Cyan;
    c[ImGuiCol_SeparatorActive] = col::CyanHi;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = col::Card;
    c[ImGuiCol_TabHovered] = col::CardHi;
    c[ImGuiCol_TabSelected] = col::CardHi;
    c[ImGuiCol_PlotLines] = col::Cyan;
    c[ImGuiCol_PlotLinesHovered] = col::Amber;
    c[ImGuiCol_PlotHistogram] = col::Cyan;
    c[ImGuiCol_TextSelectedBg] = ImVec4(col::Cyan.x, col::Cyan.y, col::Cyan.z, 0.35f);
    c[ImGuiCol_NavCursor] = col::Cyan;
}

// ---------------------------------------------------------------
// 文本
// ---------------------------------------------------------------
void TextColored(const ImVec4& c, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::TextColoredV(c, fmt, ap);
    va_end(ap);
}

void TextDim(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::TextColoredV(col::TextDim, fmt, ap);
    va_end(ap);
}

void TextFaint(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::TextColoredV(col::TextFaint, fmt, ap);
    va_end(ap);
}

void TextWrappedColored(const ImVec4& c, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void TextWrappedDim(const char* text) { TextWrappedColored(col::TextDim, text); }

void TextHeading(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushFont(gFontHeading);
    ImGui::TextColoredV(col::Text, fmt, ap);
    ImGui::PopFont();
    va_end(ap);
}

void TextMono(const ImVec4& c, const char* text, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float fs = size > 0 ? S(size) : (gFontMono ? gFontMono->FontSize : ImGui::GetFontSize());
    dl->AddText(gFontMono, fs, pos, ToU32(c), text);
    ImVec2 sz = gFontMono->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    ImGui::Dummy(sz);
}

ImVec2 CalcText(const char* text) {
    return ImGui::CalcTextSize(text, nullptr, false);
}

float TextWidth(const char* text) { return ImGui::CalcTextSize(text, nullptr, false).x; }

float HeadingWidth(const char* text) {
    if (!gFontHeading) return TextWidth(text);
    return gFontHeading->CalcTextSizeA(gFontHeading->FontSize, FLT_MAX, 0.0f, text).x;
}

// ---------------------------------------------------------------
// 绘制辅助
// ---------------------------------------------------------------
void DrawCard(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding, ImU32 fill,
              ImU32 border, bool shadow) {
    if (fill == 0) fill = ToU32(col::Card);
    if (border == 0) border = ToU32(col::Border);
    if (shadow) {
        for (int i = 3; i >= 1; --i) {
            float off = S(1.5f) * i;
            dl->AddRectFilled(ImVec2(p0.x - off, p0.y + off), ImVec2(p1.x + off, p1.y + off + off * 0.5f),
                              AlphaU32(0xFF000000, 0.05f), rounding + off * 0.5f);
        }
    }
    dl->AddRectFilled(p0, p1, fill, rounding);
    if (border != 0) dl->AddRect(p0, p1, border, rounding, 0, S(1.0f));
}

void DrawGlow(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding, ImU32 color,
              float strength) {
    for (int i = 1; i <= 4; ++i) {
        float off = S(1.0f) * i;
        dl->AddRect(ImVec2(p0.x - off, p0.y - off), ImVec2(p1.x + off, p1.y + off),
                    AlphaU32(color, 0.10f * strength / i), rounding + off, 0, S(1.0f));
    }
}

// ---------------------------------------------------------------
// 按钮样式
// ---------------------------------------------------------------
const ButtonStyle& StylePrimary() {
    static ButtonStyle st = {col::Cyan, col::CyanHi, ImVec4(0.157f, 0.706f, 0.816f, 1.0f),
                             ImVec4(0, 0, 0, 0), ImVec4(0.031f, 0.098f, 0.118f, 1.0f), true};
    return st;
}
const ButtonStyle& StyleAmber() {
    static ButtonStyle st = {col::Amber, col::AmberHi, ImVec4(0.855f, 0.588f, 0.157f, 1.0f),
                             ImVec4(0, 0, 0, 0), ImVec4(0.098f, 0.071f, 0.016f, 1.0f), true};
    return st;
}
const ButtonStyle& StyleSecondary() {
    static ButtonStyle st = {col::Card, col::CardHi, ImVec4(0.196f, 0.216f, 0.271f, 1.0f),
                             col::Border, col::Text, false};
    return st;
}
const ButtonStyle& StyleGhost() {
    static ButtonStyle st = {ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 0.06f),
                             ImVec4(1, 1, 1, 0.10f), ImVec4(0, 0, 0, 0), col::TextDim, false};
    return st;
}
const ButtonStyle& StyleDanger() {
    static ButtonStyle st = {ImVec4(0.267f, 0.153f, 0.176f, 1.0f), ImVec4(0.353f, 0.184f, 0.212f, 1.0f),
                             ImVec4(0.220f, 0.125f, 0.145f, 1.0f), ImVec4(col::Red.x, col::Red.y, col::Red.z, 0.55f),
                             col::Red, false};
    return st;
}
const ButtonStyle& StyleKey() {
    static ButtonStyle st = {ImVec4(0.129f, 0.141f, 0.180f, 1.0f), ImVec4(0.180f, 0.200f, 0.251f, 1.0f),
                             ImVec4(0.098f, 0.110f, 0.145f, 1.0f), ImVec4(0.169f, 0.188f, 0.239f, 1.0f),
                             col::Text, false};
    return st;
}
const ButtonStyle& StyleKeyFunc() {
    static ButtonStyle st = {ImVec4(0.086f, 0.169f, 0.196f, 1.0f), ImVec4(0.118f, 0.231f, 0.267f, 1.0f),
                             ImVec4(0.067f, 0.133f, 0.153f, 1.0f), ImVec4(0.133f, 0.259f, 0.298f, 1.0f),
                             col::Cyan, false};
    return st;
}
const ButtonStyle& StyleKeyOp() {
    static ButtonStyle st = {ImVec4(0.169f, 0.141f, 0.078f, 1.0f), ImVec4(0.231f, 0.192f, 0.106f, 1.0f),
                             ImVec4(0.133f, 0.110f, 0.063f, 1.0f), ImVec4(0.259f, 0.216f, 0.118f, 1.0f),
                             col::Amber, false};
    return st;
}
const ButtonStyle& StyleKeyUtil() {
    static ButtonStyle st = {ImVec4(0.141f, 0.129f, 0.157f, 1.0f), ImVec4(0.196f, 0.169f, 0.204f, 1.0f),
                             ImVec4(0.110f, 0.102f, 0.125f, 1.0f), ImVec4(0.224f, 0.196f, 0.235f, 1.0f),
                             col::TextDim, false};
    return st;
}
const ButtonStyle& StyleToggle(bool on) {
    if (on) {
        static ButtonStyle st = {col::CyanDeep, ImVec4(0.098f, 0.522f, 0.616f, 1.0f),
                                 ImVec4(0.067f, 0.400f, 0.478f, 1.0f), col::Cyan, col::CyanHi, true};
        return st;
    }
    static ButtonStyle st = {col::Card, col::CardHi, col::CardHi, col::Border, col::TextDim, false};
    return st;
}

// ---------------------------------------------------------------
// 图标（手绘）
// ---------------------------------------------------------------
void DrawIcon(ImDrawList* dl, Icon icon, const ImVec2& c, float s, ImU32 col, float th) {
    float h = s * 0.5f;
    switch (icon) {
        case Icon::None:
            break;
        case Icon::Left:
            dl->AddLine(ImVec2(c.x + h * 0.45f, c.y - h * 0.75f), ImVec2(c.x - h * 0.45f, c.y), col, th);
            dl->AddLine(ImVec2(c.x - h * 0.45f, c.y), ImVec2(c.x + h * 0.45f, c.y + h * 0.75f), col, th);
            break;
        case Icon::Right:
            dl->AddLine(ImVec2(c.x - h * 0.45f, c.y - h * 0.75f), ImVec2(c.x + h * 0.45f, c.y), col, th);
            dl->AddLine(ImVec2(c.x + h * 0.45f, c.y), ImVec2(c.x - h * 0.45f, c.y + h * 0.75f), col, th);
            break;
        case Icon::Backspace:
            dl->AddLine(ImVec2(c.x - h * 0.1f, c.y - h * 0.55f), ImVec2(c.x - h * 0.85f, c.y), col, th);
            dl->AddLine(ImVec2(c.x - h * 0.85f, c.y), ImVec2(c.x - h * 0.1f, c.y + h * 0.55f), col, th);
            dl->AddRect(ImVec2(c.x - h * 0.1f, c.y - h * 0.55f), ImVec2(c.x + h * 0.85f, c.y + h * 0.55f),
                        col, S(3.0f), 0, th);
            dl->AddLine(ImVec2(c.x + h * 0.15f, c.y - h * 0.2f), ImVec2(c.x + h * 0.55f, c.y + h * 0.2f), col, th * 0.8f);
            dl->AddLine(ImVec2(c.x + h * 0.55f, c.y - h * 0.2f), ImVec2(c.x + h * 0.15f, c.y + h * 0.2f), col, th * 0.8f);
            break;
        case Icon::Play: {
            dl->AddTriangleFilled(ImVec2(c.x - h * 0.5f, c.y - h * 0.75f), ImVec2(c.x - h * 0.5f, c.y + h * 0.75f),
                                  ImVec2(c.x + h * 0.8f, c.y), col);
            break;
        }
        case Icon::Check:
            dl->AddLine(ImVec2(c.x - h * 0.62f, c.y + h * 0.02f), ImVec2(c.x - h * 0.15f, c.y + h * 0.5f), col, th);
            dl->AddLine(ImVec2(c.x - h * 0.15f, c.y + h * 0.5f), ImVec2(c.x + h * 0.65f, c.y - h * 0.5f), col, th);
            break;
        case Icon::Cross:
            dl->AddLine(ImVec2(c.x - h * 0.55f, c.y - h * 0.55f), ImVec2(c.x + h * 0.55f, c.y + h * 0.55f), col, th);
            dl->AddLine(ImVec2(c.x + h * 0.55f, c.y - h * 0.55f), ImVec2(c.x - h * 0.55f, c.y + h * 0.55f), col, th);
            break;
        case Icon::Star: {
            ImVec2 pts[10];
            for (int i = 0; i < 10; ++i) {
                float ang = -kPiF * 0.5f + i * kPiF / 5.0f;
                float rad = (i % 2 == 0) ? h * 0.95f : h * 0.42f;
                pts[i] = ImVec2(c.x + cosf(ang) * rad, c.y + sinf(ang) * rad);
            }
            dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, th);
            break;
        }
        case Icon::Crown: {
            ImVec2 p[5] = {ImVec2(c.x - h * 0.85f, c.y + h * 0.45f), ImVec2(c.x - h * 0.7f, c.y - h * 0.5f),
                           ImVec2(c.x, c.y + h * 0.05f), ImVec2(c.x + h * 0.7f, c.y - h * 0.5f),
                           ImVec2(c.x + h * 0.85f, c.y + h * 0.45f)};
            dl->AddPolyline(p, 5, col, 0, th);
            dl->AddLine(ImVec2(c.x - h * 0.85f, c.y + h * 0.55f), ImVec2(c.x + h * 0.85f, c.y + h * 0.55f), col, th);
            break;
        }
        case Icon::Flame: {
            ImVec2 p0(c.x, c.y - h * 0.9f);
            ImVec2 p1(c.x + h * 0.6f, c.y - h * 0.1f);
            ImVec2 p2(c.x + h * 0.35f, c.y + h * 0.75f);
            ImVec2 p3(c.x - h * 0.35f, c.y + h * 0.75f);
            ImVec2 p4(c.x - h * 0.6f, c.y - h * 0.1f);
            dl->AddBezierQuadratic(p0, p1, p2, col, th);
            dl->AddLine(p2, p3, col, th);
            dl->AddBezierQuadratic(p3, p4, p0, col, th);
            break;
        }
        case Icon::Infinity: {
            float rx = h * 0.55f, ry = h * 0.42f;
            dl->PathClear();
            for (int i = 0; i <= 24; ++i) {
                float t = i / 24.0f * kPiF * 2.0f;
                float x = c.x + rx * cosf(t);
                float y = c.y + ry * sinf(t * 2.0f);
                dl->PathLineTo(ImVec2(x, y));
            }
            dl->PathStroke(col, 0, th);
            break;
        }
        case Icon::Grid:
            dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.8f), ImVec2(c.x + h * 0.8f, c.y + h * 0.8f), col, S(3.0f), 0, th);
            dl->AddLine(ImVec2(c.x, c.y - h * 0.8f), ImVec2(c.x, c.y + h * 0.8f), col, th * 0.7f);
            dl->AddLine(ImVec2(c.x - h * 0.8f, c.y), ImVec2(c.x + h * 0.8f, c.y), col, th * 0.7f);
            break;
        case Icon::Home:
            dl->AddLine(ImVec2(c.x - h * 0.85f, c.y - h * 0.05f), ImVec2(c.x, c.y - h * 0.85f), col, th);
            dl->AddLine(ImVec2(c.x, c.y - h * 0.85f), ImVec2(c.x + h * 0.85f, c.y - h * 0.05f), col, th);
            dl->AddRect(ImVec2(c.x - h * 0.62f, c.y - h * 0.05f), ImVec2(c.x + h * 0.62f, c.y + h * 0.8f), col, S(2.0f), 0, th);
            break;
        case Icon::Clock:
            dl->AddCircle(c, h * 0.8f, col, 20, th);
            dl->AddLine(c, ImVec2(c.x, c.y - h * 0.42f), col, th);
            dl->AddLine(c, ImVec2(c.x + h * 0.34f, c.y + h * 0.2f), col, th);
            break;
        case Icon::Bulb:
            dl->AddCircle(c, h * 0.55f, col, 16, th);
            dl->AddLine(ImVec2(c.x - h * 0.28f, c.y + h * 0.62f), ImVec2(c.x + h * 0.28f, c.y + h * 0.62f), col, th);
            dl->AddLine(ImVec2(c.x - h * 0.2f, c.y + h * 0.85f), ImVec2(c.x + h * 0.2f, c.y + h * 0.85f), col, th);
            for (int i = 0; i < 6; ++i) {
                float a = i * kPiF / 3.0f;
                ImVec2 p(c.x + cosf(a) * h * 1.05f, c.y + sinf(a) * h * 1.05f);
                ImVec2 q(c.x + cosf(a) * h * 1.45f, c.y + sinf(a) * h * 1.45f);
                dl->AddLine(p, q, col, th * 0.7f);
            }
            break;
        case Icon::Retry:
            dl->PathClear();
            for (int i = 0; i <= 20; ++i) {
                float a = -kPiF * 0.5f + i / 20.0f * kPiF * 1.65f;
                dl->PathLineTo(ImVec2(c.x + cosf(a) * h * 0.8f, c.y + sinf(a) * h * 0.8f));
            }
            dl->PathStroke(col, 0, th);
            dl->AddTriangleFilled(ImVec2(c.x - h * 0.8f, c.y - h * 1.0f),
                                  ImVec2(c.x - h * 0.1f, c.y - h * 0.62f),
                                  ImVec2(c.x - h * 0.72f, c.y - h * 0.25f), col);
            break;
        case Icon::List:
            for (int i = -1; i <= 1; ++i) {
                float y = c.y + i * h * 0.55f;
                dl->AddCircleFilled(ImVec2(c.x - h * 0.7f, y), th * 0.9f, col);
                dl->AddLine(ImVec2(c.x - h * 0.35f, y), ImVec2(c.x + h * 0.8f, y), col, th);
            }
            break;
        case Icon::Swords:
            dl->AddLine(ImVec2(c.x - h * 0.75f, c.y + h * 0.75f), ImVec2(c.x + h * 0.55f, c.y - h * 0.55f), col, th);
            dl->AddLine(ImVec2(c.x - h * 0.1f, c.y - h * 0.75f), ImVec2(c.x + h * 0.75f, c.y + h * 0.1f), col, th);
            dl->AddLine(ImVec2(c.x - h * 0.75f, c.y + h * 0.35f), ImVec2(c.x - h * 0.35f, c.y + h * 0.75f), col, th * 1.3f);
            break;
        case Icon::Sparkle: {
            dl->AddLine(ImVec2(c.x, c.y - h), ImVec2(c.x, c.y + h), col, th * 0.8f);
            dl->AddLine(ImVec2(c.x - h, c.y), ImVec2(c.x + h, c.y), col, th * 0.8f);
            dl->AddLine(ImVec2(c.x - h * 0.6f, c.y - h * 0.6f), ImVec2(c.x + h * 0.6f, c.y + h * 0.6f), col, th * 0.6f);
            dl->AddLine(ImVec2(c.x + h * 0.6f, c.y - h * 0.6f), ImVec2(c.x - h * 0.6f, c.y + h * 0.6f), col, th * 0.6f);
            break;
        }
        case Icon::Lock:
            dl->AddRect(ImVec2(c.x - h * 0.6f, c.y - h * 0.1f), ImVec2(c.x + h * 0.6f, c.y + h * 0.8f), col, 3, 0, th);
            dl->PathClear();
            for (int i = 0; i <= 12; ++i) {
                float a = kPiF + i / 12.0f * kPiF;
                dl->PathLineTo(ImVec2(c.x + cosf(a) * h * 0.42f, c.y - h * 0.2f + sinf(a) * h * 0.42f));
            }
            dl->PathStroke(col, 0, th);
            break;
    }
}

// ---------------------------------------------------------------
// 自绘按钮
// ---------------------------------------------------------------
bool FunButton(const char* id, const char* label, const ImVec2& size, const ButtonStyle& st,
               bool enabled, Icon icon, ImFont* font, float rounding) {
    ImGui::PushID(id);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##btn", size);
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1 = ImVec2(pos.x + size.x, pos.y + size.y);

    ImVec4 bg = st.bg;
    if (!enabled) {
        bg = ImVec4(bg.x * 0.5f, bg.y * 0.5f, bg.z * 0.5f, bg.w * 0.5f);
    } else if (held) {
        bg = st.bgActive;
    } else if (hovered) {
        bg = st.bgHover;
    }

    if (st.glow && enabled && (hovered || held)) {
        DrawGlow(dl, pos, p1, rounding, ToU32(st.text), 1.0f);
    }
    if (bg.w > 0.001f) dl->AddRectFilled(pos, p1, ToU32(bg), rounding);
    if (st.border.w > 0.001f && enabled) {
        dl->AddRect(pos, p1, ToU32(hovered && enabled ? col::BorderHi : st.border), rounding, 0,
                    S(1.0f));
    }

    ImVec4 tc = st.text;
    if (!enabled) {
        tc.w *= 0.45f;
    } else if (st.glow && (hovered || held)) {
        tc = col::White;
    }
    ImU32 tcol = ToU32(tc);
    ImFont* f = font ? font : gFontBody;
    if (!f) f = ImGui::GetFont();
    float fs = f->FontSize;

    float iconW = 0.0f;
    float gap = 0.0f;
    if (icon != Icon::None) {
        iconW = fs * 0.85f;
        gap = label && label[0] ? fs * 0.42f : 0.0f;
    }
    float textW = (label && label[0]) ? f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label).x : 0.0f;
    float totalW = iconW + gap + textW;
    float startX = pos.x + (size.x - totalW) * 0.5f;
    float cy = pos.y + size.y * 0.5f;
    if (icon != Icon::None) {
        DrawIcon(dl, icon, ImVec2(startX + iconW * 0.5f, cy), fs * 0.8f, tcol, S(2.0f));
        startX += iconW + gap;
    }
    if (label && label[0]) {
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
        dl->AddText(f, fs, ImVec2(startX, cy - ts.y * 0.5f), tcol, label);
    }
    return enabled && pressed;
}

bool MenuButton(const char* id, const char* label, const char* sub, const ImVec2& size,
                const ButtonStyle& st, Icon icon, ImFont* font) {
    ImGui::PushID(id);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##btn", size);
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1 = ImVec2(pos.x + size.x, pos.y + size.y);
    ImVec4 bg = held ? st.bgActive : (hovered ? st.bgHover : st.bg);
    float rounding = S(14.0f);
    if (st.glow && hovered) DrawGlow(dl, pos, p1, rounding, ToU32(st.text), 1.0f);
    dl->AddRectFilled(pos, p1, ToU32(bg), rounding);
    if (st.border.w > 0.001f)
        dl->AddRect(pos, p1, ToU32(hovered ? col::BorderHi : st.border), rounding, 0, S(1.0f));

    ImU32 tcol = ToU32(col::White);
    ImU32 subcol = ToU32(st.bg.w < 0.5f || st.glow ? ImVec4(st.text.x, st.text.y, st.text.z, 0.72f)
                                                    : col::TextDim);
    if (!st.glow) {
        tcol = ToU32(st.text);
    }

    // 左侧图标圆
    float iconCx = pos.x + S(46.0f);
    float cy = pos.y + size.y * 0.5f;
    if (icon != Icon::None) {
        ImU32 circleBg = st.glow ? ToU32(Alpha(col::White, 0.14f))
                                 : ToU32(Alpha(st.text, 0.10f));
        dl->AddCircleFilled(ImVec2(iconCx, cy), S(22.0f), circleBg);
        DrawIcon(dl, icon, ImVec2(iconCx, cy), S(22.0f), tcol, S(2.2f));
    }

    ImFont* f = font ? font : gFontBody;
    if (!f) f = ImGui::GetFont();
    float fs = f->FontSize;
    float tx = pos.x + S(82.0f);
    ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
    float textY = pos.y + size.y * 0.5f - ts.y * 0.5f - (sub && sub[0] ? S(9.0f) : 0.0f);
    dl->AddText(f, fs, ImVec2(tx, textY), tcol, label);
    if (sub && sub[0]) {
        dl->AddText(gFontBody, S(15.0f), ImVec2(tx, textY + ts.y + S(3.0f)), subcol, sub);
    }
    // 右侧箭头
    if (hovered) {
        DrawIcon(dl, Icon::Right, ImVec2(p1.x - S(28.0f), cy), S(15.0f), AlphaU32(tcol, 0.9f),
                 S(2.0f));
    }
    return pressed;
}

// ---------------------------------------------------------------
// 开关
// ---------------------------------------------------------------
bool ToggleSwitch(const char* id, bool* value, const char* label, const char* hint) {
    ImGui::PushID(id);
    float h = S(28.0f), w = S(52.0f);
    float labelW = label ? TextWidth(label) : 0.0f;
    float hintW = hint ? TextWidth(hint) : 0.0f;
    float totalW =
        w + (labelW > 0 ? labelW + S(12.0f) : 0.0f) + (hintW > 0 ? hintW + S(12.0f) : 0.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##toggle", ImVec2(totalW, h + 4.0f));
    bool hovered = ImGui::IsItemHovered();
    bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (pressed && value) *value = !*value;
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = pos.y + (h + 4.0f) * 0.5f;
    ImVec2 p0(pos.x, cy - h * 0.5f);
    ImVec2 p1(pos.x + w, cy + h * 0.5f);
    bool on = value && *value;
    ImU32 track = on ? ToU32(col::CyanDeep) : ToU32(col::Card);
    if (hovered) track = on ? ToU32(ImVec4(0.118f, 0.522f, 0.616f, 1.0f)) : ToU32(col::CardHi);
    dl->AddRectFilled(p0, p1, track, h * 0.5f);
    dl->AddRect(p0, p1, ToU32(on ? col::Cyan : col::Border), h * 0.5f, 0, S(1.2f));
    ImVec2 knobCenter = on ? ImVec2(p1.x - h * 0.5f, cy) : ImVec2(p0.x + h * 0.5f, cy);
    dl->AddCircleFilled(knobCenter, h * 0.33f, ToU32(on ? col::White : col::TextDim));
    if (on) DrawGlow(dl, ImVec2(p1.x - h, p0.y), p1, h * 0.5f, ToU32(col::Cyan), 0.8f);

    float x = pos.x + w + S(12.0f);
    if (label && label[0]) {
        float lfs = gFontBody ? gFontBody->FontSize : S(19.0f);
        dl->AddText(gFontBody, lfs, ImVec2(x, cy - lfs * 0.5f),
                    ToU32(on ? col::Text : col::TextDim), label);
        x += labelW + S(12.0f);
    }
    if (hint && hint[0]) {
        dl->AddText(gFontBody, S(15.0f), ImVec2(x, cy - S(8.0f)), ToU32(col::TextFaint), hint);
    }
    return pressed;
}

// ---------------------------------------------------------------
// 徽章 / 进度条 / 状态
// ---------------------------------------------------------------
void Badge(const char* label, const ImVec4& fg, const ImVec4& bg, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float fs = S(15.0f);
    ImVec2 ts = gFontBody->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
    float w = ts.x + S(16.0f);
    float h = height;
    ImVec2 p0(pos.x, pos.y);
    ImVec2 p1(pos.x + w, pos.y + h);
    dl->AddRectFilled(p0, p1, ToU32(bg), S(6.0f));
    dl->AddRect(p0, p1, ToU32(ImVec4(fg.x, fg.y, fg.z, 0.35f)), S(6.0f), 0, S(1.0f));
    dl->AddText(gFontBody, fs, ImVec2(pos.x + S(8.0f), pos.y + (h - fs) * 0.5f - S(1.0f)),
                ToU32(fg), label);
    ImGui::Dummy(ImVec2(w, h));
}

void ScoreBar(const char* label, float t, const ImVec4& fill, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float labelW = Min(S(96.0f), size.x * 0.28f);
    float barH = size.y;
    float barX = pos.x + labelW + S(10.0f);
    float barW = size.x - labelW - S(10.0f) - S(48.0f);
    float cy = pos.y + barH * 0.5f;

    dl->AddText(gFontBody, S(16.0f), ImVec2(pos.x, cy - S(9.0f)), ToU32(col::TextDim), label);

    ImVec2 b0(barX, cy - S(5.0f));
    ImVec2 b1(barX + barW, cy + S(5.0f));
    dl->AddRectFilled(b0, b1, ToU32(Alpha(col::White, 0.06f)), S(5.0f));
    float tt = Clamp(t, 0.0f, 1.0f);
    if (tt > 0.0f) {
        ImVec2 f1(barX + barW * tt, cy + S(5.0f));
        dl->AddRectFilled(b0, f1, ToU32(fill), S(5.0f));
        dl->AddRectFilled(ImVec2(b0.x, b0.y), ImVec2(f1.x, b0.y + S(3.0f)),
                          ToU32(ImVec4(1, 1, 1, 0.18f)), S(5.0f));
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)std::lround(tt * 100.0f));
    ImVec2 ts = gFontBody->CalcTextSizeA(S(16.0f), FLT_MAX, 0.0f, buf);
    dl->AddText(gFontBody, S(16.0f), ImVec2(pos.x + size.x - ts.x, cy - S(9.0f)),
                ToU32(tt >= 0.9f ? col::Green : col::TextDim), buf);
    ImGui::Dummy(ImVec2(size.x, barH));
}

void StatusDot(ImDrawList* dl, const ImVec2& center, float radius, bool ok) {
    if (ok) {
        dl->AddCircleFilled(center, radius, ToU32(Alpha(col::Green, 0.16f)));
        dl->AddCircle(center, radius, ToU32(Alpha(col::Green, 0.7f)), 24, S(1.3f));
        DrawIcon(dl, Icon::Check, center, radius * 1.1f, ToU32(col::Green), S(2.0f));
    } else {
        dl->AddCircleFilled(center, radius, ToU32(Alpha(col::Red, 0.14f)));
        dl->AddCircle(center, radius, ToU32(Alpha(col::Red, 0.7f)), 24, S(1.3f));
        DrawIcon(dl, Icon::Cross, center, radius * 1.0f, ToU32(col::Red), S(1.8f));
    }
}

void HSep(float alpha) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = pos.y + S(4.0f);
    dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + w, y), ToU32(Alpha(col::Border, alpha)),
                S(1.0f));
    ImGui::Dummy(ImVec2(w, S(9.0f)));
}

bool BackButton(const char* id, const char* label, const ImVec2& size) {
    return FunButton(id, label, size, StyleSecondary(), true, Icon::Left);
}

std::string FormatTime(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = (int)std::floor(seconds + 0.5);
    int m = total / 60;
    int s = total % 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}

const ImVec4& GradeColor(char grade) {
    switch (grade) {
        case 'S': return col::Amber;
        case 'A': return col::Cyan;
        case 'B': return col::Green;
        case 'C': return ImVec4(0.72f, 0.74f, 0.45f, 1.0f);
        default: return col::Red;
    }
}

const char* GradeLabel(char grade) {
    switch (grade) {
        case 'S': return "完美";
        case 'A': return "优秀";
        case 'B': return "良好";
        case 'C': return "合格";
        default: return "未达标";
    }
}

const char* GradeName(char grade) {
    switch (grade) {
        case 'S': return "S";
        case 'A': return "A";
        case 'B': return "B";
        case 'C': return "C";
        default: return "D";
    }
}

ImU32 GradeBg(char grade) {
    switch (grade) {
        case 'S': return ToU32(ImVec4(0.30f, 0.22f, 0.06f, 1.0f));
        case 'A': return ToU32(ImVec4(0.06f, 0.22f, 0.26f, 1.0f));
        case 'B': return ToU32(ImVec4(0.06f, 0.24f, 0.17f, 1.0f));
        case 'C': return ToU32(ImVec4(0.20f, 0.20f, 0.10f, 1.0f));
        default: return ToU32(ImVec4(0.28f, 0.12f, 0.13f, 1.0f));
    }
}

void HelpTooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(S(320.0f));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

UiCanvas MakeCanvas() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 origin = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    (void)vp;
    UiCanvas cv;
    cv.origin = origin;
    cv.size = size;
    cv.logicalW = ToLogical(size.x);
    cv.logicalH = ToLogical(size.y);
    return cv;
}

ContentArea MakeContentArea(float marginX, float marginY, float topReserved,
                            float bottomReserved, float maxContentW, float maxContentH) {
    UiCanvas cv = MakeCanvas();
    ContentArea a;
    float availW = cv.logicalW - marginX * 2.0f;
    // 超宽屏（21:9 等）：让内容区随窗口略微放宽，避免大片黑边，但仍限制上限
    float grownMax = Min(cv.logicalW * 0.72f, maxContentW * 1.38f);
    float limitW = Max(maxContentW, grownMax);
    float w = Min(availW, limitW);
    a.x = (cv.logicalW - w) * 0.5f;
    float availH = Max(0.0f, cv.logicalH - topReserved - bottomReserved - marginY * 2.0f);
    float h = Min(availH, maxContentH);
    // 超高窗口（如 21:9）时内容垂直居中，避免纵向被拉散
    a.y = topReserved + marginY + (availH - h) * 0.5f;
    a.w = w;
    a.h = h;
    return a;
}

std::string EllipsizeFont(ImFont* font, float size, const std::string& text, float maxWidth) {
    if (!font) return text;
    if (maxWidth <= 0.0f) return text;
    float full = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
    if (full <= maxWidth) return text;
    std::string result;
    float ellW = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "…").x;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];
        size_t len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        len = std::min(len, text.size() - i);
        std::string candidate = result + text.substr(i, len);
        float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, candidate.c_str()).x;
        if (w + ellW > maxWidth) break;
        result = candidate;
        i += len;
    }
    return result + "…";
}

std::string Ellipsize(const std::string& text, float maxWidth) {
    if (!gFontBody || maxWidth <= 0) return text;
    float full = gFontBody->CalcTextSizeA(gFontBody->FontSize, FLT_MAX, 0.0f, text.c_str()).x;
    if (full <= maxWidth) return text;
    std::string result;
    float ellW = gFontBody->CalcTextSizeA(gFontBody->FontSize, FLT_MAX, 0.0f, "…").x;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];
        size_t len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        len = std::min(len, text.size() - i);
        std::string candidate = result + text.substr(i, len);
        float w = gFontBody->CalcTextSizeA(gFontBody->FontSize, FLT_MAX, 0.0f, candidate.c_str()).x;
        if (w + ellW > maxWidth) break;
        result = candidate;
        i += len;
    }
    return result + "…";
}

}  // namespace ui
}  // namespace fcg
