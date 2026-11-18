@echo off
setlocal
set "PATH=D:\STM32CubeCLT_1.22.0\CMake\bin;D:\STM32CubeCLT_1.22.0\Ninja\bin;%PATH%"
call "D:\VS\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
  echo [ERROR] vcvarsall failed
  exit /b 1
)
cd /d "%~dp0"
if not exist "build\build.ninja" (
  echo [CONFIG] cmake configure...
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  if errorlevel 1 exit /b 1
)
echo [BUILD] compiling...
cmake --build build
if errorlevel 1 exit /b 1
echo [OK] build\bin\FunctionCraft.exe ; build\fcg_tests.exe
exit /b 0
