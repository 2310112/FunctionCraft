// main.cpp — Win32 + DirectX11 + Dear ImGui 平台外壳
// 职责：窗口/设备/ImGui 初始化与主循环；具体界面在 App.cpp 的 fcg::ui::frame()
#include <windows.h>

#include <d3d11.h>
#include <tchar.h>

// ImGui
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

// App
#include "App.h"
#include "ShotConfig.h"
#include "Theme.h"
#include "UiScale.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace fcg { namespace ui {
ShotConfig g_shotConfig;
float gUiScale = 1.0f;  // 由 DPI 决定，见 ComputeUiScale()
} }

// ---------------------------------------------------------------
// DirectX11 全局
// ---------------------------------------------------------------
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// 截图/调试参数
static int g_forceWidth = 0;
static int g_forceHeight = 0;
static float g_forceUiScale = 0.0f;  // >0 时覆盖 DPI 推算值（截图验收用）

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------
// D3D 辅助
// ---------------------------------------------------------------
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0,
                                                    D3D_FEATURE_LEVEL_10_0};
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel,
        &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                            createDeviceFlags, featureLevelArray, 2,
                                            D3D11_SDK_VERSION, &sd, &g_pSwapChain,
                                            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_ResizeWidth = (UINT)LOWORD(lParam);
            g_ResizeHeight = (UINT)HIWORD(lParam);
            return 0;
        case WM_GETMINMAXINFO: {
            // 最小窗口：1024x640 逻辑像素
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            float sc = fcg::ui::gUiScale > 0.0f ? fcg::ui::gUiScale : 1.0f;
            mmi->ptMinTrackSize.x = static_cast<LONG>(1024 * sc);
            mmi->ptMinTrackSize.y = static_cast<LONG>(640 * sc);
            return 0;
        }
        case WM_DPICHANGED: {
            // 拖到不同缩放比例的显示器：更新缩放并重建字体
            fcg::ui::gUiScale = static_cast<float>(HIWORD(wParam)) / 96.0f;
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            ::SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                           suggested->right - suggested->left, suggested->bottom - suggested->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            fcg::ui::RebuildFontsForScale();
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------
// 中文字体
// ---------------------------------------------------------------
static void LoadChineseFont(ImGuiIO& io) {
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = false;

    const char* candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",    // 微软雅黑
        "C:\\Windows\\Fonts\\msyhbd.ttc",  // 微软雅黑粗体
        "C:\\Windows\\Fonts\\simhei.ttf",  // 黑体
        "C:\\Windows\\Fonts\\simsun.ttc",  // 宋体
    };
    for (const char* path : candidates) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        ImFont* f = io.Fonts->AddFontFromFileTTF(path, 19.0f, &cfg,
                                                 io.Fonts->GetGlyphRangesChineseFull());
        if (f) return;
    }
    io.Fonts->AddFontDefault();
}

// ---------------------------------------------------------------
// DPI 变化：重建字体图集
// ---------------------------------------------------------------
namespace fcg {
namespace ui {
void RebuildFontsForScale() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplDX11_InvalidateDeviceObjects();
    io.Fonts->Clear();
    SetupFonts("", "");
    ImGui_ImplDX11_CreateDeviceObjects();
    ApplyTheme();
    std::printf("[DPI] fonts rebuilt for scale %.2f\n", gUiScale);
}
}  // namespace ui
}  // namespace fcg

// ---------------------------------------------------------------
// DPI / 窗口尺寸
// ---------------------------------------------------------------
static void ComputeUiScale(HWND hwnd) {
    float scale = 1.0f;
    if (g_forceUiScale > 0.0f) {
        scale = g_forceUiScale;
    } else {
        UINT dpi = 96;
        // GetDpiForWindow 需要 Win10 1607+；找不到就退回系统 DPI
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        if (user32) {
            using PFN_GetDpiForWindow = UINT(WINAPI*)(HWND);
            auto fn = reinterpret_cast<PFN_GetDpiForWindow>(
                ::GetProcAddress(user32, "GetDpiForWindow"));
            if (fn) dpi = fn(hwnd);
        }
        if (dpi == 0) dpi = 96;
        scale = static_cast<float>(dpi) / 96.0f;
    }
    if (scale < 0.75f) scale = 0.75f;
    if (scale > 4.0f) scale = 4.0f;
    fcg::ui::gUiScale = scale;
    std::printf("[DPI] ui scale = %.2f\n", scale);
}

// 默认窗口尺寸：工作区的 90%，但不小于 1280x800 逻辑像素
static void DefaultWindowSize(int* outW, int* outH) {
    RECT wa{0, 0, 1920, 1080};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int availW = wa.right - wa.left;
    int availH = wa.bottom - wa.top;
    float scale = fcg::ui::gUiScale > 0.0f ? fcg::ui::gUiScale : 1.0f;
    // 设计稿是 1440x900 逻辑像素：按缩放换算成物理像素，再用工作区 96% 封顶
    int wantW = static_cast<int>(1440 * scale);
    int wantH = static_cast<int>(900 * scale);
    int maxW = static_cast<int>(availW * 0.96f);
    int maxH = static_cast<int>(availH * 0.96f);
    int minW = static_cast<int>(1024 * scale);
    int minH = static_cast<int>(640 * scale);
    int w = wantW < maxW ? wantW : maxW;
    int h = wantH < maxH ? wantH : maxH;
    if (w < minW) w = minW;
    if (h < minH) h = minH;
    if (w > availW) w = availW;
    if (h > availH) h = availH;
    *outW = w;
    *outH = h;
}

// ---------------------------------------------------------------
// 截图支持（调试/自动验收用）
// ---------------------------------------------------------------
static std::string g_shotPath;
static int g_shotFrames = 10;
static int g_frameCounter = 0;

static void ParseCommandLine() {
    std::string cmd = ::GetCommandLineA();
    auto take = [&](const char* key) -> std::string {
        std::string k(key);
        size_t pos = cmd.find(k);
        if (pos == std::string::npos) return "";
        pos += k.size();
        size_t end = cmd.find_first_of(" \"", pos);
        return cmd.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    g_shotPath = take("--shot=");
    std::string size = take("--size=");
    if (!size.empty()) {
        size_t x = size.find('x');
        if (x == std::string::npos) x = size.find('X');
        if (x != std::string::npos) {
            g_forceWidth = std::atoi(size.substr(0, x).c_str());
            g_forceHeight = std::atoi(size.substr(x + 1).c_str());
        }
    }
    std::string dpi = take("--dpi-scale=");
    if (!dpi.empty()) g_forceUiScale = static_cast<float>(atof(dpi.c_str()));
    std::string frames = take("--frames=");
    if (!frames.empty()) g_shotFrames = std::max(1, atoi(frames.c_str()));
    fcg::ui::g_shotConfig.scene = take("--scene=");
    fcg::ui::g_shotConfig.levelId = take("--level=");
    fcg::ui::g_shotConfig.expression = take("--expr=");
}

// 离屏渲染：把 ImGui 画到指定尺寸的纹理上再存 PNG（不受窗口/屏幕尺寸限制）
static bool RenderOffscreenAndSave(const std::string& path, int w, int h) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_pd3dDevice->CreateTexture2D(&td, nullptr, &tex)) || !tex) return false;
    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(g_pd3dDevice->CreateRenderTargetView(tex, nullptr, &rtv))) {
        tex->Release();
        return false;
    }

    const float clear[4] = {0.08f, 0.09f, 0.11f, 1.0f};
    D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    g_pd3dDeviceContext->OMSetRenderTargets(1, &rtv, nullptr);
    g_pd3dDeviceContext->RSSetViewports(1, &vp);
    g_pd3dDeviceContext->ClearRenderTargetView(rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // 回读到 CPU
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    bool ok = false;
    if (SUCCEEDED(g_pd3dDevice->CreateTexture2D(&sd, nullptr, &staging))) {
        g_pd3dDeviceContext->CopyResource(staging, tex);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(g_pd3dDeviceContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
            std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
            for (int y = 0; y < h; ++y) {
                const unsigned char* src = static_cast<const unsigned char*>(mapped.pData) +
                                           static_cast<size_t>(y) * mapped.RowPitch;
                unsigned char* dst = rgb.data() + static_cast<size_t>(y) * w * 3;
                for (int x = 0; x < w; ++x) {
                    dst[x * 3 + 0] = src[x * 4 + 0];
                    dst[x * 3 + 1] = src[x * 4 + 1];
                    dst[x * 3 + 2] = src[x * 4 + 2];
                }
            }
            ok = stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3) != 0;
            g_pd3dDeviceContext->Unmap(staging, 0);
        }
        staging->Release();
    }
    rtv->Release();
    tex->Release();
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    return ok;
}

static bool SaveBackBufferPng(const std::string& path) {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer)
        return false;

    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(g_pd3dDevice->CreateTexture2D(&desc, nullptr, &staging))) {
        backBuffer->Release();
        return false;
    }
    g_pd3dDeviceContext->CopyResource(staging, backBuffer);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    bool ok = false;
    if (SUCCEEDED(g_pd3dDeviceContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        const int w = static_cast<int>(desc.Width);
        const int h = static_cast<int>(desc.Height);
        std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
        for (int y = 0; y < h; ++y) {
            const unsigned char* src =
                static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
            unsigned char* dst = rgb.data() + static_cast<size_t>(y) * w * 3;
            for (int x = 0; x < w; ++x) {
                // DXGI_FORMAT_R8G8B8A8_UNORM：内存顺序已是 R,G,B,A
                dst[x * 3 + 0] = src[x * 4 + 0];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 2];
            }
        }
        ok = stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3) != 0;
        std::printf("[SHOT] %dx%d -> %s\n", w, h, path.c_str());
        g_pd3dDeviceContext->Unmap(staging, 0);
    }
    staging->Release();
    backBuffer->Release();
    return ok;
}

// ---------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    ParseCommandLine();
    // 必须在建窗之前：开启 Per-Monitor DPI 感知，避免 Windows 位图拉伸导致发糊
    ImGui_ImplWin32_EnableDpiAwareness();
    // 控制台日志（调试用；Release 下无窗口）
    if (AllocConsole()) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"FunctionCraftWindow";
    ::RegisterClassExW(&wc);

    // 先按 96 DPI 建一个窗口拿 DPI，再按 DPI 调整尺寸
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"函数工坊 FunctionCraft", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900, nullptr, nullptr,
                                hInstance, nullptr);
    if (!hwnd) return 1;

    ComputeUiScale(hwnd);

    int winW = 0, winH = 0;
    if (g_forceWidth > 0 && g_forceHeight > 0) {
        // 截图验收：窗口尺寸按参数给定（物理像素）
        RECT rc = {0, 0, g_forceWidth, g_forceHeight};
        ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        winW = rc.right - rc.left;
        winH = rc.bottom - rc.top;
    } else {
        int cw = 0, ch = 0;
        DefaultWindowSize(&cw, &ch);
        RECT rc = {0, 0, cw, ch};
        ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        winW = rc.right - rc.left;
        winH = rc.bottom - rc.top;
    }
    ::SetWindowPos(hwnd, nullptr, 0, 0, winW, winH, SWP_NOMOVE | SWP_NOZORDER);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    const bool shotMode = !g_shotPath.empty();
    ::ShowWindow(hwnd, shotMode ? SW_SHOWDEFAULT : SW_SHOWMAXIMIZED);
    ::UpdateWindow(hwnd);

    if (g_forceWidth > 0 && g_forceHeight > 0) {
        RECT rc = {0, 0, g_forceWidth, g_forceHeight};
        ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        ::SetWindowPos(hwnd, HWND_TOP, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                       SWP_NOZORDER | SWP_FRAMECHANGED);
        ::UpdateWindow(hwnd);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // 不生成 imgui.ini

    LoadChineseFont(io);
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FrameRounding = 4.0f;
    ImGui::GetStyle().WindowRounding = 6.0f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    fcg::ui::App app;
    app.init();

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_shotPath.empty() && g_SwapChainOccluded &&
            g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight,
                                        DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        const bool offscreen = !g_shotPath.empty() && g_forceWidth > 0 && g_forceHeight > 0;
        if (offscreen) io.DisplaySize = ImVec2(static_cast<float>(g_forceWidth),
                                               static_cast<float>(g_forceHeight));
        ImGui::NewFrame();

        fcg::ui::frame(app);

        ImGui::Render();

        const bool offscreenNow = !g_shotPath.empty() && g_forceWidth > 0 && g_forceHeight > 0 &&
                                  g_frameCounter + 1 >= g_shotFrames;
        if (offscreenNow) {
            RenderOffscreenAndSave(g_shotPath, g_forceWidth, g_forceHeight);
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(g_ResizeWidth ? g_ResizeWidth : 1424),
                              static_cast<float>(g_ResizeHeight ? g_ResizeHeight : 861), 0.0f, 1.0f};
            g_pd3dDeviceContext->RSSetViewports(1, &vp);
        } else {
            const float clear_color[4] = {0.08f, 0.09f, 0.11f, 1.00f};
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);

        if (!g_shotPath.empty() && ++g_frameCounter >= g_shotFrames) {
            if (!offscreen) SaveBackBufferPng(g_shotPath);
            std::printf("[SHOT] %s\n", g_shotPath.c_str());
            done = true;
        }
    }

    app.shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, hInstance);
    return 0;
}
