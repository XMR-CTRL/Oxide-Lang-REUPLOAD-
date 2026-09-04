@echo on
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
echo VC_RC=%errorlevel%
cd /d C:\Users\mosky\Pictures\Oxide-lang
echo CWD_OK
cl /std:c++17 /EHsc /O2 /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /Febuild\oxide.exe src\*.cpp
echo CL_RC=%errorlevel%
