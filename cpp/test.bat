@echo off
setlocal
call "%~dp0build.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
echo.
echo ====== UNIT TESTS ======
build\fcg_tests.exe %*
exit /b %errorlevel%
