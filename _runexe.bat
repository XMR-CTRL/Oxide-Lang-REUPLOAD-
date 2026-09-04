@echo off
set PATH=C:\Program Files\LLVM\bin;%PATH%
"C:\Users\mosky\Pictures\Oxide-lang\%1.exe" > "C:\Users\mosky\Pictures\Oxide-lang\runout2.txt" 2>&1
echo EXE_RC=%errorlevel% >> "C:\Users\mosky\Pictures\Oxide-lang\runout2.txt"
