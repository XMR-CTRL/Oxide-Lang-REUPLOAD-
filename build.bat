@echo off
taskkill /F /IM oxide.exe 2>nul
taskkill /F /IM z3.exe 2>nul
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\Users\mosky\Documents\Oxide-lang backup\build"
cmake -G "NMake Makefiles" .. 2>&1
echo --- CMAKE EXIT: %ERRORLEVEL% ---
nmake 2>&1
echo --- BUILD EXIT: %ERRORLEVEL% ---
