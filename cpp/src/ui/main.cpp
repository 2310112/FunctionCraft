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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace fcg { namespace ui { ShotConfig g_shotConfig; } }

// ---------------------------------------------------------------
// DirectX11 全局
// ---------------------------------------------------------------
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

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
// 截图支持（调试/自动验收用）
// ---------------------------------------------------------------
static std::string g_shotPath;
static int g_shotFrames = 10;
static int g_forceWidth = 0;
static int g_forceHeight = 0;
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
    std::string frames = take("--frames=");
    if (!frames.empty()) g_shotFrames = std::max(1, atoi(frames.c_str()));
    fcg::ui::g_shotConfig.scene = take("--scene=");
    fcg::ui::g_shotConfig.levelId = take("--level=");
    fcg::ui::g_shotConfig.expression = take("--expr=");
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

    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"函数工坊 FunctionCraft", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900, nullptr, nullptr,
                                hInstance, nullptr);
    if (!hwnd) return 1;

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    // 截图模式：强制窗口客户区尺寸（宿主屏幕小于目标尺寸时 Win32 会钳窗口）
    if (g_forceWidth > 0 && g_forceHeight > 0) {
        RECT rc = {0, 0, g_forceWidth, g_forceHeight};
        ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        ::SetWindowPos(hwnd, HWND_TOP, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                       SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

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
        ImGui::NewFrame();

        fcg::ui::frame(app);

        ImGui::Render();
        const float clear_color[4] = {0.08f, 0.09f, 0.11f, 1.00f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);

        if (!g_shotPath.empty() && ++g_frameCounter >= g_shotFrames) {
            SaveBackBufferPng(g_shotPath);
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
