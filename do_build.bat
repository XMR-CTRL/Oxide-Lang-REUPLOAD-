@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\Users\mosky\Documents\Oxide-lang backup\build"
nmake > build_output.txt 2>&1
echo BUILD_EXIT=%ERRORLEVEL% >> build_output.txt
