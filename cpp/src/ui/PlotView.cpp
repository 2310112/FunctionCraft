// PlotView.cpp — 函数图像绘制（ImGui draw list 手绘）
// 采样 evaluateAt()，逐像素绘制，定义域断点自动断开，约束区间淡色高亮。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "App.h"
#include "LevelUi.h"
#include "core/Numeric.h"
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
        dl->AddLine(ImVec2(r0.x, py), ImVec2(r1.x, py), axisCol, 1.4f);
    }
    if (xmin < 0 && xmax > 0) {
        float px = mapX(0.0);
        dl->AddLine(ImVec2(px, r0.y), ImVec2(px, r1.y), axisCol, 1.4f);
    }

    // 刻度文字
    float fs = 12.0f;
    ImU32 tickCol = ToU32(col::TextFaint);
    float xLabelY = (ymin < 0 && ymax > 0) ? Clamp(mapY(0.0) + 5.0f, r0.y, r1.y - fs - 2.0f)
                                           : r1.y - fs - 3.0f;
    double lastLabelX = -1e308;
    for (double x = xstart; x <= xmax + 1e-9; x += xstep) {
        if (std::fabs(x) < xstep * 1e-6) continue;
        float px = mapX(x);
        if (px < r0.x + 8 || px > r1.x - 8) continue;
        if (px - lastLabelX < 54.0f) continue;
        std::string s = TickLabel(x);
        ImVec2 ts = gFontBody->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str());
        dl->AddText(gFontBody, fs, ImVec2(px - ts.x * 0.5f, xLabelY), tickCol, s.c_str());
        lastLabelX = px;
    }
    if (xmin < 0 && xmax > 0) {
        float px = mapX(0.0);
        ImVec2 ts = gFontBody->CalcTextSizeA(fs, FLT_MAX, 0.0f, "0");
        dl->AddText(gFontBody, fs, ImVec2(px - ts.x - 4.0f, r1.y - fs - 3.0f), tickCol, "0");
    }
    for (double y = ystart; y <= ymax + 1e-9; y += ystep) {
        if (std::fabs(y) < ystep * 1e-6) continue;
        float py = mapY(y);
        if (py < r0.y + 8 || py > r1.y - 8) continue;
        std::string s = TickLabel(y);
        ImVec2 ts = gFontBody->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str());
        float tx = r0.x + 5.0f;
        if (xmin < 0 && xmax > 0) tx = Max(r0.x + 5.0f, mapX(0.0) + 5.0f);
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
        if (p1 - p0 < 2.0f) continue;
        dl->AddRectFilled(ImVec2(p0, r0.y), ImVec2(p1, r1.y),
                          ToU32(Alpha(col::Cyan, drawn ? 0.028f : 0.045f)));
        // 区间边界的虚线
        if (!iv.loInf) {
            for (float y = r0.y; y < r1.y; y += 7.0f) {
                dl->AddLine(ImVec2(p0, y), ImVec2(p0, Min(y + 3.5f, r1.y)),
                            ToU32(Alpha(col::Cyan, 0.22f)), 1.0f);
            }
        }
        if (!iv.hiInf) {
            for (float y = r0.y; y < r1.y; y += 7.0f) {
                dl->AddLine(ImVec2(p1, y), ImVec2(p1, Min(y + 3.5f, r1.y)),
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
    DrawCard(dl, pos, p1, 16.0f, ToU32(col::Panel), ToU32(col::Border));

    const float pad = 18.0f;
    // 标题栏
    dl->AddText(gFontHeading, 24.0f, ImVec2(pos.x + pad, pos.y + 16.0f), ToU32(col::Text),
                "函数图像");
    const char* sub = "实时采样";
    float subW = gFontBody->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, sub).x;
    dl->AddText(gFontBody, 14.0f, ImVec2(p1.x - pad - subW, pos.y + 23.0f),
                ToU32(col::TextFaint), sub);

    // 图像区域
    ImVec2 r0(pos.x + pad, pos.y + 58.0f);
    ImVec2 r1(p1.x - pad, p1.y - 96.0f);
    if (r1.x - r0.x < 40 || r1.y - r0.y < 40) return;

    dl->AddRectFilled(ImVec2(r0.x - 4, r0.y - 4), ImVec2(r1.x + 4, r1.y + 4),
                      ToU32(ImVec4(0.051f, 0.059f, 0.078f, 1.0f)), 10.0f);
    dl->AddRect(ImVec2(r0.x - 4, r0.y - 4), ImVec2(r1.x + 4, r1.y + 4), ToU32(col::Border), 10.0f,
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
                                ImDrawFlags_None, 7.0f);
                dl->AddPolyline(seg.data(), static_cast<int>(seg.size()), curveCol,
                                ImDrawFlags_None, 2.2f);
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
            for (float y = r0.y; y < r1.y; y += 6.0f)
                dl->AddLine(ImVec2(px, y), ImVec2(px, Min(y + 3.0f, r1.y)),
                            ToU32(Alpha(col::TextDim, 0.35f)), 1.0f);
            if (rr.ok && std::isfinite(rr.value)) {
                float py = mapY(rr.value);
                if (py >= r0.y && py <= r1.y) {
                    dl->AddCircleFilled(ImVec2(px, py), 4.0f, ToU32(col::Amber));
                    dl->AddCircle(ImVec2(px, py), 7.0f, ToU32(Alpha(col::Amber, 0.4f)), 16,
                                  1.0f);
                }
                char buf[128];
                std::snprintf(buf, sizeof(buf), "x = %s   f(x) = %s", FormatNumber(x).c_str(),
                              FormatNumber(rr.value).c_str());
                ImVec2 ts = gFontBody->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, buf);
                ImVec2 tp(px + 12.0f, mouse.y - 26.0f);
                if (tp.x + ts.x > r1.x - 4) tp.x = px - ts.x - 12.0f;
                if (tp.y < r0.y + 2) tp.y = r0.y + 2;
                dl->AddRectFilled(ImVec2(tp.x - 6, tp.y - 4),
                                  ImVec2(tp.x + ts.x + 6, tp.y + ts.y + 4),
                                  ToU32(Alpha(col::Panel, 0.94f)), 6.0f);
                dl->AddText(gFontBody, 13.0f, tp, ToU32(col::Text), buf);
            }
        }
    } else {
        const char* msg = app.expr.empty() ? "输入表达式后，这里会实时绘制函数图像"
                                           : "表达式有误，暂无法绘制";
        ImVec2 ts = gFontBody->CalcTextSizeA(15.0f, FLT_MAX, 0.0f, msg);
        dl->AddText(gFontBody, 15.0f,
                    ImVec2((r0.x + r1.x - ts.x) * 0.5f, (r0.y + r1.y) * 0.5f - ts.y * 0.5f),
                    ToU32(col::TextFaint), msg);
    }
    dl->PopClipRect();

    // 底部图例 / 表达式
    float fy = r1.y + 16.0f;
    dl->AddCircleFilled(ImVec2(pos.x + pad + 5, fy + 8), 5.0f, ToU32(col::Cyan));
    std::string exprText = app.expr.empty() ? std::string("f(x) = ？") : ("f(x) = " + app.expr);
    exprText = Ellipsize(exprText, size.x - pad * 2 - 20.0f);
    dl->AddText(gFontMono, 17.0f, ImVec2(pos.x + pad + 18, fy), ToU32(col::TextDim),
                exprText.c_str());

    char buf[128];
    std::snprintf(buf, sizeof(buf), "x ∈ [%s, %s] · %d 点采样", FormatNumber(xmin).c_str(),
                  FormatNumber(xmax).c_str(), sampleN);
    dl->AddText(gFontBody, 13.0f, ImVec2(pos.x + pad, fy + 26.0f), ToU32(col::TextFaint), buf);

    // 约束区间图例
    if (!app.level.constraints.empty()) {
        float lx = p1.x - pad - 96.0f;
        dl->AddRectFilled(ImVec2(lx, fy + 32.0f), ImVec2(lx + 12, fy + 42.0f),
                          ToU32(Alpha(col::Cyan, 0.18f)));
        dl->AddText(gFontBody, 13.0f, ImVec2(lx + 17, fy + 29.0f), ToU32(col::TextFaint),
                    "约束区间");
    }
    ImGui::Dummy(size);
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
    float step = 38.0f;
    for (float x = r0.x; x <= r1.x; x += step)
        dl->AddLine(ImVec2(x, r0.y), ImVec2(x, r1.y), gridCol, 1.0f);
    for (float y = r0.y; y <= r1.y; y += step)
        dl->AddLine(ImVec2(r0.x, y), ImVec2(r1.x, y), gridCol, 1.0f);

    float cx = (r0.x + r1.x) * 0.5f;
    float cy = (r0.y + r1.y) * 0.5f;
    ImU32 axisCol = ToU32(Alpha(col::TextDim, 0.28f));
    dl->AddLine(ImVec2(r0.x, cy), ImVec2(r1.x, cy), axisCol, 1.2f);
    dl->AddLine(ImVec2(cx, r0.y), ImVec2(cx, r1.y), axisCol, 1.2f);

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
                        AlphaU32(color, 0.10f), ImDrawFlags_None, thick + 5.0f);
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), color, ImDrawFlags_None,
                        thick);
    };
    drawCurve(static_cast<double (*)(double)>(std::sin), 1.0, 1.0, ToU32(col::Cyan), 2.2f);
    drawCurve(static_cast<double (*)(double)>(std::cos), 0.62, 2.0,
              ToU32(Alpha(col::Amber, 0.75f)), 1.8f);

    // 边缘渐隐（模拟 vignette）
    ImU32 fade = ToU32(col::Bg);
    (void)fade;
}

}  // namespace ui
}  // namespace fcg
