@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /O2 /W3 /I"src" /Fe"tests\regression.exe" /Fo"tests\\" tests\regression.cpp src\profiler.cpp
if errorlevel 1 exit /b 1
tests\regression.exe %*
exit /b %errorlevel%
