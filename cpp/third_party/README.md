# 第三方依赖

## Dear ImGui

- 版本：v1.91.5
- 上游：https://github.com/ocornut/imgui
- 许可：MIT（见 `imgui/LICENSE.txt`）
- 只保留了构建实际需要的文件（核心 + Win32/DX11 后端），省略了 examples / docs / demo / 其它平台后端。

## stb_image_write

- 上游：https://github.com/nothings/stb
- 许可：public domain / MIT
- 仅用于调试截图（`--shot` 参数）。
