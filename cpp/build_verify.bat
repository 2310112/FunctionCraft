@echo off
setlocal
set "PATH=D:\STM32CubeCLT_1.22.0\CMake\bin;D:\STM32CubeCLT_1.22.0\Ninja\bin;%PATH%"
call "D:\VS\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d "%~dp0"
if not exist "build_verify\build.ninja" cmake -S . -B build_verify -G Ninja -DCMAKE_BUILD_TYPE=Release >nul
cmake --build build_verify --target fcg_tests fcg_cli
