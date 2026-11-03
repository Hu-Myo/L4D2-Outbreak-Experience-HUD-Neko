@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
  echo Open an x86 Native Tools Command Prompt for Visual Studio 2022, then run build.cmd.
  exit /b 1
)
if not exist build mkdir build
if not exist dist\bin\neko\plugins mkdir dist\bin\neko\plugins
cl /nologo /std:c++17 /EHsc /W4 /permissive- /MT /O2 /DWIN32 /DNDEBUG /DNOMINMAX /Isrc /LD /Fo"build\\" src\plugin.cpp src\runtime.cpp src\layout.cpp src\renderer.cpp /link /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /DEF:NekoExtraHUD.def /IMPLIB:build\NekoExtraHUD.lib /MAP:build\NekoExtraHUD.map /OUT:dist\bin\neko\plugins\NekoExtraHUD.dll bcrypt.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /permissive- /MT /O2 /Isrc /Fo"build\\" tests\layout_tests.cpp src\layout.cpp /Fe:build\layout_tests.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
build\layout_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /permissive- /MT /O2 /Isrc /Fo"build\\" tests\paint_context_tests.cpp /Fe:build\paint_context_tests.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
build\paint_context_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /permissive- /MT /O2 /Isrc /Fo"build\\" tests\runtime_adapter_tests.cpp /Fe:build\runtime_adapter_tests.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
build\runtime_adapter_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /permissive- /MT /O2 /Isrc /Fo"build\\" tests\renderer_abi_tests.cpp src\renderer.cpp /Fe:build\renderer_abi_tests.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
build\renderer_abi_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /permissive- /MT /O2 /Isrc /Fo"build\\" tests\plugin_load_tests.cpp /Fe:build\plugin_load_tests.exe /link /MACHINE:X86
if errorlevel 1 exit /b 1
build\plugin_load_tests.exe "%cd%\dist\bin\neko\plugins\NekoExtraHUD.dll"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /permissive- /MT /O2 /Isrc /Fo"build\\" tests\plugin_hotpath_tests.cpp /Fe:build\plugin_hotpath_tests.exe /link /MACHINE:X86 bcrypt.lib user32.lib
if errorlevel 1 exit /b 1
build\plugin_hotpath_tests.exe
if errorlevel 1 exit /b 1
if not exist dist\left4dead2\neko mkdir dist\left4dead2\neko
copy /y config\extra_hud.ini dist\left4dead2\neko\extra_hud.ini >nul
echo Build complete. Install the contents of dist into the game directory.
endlocal
