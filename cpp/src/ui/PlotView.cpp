// PlotView.cpp — 函数图像绘制（ImGui draw list 手绘）
// 采样 evaluateAt()，逐像素绘制，定义域断点自动断开，约束区间淡色高亮。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "App.h"
#include "LevelUi.h"
#include "core/Numeric.h"
#include "D3DTexture.h"
#include "Theme.h"
#include "imgui.h"

namespace fcg {
namespace ui {

namespace {

constexpr double kPi = 3.14159265358979323846;

double NiceStep(double span, int targetTicks) {
    if (span <= 0) return 1.0;
    double rough = span / targetTicks;
    double mag = std::pow(10.0, std::floor(std::log10(rough)));
    double n = rough / mag;
    double step;
    if (n <= 1.0)
        step = 1.0;
    else if (n <= 2.0)
        step = 2.0;
    else if (n <= 2.5)
        step = 2.5;
    else if (n <= 5.0)
        step = 5.0;
    else
        step = 10.0;
    return step * mag;
}

std::string TickLabel(double v) {
    std::string s = FormatNumber(v);
    return s;
}

// 计算图像的 x 显示范围：优先参考关卡约束区间
void ComputeXRange(const LevelDef& level, double* xmin, double* xmax) {
    double lo = 1e308, hi = -1e308;
    bool have = false;
    for (const auto& c : level.constraints) {
        const Interval& iv = c.interval;
        if (iv.loInf && iv.hiInf) continue;
        double l = iv.loInf ? 0.0 : iv.lo;
        double h = iv.hiInf ? 0.0 : iv.hi;
        if (iv.loInf) l = h - 2.0 * kPi;
        if (iv.hiInf) h = l + 2.0 * kPi;
        if (!(h > l)) continue;
        if (h - l > 400.0) continue;
        lo = std::min(lo, l);
        hi = std::max(hi, h);
        have = true;
    }
    if (!have) {
        *xmin = -2.0 * kPi;
        *xmax = 2.0 * kPi;
        return;
    }
    double pad = std::max(0.65, (hi - lo) * 0.16);
    *xmin = lo - pad;
    *xmax = hi + pad;
}

struct SampleData {
    std::vector<double> xs;
    std::vector<double> ys;
    std::vector<char> ok;
};

void SampleCurve(const ExprP& ast, double xmin, double xmax, int n, SampleData* out) {
    out->xs.resize(static_cast<size_t>(n));
    out->ys.resize(static_cast<size_t>(n));
    out->ok.resize(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / std::max(1, n - 1);
        double x = xmin + (xmax - xmin) * t;
        out->xs[static_cast<size_t>(i)] = x;
        RealResult r = evalReal(ast, "x", x);
        if (r.ok && std::isfinite(r.value) && std::fabs(r.value) < 1e12) {
            out->ys[static_cast<size_t>(i)] = r.value;
            out->ok[static_cast<size_t>(i)] = 1;
        }
    }
}

void ComputeYRange(const SampleData& s, double* ymin, double* ymax) {
    std::vector<double> vals;
    vals.reserve(s.ys.size());
    for (size_t i = 0; i < s.ys.size(); ++i)
        if (s.ok[i]) vals.push_back(s.ys[i]);
    if (vals.empty()) {
        *ymin = -1.5;
        *ymax = 1.5;
        return;
    }
    std::sort(vals.begin(), vals.end());
    size_t n = vals.size();
    double lo = vals[n <= 20 ? 0 : static_cast<size_t>(n * 0.02)];
    double hi = vals[n <= 20 ? n - 1 : static_cast<size_t>(n * 0.98)];
    if (n > 40) {
        // 用 2%/98% 分位抑制极点附近的爆表值
    }
    lo = std::min(lo, 0.0);
    hi = std::max(hi, 0.0);
    double span = hi - lo;
    if (span < 1e-9) {
        lo -= 1.0;
        hi += 1.0;
        span = 2.0;
    }
    double pad = span * 0.16;
    *ymin = lo - pad;
    *ymax = hi + pad;
    if (*ymax - *ymin > 1e9) {
        *ymin = -10;
        *ymax = 10;
    }
}

void DrawGrid(ImDrawList* dl, const ImVec2& r0, const ImVec2& r1, double xmin, double xmax,
              double ymin, double ymax, int targetY) {
    auto mapX = [&](double x) {
        return r0.x + static_cast<float>((x - xmin) / (xmax - xmin)) * (r1.x - r0.x);
    };
    auto mapY = [&](double y) {
        return r1.y - static_cast<float>((y - ymin) / (ymax - ymin)) * (r1.y - r0.y);
    };
    ImU32 gridCol = ToU32(Alpha(col::White, 0.045f));
    ImU32 gridCol2 = ToU32(Alpha(col::White, 0.075f));

    // 网格步长
    double xspan = xmax - xmin;
    double yspan = ymax - ymin;
    double xstep = NiceStep(xspan, 8);
    double ystep = NiceStep(yspan, targetY);

    // 竖线
    double xstart = std::ceil(xmin / xstep) * xstep;
    for (double x = xstart; x <= xmax + 1e-9; x += xstep) {
        float px = mapX(x);
        bool zero = std::fabs(x) < xstep * 1e-6;
        dl->AddLine(ImVec2(px, r0.y), ImVec2(px, r1.y), zero ? gridCol2 : gridCol, 1.0f);
    }
    // 横线
    double ystart = std::ceil(ymin / ystep) * ystep;
    for (double y = ystart; y <= ymax + 1e-9; y += ystep) {
        float py = mapY(y);
        bool zero = std::fabs(y) < ystep * 1e-6;
        dl->AddLine(ImVec2(r0.x, py), ImVec2(r1.x, py), zero ? gridCol2 : gridCol, 1.0f);
    }

    // 坐标轴
    ImU32 axisCol = ToU32(Alpha(col::TextDim, 0.55f));
    if (ymin < 0 && ymax > 0) {
        float py = mapY(0.0);
        dl->AddLine(ImVec2(r0.x, py), ImVec2(r1.x, py), axisCol, S(1.4f));
    }
    if (xmin < 0 && xmax > 0) {
        float px = mapX(0.0);
        dl->AddLine(ImVec2(px, r0.y), ImVec2(px, r1.y), axisCol, S(1.4f));
    }

    // 刻度文字
    float fs = S(12.0f);
    ImU32 tickCol = ToU32(col::TextFaint);
    float xLabelY = (ymin < 0 && ymax > 0) ? Clamp(mapY(0.0) + S(5.0f), r0.y, r1.y - fs - S(2.0f))
                                           : r1.y - fs - S(3.0f);
    double lastLabelX = -1e308;
    for (double x = xstart; x <= xmax + 1e-9; x += xstep) {
        if (std::fabs(x) < xstep * 1e-6) continue;
        float px = mapX(x);
        if (px < r0.x + 8 || px > r1.x - 8) continue;
        if (px - lastLabelX < S(54.0f)) continue;
        std::string s = TickLabel(x);
        ImVec2 ts = gFontBody->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str());
        dl->AddText(gFontBody, fs, ImVec2(px - ts.x * 0.5f, xLabelY), tickCol, s.c_str());
        lastLabelX = px;
    }
    if (xmin < 0 && xmax > 0) {
        float px = mapX(0.0);
        ImVec2 ts = gFontBody->CalcTextSizeA(fs, FLT_MAX, 0.0f, "0");
        dl->AddText(gFontBody, fs, ImVec2(px - ts.x - S(4.0f), r1.y - fs - S(3.0f)), tickCol, "0");
    }
    for (double y = ystart; y <= ymax + 1e-9; y += ystep) {
        if (std::fabs(y) < ystep * 1e-6) continue;
        float py = mapY(y);
        if (py < r0.y + 8 || py > r1.y - 8) continue;
        std::string s = TickLabel(y);
        ImVec2 ts = gFontBody->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str());
        float tx = r0.x + S(5.0f);
        if (xmin < 0 && xmax > 0) tx = Max(r0.x + S(5.0f), mapX(0.0) + S(5.0f));
        if (tx + ts.x > r1.x - 4) continue;
        dl->AddText(gFontBody, fs, ImVec2(tx, py - fs * 0.5f), tickCol, s.c_str());
    }
}

void DrawConstraintBands(ImDrawList* dl, const ImVec2& r0, const ImVec2& r1, const LevelDef& level,
                         double xmin, double xmax) {
    auto mapX = [&](double x) {
        return r0.x + static_cast<float>((x - xmin) / (xmax - xmin)) * (r1.x - r0.x);
    };
    bool drawn = false;
    for (const auto& c : level.constraints) {
        const Interval& iv = c.interval;
        if (iv.loInf && iv.hiInf) continue;
        double lo = iv.loInf ? xmin : std::max(iv.lo, xmin);
        double hi = iv.hiInf ? xmax : std::min(iv.hi, xmax);
        if (!(hi > lo)) continue;
        float p0 = mapX(lo);
        float p1 = mapX(hi);
        if (p1 - p0 < S(2.0f)) continue;
        dl->AddRectFilled(ImVec2(p0, r0.y), ImVec2(p1, r1.y),
                          ToU32(Alpha(col::Cyan, drawn ? 0.028f : 0.045f)));
        // 区间边界的虚线
        if (!iv.loInf) {
            for (float y = r0.y; y < r1.y; y += S(7.0f)) {
                dl->AddLine(ImVec2(p0, y), ImVec2(p0, Min(y + S(3.5f), r1.y)),
                            ToU32(Alpha(col::Cyan, 0.22f)), 1.0f);
            }
        }
        if (!iv.hiInf) {
            for (float y = r0.y; y < r1.y; y += S(7.0f)) {
                dl->AddLine(ImVec2(p1, y), ImVec2(p1, Min(y + S(3.5f), r1.y)),
                            ToU32(Alpha(col::Cyan, 0.22f)), 1.0f);
            }
        }
        drawn = true;
    }
}

}  // namespace

// ---------------------------------------------------------------
// 右侧函数图像卡片
// ---------------------------------------------------------------
void DrawPlotCard(App& app, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 p1(pos.x + size.x, pos.y + size.y);
    DrawCard(dl, pos, p1, S(16.0f), ToU32(col::Panel), ToU32(col::Border));

    const float pad = S(18.0f);
    // 标题栏
    dl->AddText(gFontHeading, S(24.0f), ImVec2(pos.x + pad, pos.y + S(16.0f)), ToU32(col::Text),
                "函数图像");
    const char* sub = "实时采样";
    float subW = gFontBody->CalcTextSizeA(S(14.0f), FLT_MAX, 0.0f, sub).x;
    dl->AddText(gFontBody, S(14.0f), ImVec2(p1.x - pad - subW, pos.y + S(23.0f)),
                ToU32(col::TextFaint), sub);

    // 图像区域
    ImVec2 r0(pos.x + pad, pos.y + S(58.0f));
    ImVec2 r1(p1.x - pad, p1.y - S(96.0f));
    if (r1.x - r0.x < 40 || r1.y - r0.y < 40) return;

    dl->AddRectFilled(ImVec2(r0.x - 4, r0.y - 4), ImVec2(r1.x + 4, r1.y + 4),
                      ToU32(ImVec4(0.051f, 0.059f, 0.078f, 1.0f)), S(10.0f));
    dl->AddRect(ImVec2(r0.x - 4, r0.y - 4), ImVec2(r1.x + 4, r1.y + 4), ToU32(col::Border), S(10.0f),
                0, 1.0f);

    double xmin, xmax;
    ComputeXRange(app.level, &xmin, &xmax);
    int sampleN = static_cast<int>((r1.x - r0.x) * 2.2f);
    sampleN = Clamp(sampleN, 200, 1600);

    SampleData sd;
    if (app.ast) SampleCurve(app.ast, xmin, xmax, sampleN, &sd);
    double ymin = -1.5, ymax = 1.5;
    if (app.ast) ComputeYRange(sd, &ymin, &ymax);

    dl->PushClipRect(r0, r1, true);
    DrawGrid(dl, r0, r1, xmin, xmax, ymin, ymax, 6);
    DrawConstraintBands(dl, r0, r1, app.level, xmin, xmax);

    if (app.ast && !sd.ys.empty()) {
        auto mapX = [&](double x) {
            return r0.x + static_cast<float>((x - xmin) / (xmax - xmin)) * (r1.x - r0.x);
        };
        auto mapY = [&](double y) {
            return r1.y - static_cast<float>((y - ymin) / (ymax - ymin)) * (r1.y - r0.y);
        };
        ImU32 curveCol = ToU32(col::Cyan);
        ImU32 glowCol = ToU32(Alpha(col::Cyan, 0.10f));
        std::vector<ImVec2> seg;
        auto flush = [&]() {
            if (seg.size() >= 2) {
                dl->AddPolyline(seg.data(), static_cast<int>(seg.size()), glowCol,
                                ImDrawFlags_None, S(7.0f));
                dl->AddPolyline(seg.data(), static_cast<int>(seg.size()), curveCol,
                                ImDrawFlags_None, S(2.2f));
            }
            seg.clear();
        };
        double yspan = ymax - ymin;
        double prevY = 0;
        bool hasPrev = false;
        for (size_t i = 0; i < sd.xs.size(); ++i) {
            if (!sd.ok[i]) {
                flush();
                hasPrev = false;
                continue;
            }
            double y = sd.ys[i];
            // 断点：数值跳变过大（如 tan 的极点）时断开折线
            if (hasPrev && std::fabs(y - prevY) > yspan * 1.6) {
                flush();
            }
            seg.push_back(ImVec2(mapX(sd.xs[i]), mapY(y)));
            prevY = y;
            hasPrev = true;
            if (seg.size() > 4000) flush();
        }
        flush();

        // 悬停读数
        ImVec2 mouse = ImGui::GetMousePos();
        if (mouse.x >= r0.x && mouse.x <= r1.x && mouse.y >= r0.y && mouse.y <= r1.y) {
            double x = xmin + (mouse.x - r0.x) / (r1.x - r0.x) * (xmax - xmin);
            RealResult rr = evalReal(app.ast, "x", x);
            float px = mapX(x);
            for (float y = r0.y; y < r1.y; y += S(6.0f))
                dl->AddLine(ImVec2(px, y), ImVec2(px, Min(y + S(3.0f), r1.y)),
                            ToU32(Alpha(col::TextDim, 0.35f)), 1.0f);
            if (rr.ok && std::isfinite(rr.value)) {
                float py = mapY(rr.value);
                if (py >= r0.y && py <= r1.y) {
                    dl->AddCircleFilled(ImVec2(px, py), S(4.0f), ToU32(col::Amber));
                    dl->AddCircle(ImVec2(px, py), S(7.0f), ToU32(Alpha(col::Amber, 0.4f)), 16,
                                  1.0f);
                }
                char buf[128];
                std::snprintf(buf, sizeof(buf), "x = %s   f(x) = %s", FormatNumber(x).c_str(),
                              FormatNumber(rr.value).c_str());
                ImVec2 ts = gFontBody->CalcTextSizeA(S(13.0f), FLT_MAX, 0.0f, buf);
                ImVec2 tp(px + S(12.0f), mouse.y - S(26.0f));
                if (tp.x + ts.x > r1.x - 4) tp.x = px - ts.x - S(12.0f);
                if (tp.y < r0.y + 2) tp.y = r0.y + 2;
                dl->AddRectFilled(ImVec2(tp.x - 6, tp.y - 4),
                                  ImVec2(tp.x + ts.x + 6, tp.y + ts.y + 4),
                                  ToU32(Alpha(col::Panel, 0.94f)), S(6.0f));
                dl->AddText(gFontBody, S(13.0f), tp, ToU32(col::Text), buf);
            }
        }
    } else {
        const char* msg = app.expr.empty() ? "输入表达式后，这里会实时绘制函数图像"
                                           : "表达式有误，暂无法绘制";
        ImVec2 ts = gFontBody->CalcTextSizeA(S(15.0f), FLT_MAX, 0.0f, msg);
        dl->AddText(gFontBody, S(15.0f),
                    ImVec2((r0.x + r1.x - ts.x) * 0.5f, (r0.y + r1.y) * 0.5f - ts.y * 0.5f),
                    ToU32(col::TextFaint), msg);
    }
    dl->PopClipRect();

    // 底部图例 / 表达式
    float fy = r1.y + S(16.0f);
    dl->AddCircleFilled(ImVec2(pos.x + pad + 5, fy + 8), S(5.0f), ToU32(col::Cyan));
    std::string exprText = app.expr.empty() ? std::string("f(x) = ？") : ("f(x) = " + app.expr);
    exprText = Ellipsize(exprText, size.x - pad * 2 - S(20.0f));
    dl->AddText(gFontMono, S(17.0f), ImVec2(pos.x + pad + 18, fy), ToU32(col::TextDim),
                exprText.c_str());

    char buf[128];
    std::snprintf(buf, sizeof(buf), "x ∈ [%s, %s] · %d 点采样", FormatNumber(xmin).c_str(),
                  FormatNumber(xmax).c_str(), sampleN);
    dl->AddText(gFontBody, S(13.0f), ImVec2(pos.x + pad, fy + S(26.0f)), ToU32(col::TextFaint), buf);

    // 约束区间图例
    if (!app.level.constraints.empty()) {
        float lx = p1.x - pad - S(96.0f);
        dl->AddRectFilled(ImVec2(lx, fy + S(32.0f)), ImVec2(lx + 12, fy + S(42.0f)),
                          ToU32(Alpha(col::Cyan, 0.18f)));
        dl->AddText(gFontBody, S(13.0f), ImVec2(lx + 17, fy + S(29.0f)), ToU32(col::TextFaint),
                    "约束区间");
    }
    ImGui::Dummy(size);
}

// ---------------------------------------------------------------
// 右侧复平面着色图卡片（domain coloring）
// ---------------------------------------------------------------
namespace {

// 复平面区域在着色图上描边/淡填充
void DrawRegionOverlay(ImDrawList* dl, const LevelDef& level, const ImVec2& img0, float side,
                       double cx, double cy, double span) {
    auto mapX = [&](double re) { return img0.x + static_cast<float>((re - cx) / span + 0.5) * side; };
    auto mapY = [&](double im) { return img0.y + static_cast<float>(0.5 - (im - cy) / span) * side; };
    auto inView = [&](double re, double im) {
        return std::fabs(re - cx) <= span * 0.75 && std::fabs(im - cy) <= span * 0.75;
    };
    const ImU32 amber = ToU32(col::Amber);
    const ImU32 regionLine = ToU32(Alpha(col::Cyan, 0.62f));
    const ImU32 regionFill = ToU32(Alpha(col::Cyan, 0.05f));
    for (const auto& c : level.constraints) {
        const ComplexRegion& r = c.region;
        const bool markPoint =
            (c.kind == ConstraintKind::ResidueAt || c.kind == ConstraintKind::PoleOrderAt ||
             c.kind == ConstraintKind::ComplexBoundaryValue);
        const bool drawRegion =
            (c.kind == ConstraintKind::HolomorphicOn || c.kind == ConstraintKind::NoSingularityIn ||
             c.kind == ConstraintKind::ContourIntegralValue ||
             c.kind == ConstraintKind::ZeroCountInRegion ||
             c.kind == ConstraintKind::ModulusRangeOn);
        if (markPoint) {
            float px = mapX(c.cpoint.re);
            float py = mapY(c.cpoint.im);
            if (px > img0.x && px < img0.x + side && py > img0.y && py < img0.y + side) {
                dl->AddCircle(ImVec2(px, py), S(7.0f), amber, 20, S(1.6f));
                dl->AddLine(ImVec2(px - S(4.0f), py), ImVec2(px + S(4.0f), py), amber, S(1.4f));
                dl->AddLine(ImVec2(px, py - S(4.0f)), ImVec2(px, py + S(4.0f)), amber, S(1.4f));
            }
            continue;
        }
        if (!drawRegion) continue;
        switch (r.kind) {
            case ComplexRegion::Kind::Disk: {
                if (!inView(r.center.re, r.center.im)) break;
                float px = mapX(r.center.re), py = mapY(r.center.im);
                float rad = static_cast<float>(r.radius / span) * side;
                dl->AddCircle(ImVec2(px, py), rad, regionLine, 56, S(1.5f));
                break;
            }
            case ComplexRegion::Kind::Annulus: {
                float px = mapX(r.center.re), py = mapY(r.center.im);
                dl->AddCircle(ImVec2(px, py), static_cast<float>(r.radius / span) * side,
                              regionLine, 56, S(1.4f));
                dl->AddCircle(ImVec2(px, py), static_cast<float>(r.outerRadius / span) * side,
                              regionLine, 56, S(1.4f));
                break;
            }
            case ComplexRegion::Kind::Rectangle: {
                ImVec2 a(mapX(r.x0), mapY(r.y1));
                ImVec2 b(mapX(r.x1), mapY(r.y0));
                dl->AddRectFilled(a, b, regionFill);
                dl->AddRect(a, b, regionLine, 0, 0, S(1.5f));
                break;
            }
            case ComplexRegion::Kind::HalfPlaneUpper:
                dl->AddRectFilled(ImVec2(img0.x, mapY(r.yLine)),
                                  ImVec2(img0.x + side, img0.y + side), regionFill);
                dl->AddLine(ImVec2(img0.x, mapY(r.yLine)), ImVec2(img0.x + side, mapY(r.yLine)),
                            regionLine, S(1.5f));
                break;
            case ComplexRegion::Kind::HalfPlaneLower:
                dl->AddRectFilled(ImVec2(img0.x, img0.y),
                                  ImVec2(img0.x + side, mapY(r.yLine)), regionFill);
                dl->AddLine(ImVec2(img0.x, mapY(r.yLine)), ImVec2(img0.x + side, mapY(r.yLine)),
                            regionLine, S(1.5f));
                break;
            case ComplexRegion::Kind::HalfPlaneRight:
                dl->AddRectFilled(ImVec2(mapX(r.xLine), img0.y),
                                  ImVec2(img0.x + side, img0.y + side), regionFill);
                dl->AddLine(ImVec2(mapX(r.xLine), img0.y), ImVec2(mapX(r.xLine), img0.y + side),
                            regionLine, S(1.5f));
                break;
            case ComplexRegion::Kind::ExteriorOfDisk: {
                float px = mapX(r.center.re), py = mapY(r.center.im);
                dl->AddCircle(ImVec2(px, py), static_cast<float>(r.radius / span) * side,
                              regionLine, 56, S(1.4f));
                break;
            }
            default: break;
        }
    }
}

}  // namespace

void DrawComplexPlotCard(App& app, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 p1(pos.x + size.x, pos.y + size.y);
    DrawCard(dl, pos, p1, S(16.0f), ToU32(col::Panel), ToU32(col::Border));

    const float pad = S(18.0f);
    const float headerH = S(58.0f);
    const float innerW = size.x - pad * 2.0f;
    const float innerH = size.y - pad;

    // 标题
    dl->AddText(gFontHeading, S(23.0f), ImVec2(pos.x + pad, pos.y + S(15.0f)), ToU32(col::Text),
                "复平面着色图");
    const char* sub = "Domain Coloring";
    float subW = gFontBody->CalcTextSizeA(S(13.0f), FLT_MAX, 0.0f, sub).x;
    dl->AddText(gFontBody, S(13.0f), ImVec2(p1.x - pad - subW, pos.y + S(23.0f)),
                ToU32(col::TextFaint), sub);

    // 图像尺寸：正方形，占满可用宽度
    float side = Min(innerW, innerH - headerH - S(150.0f));
    const bool small = side < S(160.0f);
    if (small) side = Max(innerW, S(120.0f));
    ImVec2 img0(pos.x + pad + (innerW - side) * 0.5f, pos.y + headerH);
    ImVec2 img1(img0.x + side, img0.y + side);

    // ---- 交互：拖拽平移 / 滚轮缩放 / 双击复位 ----
    ImGui::SetCursorScreenPos(img0);
    ImGui::InvisibleButton("##complex_plot", ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();
    double span = app.complexSpanRe;
    if (hovered && io.MouseWheel != 0.0f) {
        double factor = std::pow(0.86, static_cast<double>(io.MouseWheel));
        span = Clamp(span * factor, 0.05, 60.0);
        if (std::fabs(span - app.complexSpanRe) > 1e-9) {
            app.complexSpanRe = static_cast<float>(span);
            app.markComplexDirty();
        }
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 d = io.MouseDelta;
        if (std::fabs(d.x) > 0.0f || std::fabs(d.y) > 0.0f) {
            app.complexCenter.x -= static_cast<float>(d.x / side * app.complexSpanRe);
            app.complexCenter.y += static_cast<float>(d.y / side * app.complexSpanRe);
            app.markComplexDirty();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        app.complexCenter = ImVec2(0.0f, 0.0f);
        app.complexSpanRe = 5.0f;
        app.markComplexDirty();
    }
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // ---- 计算 / 上传纹理（去抖 250ms + 分帧，避免卡主线程） ----
    int pixW = static_cast<int>(std::lround(ToLogical(side) * 0.95f));
    pixW = Clamp(pixW, 180, 420);
    app.updateComplexPlot(pixW, pixW);

    // ---- 绘制图像 ----
    dl->AddRectFilled(ImVec2(img0.x - S(4.0f), img0.y - S(4.0f)),
                      ImVec2(img1.x + S(4.0f), img1.y + S(4.0f)),
                      ToU32(ImVec4(0.043f, 0.051f, 0.070f, 1.0f)), S(10.0f));
    if (app.complexValid && app.complexTex.srv) {
        dl->AddImage(TextureId(app.complexTex), img0, img1);
    } else {
        dl->AddRectFilled(img0, img1, ToU32(ImVec4(0.055f, 0.063f, 0.082f, 1.0f)), S(4.0f));
        const char* msg = app.expr.empty() ? "输入含 z 的表达式后显示复平面"
                                           : (!app.exprValid ? "表达式有误，无法着色" : "");
        if (msg[0]) {
            ImVec2 ts = gFontBody->CalcTextSizeA(S(14.0f), FLT_MAX, 0.0f, msg);
            dl->AddText(gFontBody, S(14.0f),
                        ImVec2((img0.x + img1.x - ts.x) * 0.5f,
                               (img0.y + img1.y) * 0.5f - ts.y * 0.5f),
                        ToU32(col::TextFaint), msg);
        }
    }
    dl->AddRect(ImVec2(img0.x - S(4.0f), img0.y - S(4.0f)),
                ImVec2(img1.x + S(4.0f), img1.y + S(4.0f)), ToU32(col::Border), S(10.0f), 0,
                S(1.0f));

    // 完成图像后绘制叠加层
    const double cx = app.complexCenter.x;
    const double cy = app.complexCenter.y;
    const double spanNow = app.complexSpanRe;
    auto mapX = [&](double re) { return img0.x + static_cast<float>((re - cx) / spanNow + 0.5) * side; };
    auto mapY = [&](double im) { return img0.y + static_cast<float>(0.5 - (im - cy) / spanNow) * side; };
    auto invX = [&](float px) { return cx + (static_cast<double>(px - img0.x) / side - 0.5) * spanNow; };
    auto invY = [&](float py) { return cy + (0.5 - static_cast<double>(py - img0.y) / side) * spanNow; };

    if (app.complexValid) {
        // 复平面区域约束叠加
        DrawRegionOverlay(dl, app.level, img0, side, cx, cy, spanNow);

        // 坐标轴
        ImU32 axisCol = ToU32(Alpha(col::White, 0.35f));
        if (cx - spanNow * 0.5 < 0.0 && cx + spanNow * 0.5 > 0.0) {
            float px = mapX(0.0);
            dl->AddLine(ImVec2(px, img0.y), ImVec2(px, img0.y + side), axisCol, S(1.2f));
        }
        if (cy - spanNow * 0.5 < 0.0 && cy + spanNow * 0.5 > 0.0) {
            float py = mapY(0.0);
            dl->AddLine(ImVec2(img0.x, py), ImVec2(img0.x + side, py), axisCol, S(1.2f));
        }
        // 刻度
        double step = NiceStep(spanNow, 4);
        ImU32 tickCol = ToU32(Alpha(col::White, 0.55f));
        ImU32 labCol = ToU32(Alpha(col::White, 0.75f));
        for (double re = std::ceil((cx - spanNow * 0.5) / step) * step;
             re <= cx + spanNow * 0.5 + 1e-9; re += step) {
            float px = mapX(re);
            if (px < img0.x + S(6.0f) || px > img0.x + side - S(6.0f)) continue;
            dl->AddLine(ImVec2(px, img0.y + side - S(5.0f)), ImVec2(px, img0.y + side),
                        tickCol, S(1.2f));
            std::string t = FormatNumber(re);
            ImVec2 ts = gFontBody->CalcTextSizeA(S(11.0f), FLT_MAX, 0.0f, t.c_str());
            dl->AddText(gFontBody, S(11.0f),
                        ImVec2(px - ts.x * 0.5f, img0.y + side + S(3.0f)), labCol, t.c_str());
        }
        for (double im = std::ceil((cy - spanNow * 0.5) / step) * step;
             im <= cy + spanNow * 0.5 + 1e-9; im += step) {
            float py = mapY(im);
            if (py < img0.y + S(6.0f) || py > img0.y + side - S(6.0f)) continue;
            dl->AddLine(ImVec2(img0.x, py), ImVec2(img0.x + S(5.0f), py), tickCol, S(1.2f));
            std::string t = FormatNumber(im);
            ImVec2 ts = gFontBody->CalcTextSizeA(S(11.0f), FLT_MAX, 0.0f, t.c_str());
            dl->AddText(gFontBody, S(11.0f),
                        ImVec2(img0.x + S(6.0f), py - ts.y * 0.5f), labCol, t.c_str());
        }
        // 轴标签
        dl->AddText(gFontBody, S(11.0f), ImVec2(img0.x + side - S(18.0f), mapY(0.0) - S(16.0f)),
                    labCol, "Re");
        dl->AddText(gFontBody, S(11.0f), ImVec2(mapX(0.0) + S(6.0f), img0.y + S(4.0f)), labCol,
                    "Im");

        // 鼠标探针：z 与 f(z)
        if (hovered) {
            float mx = io.MousePos.x, my = io.MousePos.y;
            if (mx >= img0.x && mx <= img0.x + side && my >= img0.y && my <= img0.y + side) {
                double zr = invX(mx), zi = invY(my);
                VarMap vars;
                vars[app.level.primaryVar()] = Cplx(zr, zi);
                EvalResult ev = app.ast ? evaluate(app.ast, vars) : EvalResult{};
                dl->AddLine(ImVec2(mx, img0.y), ImVec2(mx, img0.y + side),
                            ToU32(Alpha(col::White, 0.20f)), S(1.0f));
                dl->AddLine(ImVec2(img0.x, my), ImVec2(img0.x + side, my),
                            ToU32(Alpha(col::White, 0.20f)), S(1.0f));
                char buf[192];
                std::string zs = FormatComplex(Cplx(zr, zi));
                std::string ws =
                    (ev.ok && ev.value.isFinite()) ? FormatComplex(ev.value) : std::string("∞ / 无定义");
                std::snprintf(buf, sizeof(buf), "z = %s   f(z) = %s", zs.c_str(), ws.c_str());
                ImVec2 ts = gFontBody->CalcTextSizeA(S(12.0f), FLT_MAX, 0.0f, buf);
                ImVec2 tp(img0.x + S(6.0f), img0.y + side - ts.y - S(8.0f));
                if (mx - tp.x < ts.x + S(20.0f)) tp.y = img0.y + S(8.0f);
                dl->AddRectFilled(ImVec2(tp.x - S(5.0f), tp.y - S(3.0f)),
                                  ImVec2(tp.x + ts.x + S(5.0f), tp.y + ts.y + S(3.0f)),
                                  ToU32(ImVec4(0.02f, 0.03f, 0.05f, 0.82f)), S(5.0f));
                dl->AddText(gFontBody, S(12.0f), tp, ToU32(col::Text), buf);
            }
        }
    }

    // 计算中/去抖进度覆盖
    if (!app.complexValid || app.complexDirty || app.complexComputing) {
        int pct = 0;
        if (app.complexComputing && app.complexPixH > 0)
            pct = static_cast<int>(100.0f * app.complexRow / app.complexPixH);
        if (app.complexDirty && !app.complexComputing) pct = 0;
        if (pct < 100 && app.exprValid) {
            dl->AddRectFilled(img0, ImVec2(img0.x + side, img0.y + side),
                              ToU32(ImVec4(0.02f, 0.03f, 0.05f, 0.45f)));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "计算中… %d%%", pct);
            ImVec2 ts = gFontBody->CalcTextSizeA(S(14.0f), FLT_MAX, 0.0f, buf);
            dl->AddText(gFontBody, S(14.0f),
                        ImVec2((img0.x + img1.x - ts.x) * 0.5f, img0.y + side * 0.5f - ts.y),
                        ToU32(col::Cyan), buf);
            float barW = side * 0.5f;
            float barX = img0.x + (side - barW) * 0.5f;
            float barY = img0.y + side * 0.5f + S(8.0f);
            dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + S(4.0f)),
                              ToU32(Alpha(col::White, 0.12f)), S(2.0f));
            if (pct > 0)
                dl->AddRectFilled(ImVec2(barX, barY),
                                  ImVec2(barX + barW * pct / 100.0f, barY + S(4.0f)),
                                  ToU32(col::Cyan), S(2.0f));
        }
    }

    // 视野范围（图内右上角）
    {
        auto fmtRange = [](double c, double span) {
            std::string s = "[" + FormatNumber(c - span * 0.5) + ", " +
                            FormatNumber(c + span * 0.5) + "]";
            return s;
        };
        std::string range = "Re " + fmtRange(cx, spanNow) + "  Im " + fmtRange(cy, spanNow);
        ImVec2 ts = gFontBody->CalcTextSizeA(S(11.5f), FLT_MAX, 0.0f, range.c_str());
        ImVec2 r0(img0.x + side - ts.x - S(18.0f), img0.y + S(6.0f));
        ImVec2 r1(r0.x + ts.x + S(12.0f), r0.y + ts.y + S(6.0f));
        dl->AddRectFilled(r0, r1, ToU32(ImVec4(0.02f, 0.03f, 0.05f, 0.62f)), S(5.0f));
        dl->AddText(gFontBody, S(11.5f), ImVec2(r0.x + S(6.0f), r0.y + S(3.0f)),
                    ToU32(Alpha(col::Text, 0.85f)), range.c_str());
    }

    // ---- 底部信息区 ----
    float fy = img1.y + S(14.0f);
    float fx = pos.x + pad;
    float footerW = size.x - pad * 2.0f;

    // 色相条（辐角图例）
    {
        const int n = 24;
        float bw = S(8.0f);
        float bh = S(8.0f);
        for (int i = 0; i < n; ++i) {
            float t = static_cast<float>(i) / n;
            float r = 0.5f + 0.5f * std::cos(t * 6.2831853f);
            float g = 0.5f + 0.5f * std::cos(t * 6.2831853f - 2.0943951f);
            float b = 0.5f + 0.5f * std::cos(t * 6.2831853f + 2.0943951f);
            dl->AddRectFilled(ImVec2(fx + i * bw, fy), ImVec2(fx + (i + 1) * bw, fy + bh),
                              ToU32(ImVec4(r, g, b, 1.0f)));
        }
        dl->AddText(gFontBody, S(12.0f), ImVec2(fx + n * bw + S(10.0f), fy - S(2.0f)),
                    ToU32(col::TextDim), "色相 = arg f(z) · 明度 = |f(z)|");
        fy += S(18.0f);
    }
    // 视野范围 + 交互提示
    {
        auto fmtRange = [](double c, double span) {
            std::string s = "[" + FormatNumber(c - span * 0.5) + ", " + FormatNumber(c + span * 0.5) + "]";
            return s;
        };
        std::string info = "Re ∈ " + fmtRange(cx, spanNow) + "  Im ∈ " + fmtRange(cy, spanNow);
        dl->AddText(gFontBody, S(12.0f), ImVec2(fx, fy), ToU32(col::TextDim), info.c_str());
        const char* tips = "滚轮缩放 · 拖拽平移 · 双击复位";
        float tw = gFontBody->CalcTextSizeA(S(11.5f), FLT_MAX, 0.0f, tips).x;
        dl->AddText(gFontBody, S(11.5f), ImVec2(fx + footerW - tw, fy + S(1.0f)),
                    ToU32(col::TextFaint), tips);
        fy += S(18.0f);
    }
    // 相位带 / 等模线 / 渲染数据
    {
        struct Chip {
            const char* text;
            const ImVec4* color;
        };
        Chip chips[] = {{"相位带 ×12", &col::Cyan}, {"等模线", &col::Amber}};
        float cxp = fx;
        for (const auto& c : chips) {
            float tw = gFontBody->CalcTextSizeA(S(11.0f), FLT_MAX, 0.0f, c.text).x;
            ImVec2 q0(cxp, fy);
            ImVec2 q1(cxp + tw + S(16.0f), fy + S(20.0f));
            dl->AddRectFilled(q0, q1, ToU32(Alpha(*c.color, 0.10f)), S(6.0f));
            dl->AddText(gFontBody, S(11.0f), ImVec2(q0.x + S(8.0f), q0.y + S(3.0f)),
                        ToU32(*c.color), c.text);
            cxp += tw + S(22.0f);
        }
        // 渲染数据（含性能）
        char stat[128];
        if (!app.exprValid || app.complexPixW <= 0) {
            std::snprintf(stat, sizeof(stat), "等待有效表达式");
        } else if (app.complexComputing) {
            std::snprintf(stat, sizeof(stat), "%d×%d · 计算中 %d%%", app.complexPixW,
                          app.complexPixH,
                          app.complexPixH > 0 ? static_cast<int>(100.0f * app.complexRow / app.complexPixH) : 0);
        } else {
            std::snprintf(stat, sizeof(stat), "%d×%d · %.1f ms", app.complexPixW, app.complexPixH,
                          app.complexComputeMs);
        }
        float sw = gFontBody->CalcTextSizeA(S(11.0f), FLT_MAX, 0.0f, stat).x;
        dl->AddText(gFontBody, S(11.0f), ImVec2(fx + footerW - sw, fy + S(3.0f)),
                    ToU32(Alpha(col::TextFaint, 0.9f)), stat);
        fy += S(26.0f);
    }
    // 复位按钮
    ImGui::SetCursorScreenPos(ImVec2(fx, fy));
    if (FunButton("complex_reset", "复位视图", ImVec2(S(96.0f), S(26.0f)), StyleGhost(), true,
                  Icon::Retry, gFontBody)) {
        app.complexCenter = ImVec2(0.0f, 0.0f);
        app.complexSpanRe = 5.0f;
        app.markComplexDirty();
    }
    ImGui::Dummy(ImVec2(1.0f, S(2.0f)));
}

// ---------------------------------------------------------------
// 主菜单背景装饰曲线
// ---------------------------------------------------------------
void DrawMenuCanvas(App& app, const ImVec2& r0, const ImVec2& r1) {
    (void)app;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = r1.x - r0.x;
    float h = r1.y - r0.y;
    if (w < 40 || h < 40) return;

    // 网格
    ImU32 gridCol = ToU32(Alpha(col::Cyan, 0.05f));
    float step = S(38.0f);
    for (float x = r0.x; x <= r1.x; x += step)
        dl->AddLine(ImVec2(x, r0.y), ImVec2(x, r1.y), gridCol, 1.0f);
    for (float y = r0.y; y <= r1.y; y += step)
        dl->AddLine(ImVec2(r0.x, y), ImVec2(r1.x, y), gridCol, 1.0f);

    float cx = (r0.x + r1.x) * 0.5f;
    float cy = (r0.y + r1.y) * 0.5f;
    ImU32 axisCol = ToU32(Alpha(col::TextDim, 0.28f));
    dl->AddLine(ImVec2(r0.x, cy), ImVec2(r1.x, cy), axisCol, S(1.2f));
    dl->AddLine(ImVec2(cx, r0.y), ImVec2(cx, r1.y), axisCol, S(1.2f));

    // 曲线：sin 与 cos
    auto drawCurve = [&](double (*fn)(double), double amp, double freq, ImU32 color,
                         float thick) {
        std::vector<ImVec2> pts;
        int n = static_cast<int>(w);
        pts.reserve(static_cast<size_t>(n));
        for (int i = 0; i <= n; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(n);
            double x = (t - 0.5) * 4.0 * kPi;
            double y = fn(x * freq) * amp;
            pts.push_back(ImVec2(r0.x + t * w, cy - static_cast<float>(y) * h * 0.34));
        }
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()),
                        AlphaU32(color, 0.10f), ImDrawFlags_None, thick + S(5.0f));
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), color, ImDrawFlags_None,
                        thick);
    };
    drawCurve(static_cast<double (*)(double)>(std::sin), 1.0, 1.0, ToU32(col::Cyan), S(2.2f));
    drawCurve(static_cast<double (*)(double)>(std::cos), 0.62, 2.0,
              ToU32(Alpha(col::Amber, 0.75f)), S(1.8f));

    // 边缘渐隐（模拟 vignette）
    ImU32 fade = ToU32(col::Bg);
    (void)fade;
}

}  // namespace ui
}  // namespace fcg
