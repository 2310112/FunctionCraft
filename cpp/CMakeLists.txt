cmake_minimum_required(VERSION 3.20)
project(FunctionCraft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W3 /EHsc /MP /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
endif()

# ---------------------------------------------------------------
# 核心符号引擎（无外部依赖）
# ---------------------------------------------------------------
add_library(fcg_core STATIC
    src/core/Expr.cpp
    src/core/Numeric.cpp
    src/core/Parser.cpp
    src/core/Json.cpp
    src/core/Simplify.cpp
    src/core/Diff.cpp
    src/core/Domain.cpp
    src/core/Limit.cpp
    src/core/Constraint.cpp
    src/core/Scoring.cpp
    src/core/Level.cpp
    src/core/EndlessGen.cpp
)
target_include_directories(fcg_core PUBLIC src)

# ---------------------------------------------------------------
# 测试
# ---------------------------------------------------------------
file(GLOB FCG_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(fcg_tests ${FCG_TEST_SOURCES})
target_link_libraries(fcg_tests PRIVATE fcg_core)
target_include_directories(fcg_tests PRIVATE tests)
target_compile_definitions(fcg_tests PRIVATE FCG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# ---------------------------------------------------------------
# 命令行工具（关卡自检 / 引擎调试）
# ---------------------------------------------------------------
add_executable(fcg_cli src/tools/cli.cpp)
target_link_libraries(fcg_cli PRIVATE fcg_core)

# ---------------------------------------------------------------
# 游戏本体（Win32 + DirectX11 + Dear ImGui）
# ---------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_link_libraries(imgui PUBLIC d3d11 dxgi)

file(GLOB FCG_UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp)
add_executable(FunctionCraft WIN32 ${FCG_UI_SOURCES})
target_link_libraries(FunctionCraft PRIVATE fcg_core imgui)
target_include_directories(FunctionCraft PRIVATE src src/ui)
set_target_properties(FunctionCraft PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
# 关卡数据随可执行文件发布
add_custom_command(TARGET FunctionCraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/data ${CMAKE_BINARY_DIR}/bin/data
)
