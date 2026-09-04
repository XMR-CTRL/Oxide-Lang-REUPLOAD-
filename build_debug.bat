@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /std:c++17 /EHsc /Od /Zi /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /Febuild\oxided.exe /Fdbuild\ /FS src\*.cpp /link 2>&1
echo BUILD_RC=%errorlevel%
