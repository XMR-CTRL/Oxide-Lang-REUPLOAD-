@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Program Files\LLVM\bin;%PATH%
cd /d C:\Users\mosky\Pictures\Oxide-lang
del /q sweep_out.txt >nul 2>&1
setlocal enabledelayedexpansion
for %%F in (examples\hello.ox examples\fib.ox examples\structs.ox examples\methods.ox examples\loops.ox examples\arrays.ox examples\enums.ox examples\sizeof.ox examples\inc_ternary.ox examples\lambdas.ox examples\operator_overload.ox examples\refs.ox examples\foreach.ox examples\globals.ox examples\strings.ox examples\strings2.ox examples\strings3.ox examples\casts_widths.ox examples\f32.ox examples\private.ox examples\vec_structs.ox examples\map.ox examples\map_struct.ox examples\set.ox examples\generic_methods.ox examples\stdlib.ox examples\stdlib2.ox examples\lambdas2.ox examples\features.ox examples\raii.ox examples\inheritance.ox examples\defer.ox examples\range.ox examples\default_args.ox examples\map_iter.ox examples\inheritance_virtual.ox) do (
  build\oxide.exe exe %%F >nul 2>"err_%%~nF.txt"
  if errorlevel 1 (echo FAIL_BUILD %%F>>sweep_out.txt & type "err_%%~nF.txt">>sweep_out.txt) else (
    "examples\%%~nF.exe" >"out_%%~nF.txt" 2>&1
    if errorlevel 1 (echo FAIL_RUN %%F rc=!errorlevel!>>sweep_out.txt & echo ----- %%~nF ----- >>sweep_out.txt & type "out_%%~nF.txt">>sweep_out.txt) else (echo OK %%F>>sweep_out.txt)
  )
)
echo SWEEP_DONE>>sweep_out.txt
