// D3DTexture.h — 把 CPU 像素缓冲包装成 ImGui 可用的纹理
// 复平面着色图（domain coloring）需要每帧/每次重算生成一张 RGBA 图，
// 由 main.cpp（持有 D3D11 设备）实现，UI 层只依赖这个接口。
#pragma once

#include "imgui.h"

namespace fcg {
namespace ui {

// 动态纹理句柄（内部是 ID3D11Texture2D* + ID3D11ShaderResourceView*）
struct DynTexture {
    void* tex = nullptr;  // ID3D11Texture2D*
    void* srv = nullptr;  // ID3D11ShaderResourceView*
    int w = 0;
    int h = 0;
};

// 确保纹理尺寸为 w x h（尺寸变化会重建）
bool TextureEnsure(DynTexture& t, int w, int h);

// 上传 RGBA8 像素（行优先，w*h*4 字节）
bool TextureUpdate(DynTexture& t, const unsigned char* rgba);

// 释放
void TextureDestroy(DynTexture& t);

// 传给 ImGui::Image
inline ImTextureID TextureId(const DynTexture& t) {
    return reinterpret_cast<ImTextureID>(t.srv);
}

}  // namespace ui
}  // namespace fcg
