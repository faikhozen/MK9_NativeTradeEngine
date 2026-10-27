@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "BUILD_DIR=obj\build-%RANDOM%-%RANDOM%"
if exist "%BUILD_DIR%" exit /b 1
mkdir "%BUILD_DIR%"
if errorlevel 1 exit /b 1

echo ======================================================================
echo  Building MK9 Trade Priority Fix Mod (dinput8.dll x86)
echo ======================================================================

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 (
    echo [!] Failed to initialize MSVC x86 environment.
    exit /b 1
)

if not exist bin mkdir bin
if not exist obj mkdir obj
rem Fresh isolated validation directory

set CFLAGS=/nologo /O2 /Oi /Ot /Oy /GL /MD /W3 /EHsc /std:c++17 /MP /I"src" /I"src\minhook\include" /I"src\minhook\src" /I"src\imgui" /Fo"%BUILD_DIR%\\"
set LDFLAGS=/link /nologo /DLL /LTCG /OPT:REF /OPT:ICF /MAP:"%BUILD_DIR%\dinput8.map" /DEF:src\dinput8.def /OUT:"%BUILD_DIR%\dinput8.dll" user32.lib kernel32.lib d3d9.lib gdi32.lib winmm.lib

echo [*] Compiling MinHook (x86)...
cl %CFLAGS% /c src\minhook\src\buffer.c src\minhook\src\hook.c src\minhook\src\trampoline.c src\minhook\src\hde\hde32.c
if errorlevel 1 exit /b 1

echo [*] Compiling Dear ImGui (x86)...
cl %CFLAGS% /c src\imgui\imgui.cpp src\imgui\imgui_draw.cpp src\imgui\imgui_tables.cpp src\imgui\imgui_widgets.cpp src\imgui\imgui_impl_dx9.cpp src\imgui\imgui_impl_win32.cpp
if errorlevel 1 exit /b 1

echo [*] Compiling Mod Source Files (x86)...
cl %CFLAGS% /c src\engine_common.cpp src\engine_stability.cpp src\display_window.cpp src\frame_data.cpp src\cancel_detector.cpp src\collision_visualizer.cpp src\combat_state.cpp src\trade_engine.cpp src\meter_drain_fix.cpp src\negative_edge.cpp src\crouch_buffer_fix.cpp src\jump_input_fix.cpp src\move_priority.cpp src\profiler.cpp src\toasty_training.cpp src\input_recording.cpp src\input_isolation.cpp src\reversal_engine.cpp src\simulation.cpp src\trade_fix.cpp src\free_camera.cpp src\anim_viewer.cpp src\extra_gameplay.cpp src\stun_modifier.cpp src\juggle_deterioration.cpp src\attack_frame_modifier.cpp src\imgui_overlay.cpp src\load_profiler.cpp src\asi_loader.cpp src\dinput8_proxy.cpp src\dllmain.cpp
if errorlevel 1 exit /b 1

echo [*] Linking bin\dinput8.dll...
cl /nologo "%BUILD_DIR%\buffer.obj" "%BUILD_DIR%\hook.obj" "%BUILD_DIR%\trampoline.obj" "%BUILD_DIR%\hde32.obj" "%BUILD_DIR%\imgui.obj" "%BUILD_DIR%\imgui_draw.obj" "%BUILD_DIR%\imgui_tables.obj" "%BUILD_DIR%\imgui_widgets.obj" "%BUILD_DIR%\imgui_impl_dx9.obj" "%BUILD_DIR%\imgui_impl_win32.obj" "%BUILD_DIR%\engine_common.obj" "%BUILD_DIR%\engine_stability.obj" "%BUILD_DIR%\display_window.obj" "%BUILD_DIR%\frame_data.obj" "%BUILD_DIR%\cancel_detector.obj" "%BUILD_DIR%\collision_visualizer.obj" "%BUILD_DIR%\combat_state.obj" "%BUILD_DIR%\trade_engine.obj" "%BUILD_DIR%\meter_drain_fix.obj" "%BUILD_DIR%\negative_edge.obj" "%BUILD_DIR%\crouch_buffer_fix.obj" "%BUILD_DIR%\jump_input_fix.obj" "%BUILD_DIR%\move_priority.obj" "%BUILD_DIR%\profiler.obj" "%BUILD_DIR%\toasty_training.obj" "%BUILD_DIR%\input_recording.obj" "%BUILD_DIR%\input_isolation.obj" "%BUILD_DIR%\reversal_engine.obj" "%BUILD_DIR%\simulation.obj" "%BUILD_DIR%\trade_fix.obj" "%BUILD_DIR%\free_camera.obj" "%BUILD_DIR%\anim_viewer.obj" "%BUILD_DIR%\extra_gameplay.obj" "%BUILD_DIR%\stun_modifier.obj" "%BUILD_DIR%\juggle_deterioration.obj" "%BUILD_DIR%\attack_frame_modifier.obj" "%BUILD_DIR%\imgui_overlay.obj" "%BUILD_DIR%\load_profiler.obj" "%BUILD_DIR%\asi_loader.obj" "%BUILD_DIR%\dinput8_proxy.obj" "%BUILD_DIR%\dllmain.obj" %LDFLAGS%
if errorlevel 1 exit /b 1



if not exist "%BUILD_DIR%\dinput8.dll" exit /b 1
copy /y "%BUILD_DIR%\dinput8.dll" "bin\dinput8.dll" >nul
if errorlevel 1 exit /b 1
copy /y "%BUILD_DIR%\dinput8.map" "bin\dinput8.map" >nul
if errorlevel 1 exit /b 1
"%SystemRoot%\System32\certutil.exe" -hashfile "bin\dinput8.dll" SHA256 > "bin\dinput8.sha256.txt"
if errorlevel 1 exit /b 1
echo [+] Build succeeded: bin\dinput8.dll. Deployment is a separate step.
exit /b 0

