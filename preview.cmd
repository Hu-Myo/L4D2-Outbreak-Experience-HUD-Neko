@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
  echo Open an x86 Native Tools Command Prompt for Visual Studio 2022.
  exit /b 1
)
if not exist build mkdir build
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /permissive- /MT /O2 /Isrc /Fo"build\\" tests\render_preview.cpp src\layout.cpp src\renderer.cpp /Fe:build\render_preview.exe /link /MACHINE:X86 user32.lib
if errorlevel 1 exit /b 1
build\render_preview.exe preview.svg
if errorlevel 1 exit /b 1
echo preview.svg generated. Open it in a web browser to view or capture it.
endlocal
