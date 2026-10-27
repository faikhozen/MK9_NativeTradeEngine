@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 exit /b 1
if not exist tests\artifacts mkdir tests\artifacts
if not exist tests\scripts mkdir tests\scripts
if not exist tests\plugins mkdir tests\plugins
cl /nologo /LD /EHsc /O2 /Fe"tests\scripts\asi_dependency.dll" /Fo"tests\asi_dependency.obj" tests\asi_dependency.cpp /link /IMPLIB:"tests\asi_dependency.lib"
if errorlevel 1 exit /b 1
cl /nologo /LD /EHsc /O2 /Fe"tests\scripts\fixture.asi" /Fo"tests\asi_fixture.obj" tests\asi_fixture.cpp tests\asi_dependency.lib /link /IMPLIB:"tests\asi_fixture.lib"
if errorlevel 1 exit /b 1
echo This is an intentionally invalid ASI test fixture> tests\plugins\invalid.asi
cl /nologo /EHsc /std:c++17 /O2 /I"src" /Fe"tests\asi_host.exe" /Fo"tests\\" tests\asi_host.cpp src\asi_loader.cpp
if errorlevel 1 exit /b 1
tests\asi_host.exe
exit /b %errorlevel%

