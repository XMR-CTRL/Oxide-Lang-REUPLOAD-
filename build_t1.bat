@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
echo VC_RC=%errorlevel%
cd /d "C:\Users\mosky\Documents\Oxide-lang backup"
cl /std:c++17 /EHsc /O2 /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /Febuild\oxide.exe src\*.cpp > buildlog_t1.txt 2>&1
echo CL_RC=%errorlevel%
