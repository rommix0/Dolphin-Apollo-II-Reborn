@echo off
rem Rebuilds the headless scratch project build\ghidra740\project from the ROM and ghidra740\annotations,
rem then checks it against the M3 done criteria (M3 spec §5.4, §6). Never touches apollo_ghidra\, so it
rem works while the Ghidra GUI is open. Options:
rem   --format     afterwards rewrite ghidra740\annotations in canonical form (ExportAnnotations write)
rem   --roundtrip  afterwards check that an export reproduces ghidra740\annotations byte for byte
rem   --reset      reuse the project's program instead of importing the ROM again: ResetProject.java returns it to
rem                its just-imported state first; then build, check and the --roundtrip check run as usual
rem Reports: build\ghidra740\build.txt, check.txt, roundtrip.txt; log: build\ghidra740\rebuild.log.
rem Exit code: 0 when check.txt says RESULT PASS (and, with --roundtrip or --reset, the RESULT line in export.txt does
rem too), else 1. With --format, --roundtrip or --reset it is also 1 unless build\ghidra740\export.txt ends in EXPORT DONE.
setlocal
pushd "%~dp0.." || exit /b 1
set "ROM=%APOLLO2_ROM%"
if not defined ROM set "ROM=xtras\UK_V2.01_4.04_ROM.BIN"
set "ROM=%ROM:/=\%"
if not exist "%ROM%" (
    echo rebuild: ROM not found: %ROM%
    popd & exit /b 1
)
set "EXPORT="
set "ROUNDTRIP="
set "IMPORT=-import build\ghidra740\UK_V2.01_4.04_ROM.BIN -overwrite -processor MELPS740:LE:16:default -cspec default -loader BinaryLoader -loader-baseAddr 0x0"
if /i "%~1"=="--format" set "EXPORT=-postScript ExportAnnotations.java write"
if /i "%~1"=="--roundtrip" set "ROUNDTRIP=1"
if /i "%~1"=="--reset" (set "ROUNDTRIP=1" & set "IMPORT=-process UK_V2.01_4.04_ROM.BIN -postScript ResetProject.java")
if defined ROUNDTRIP set "EXPORT=-postScript ExportAnnotations.java check"
call ghidra740\install.bat >nul || (popd & exit /b 1)
call ghidra740\ghidra_env.bat build\ghidra740\project || (popd & exit /b 1)
copy /y "%ROM%" build\ghidra740\UK_V2.01_4.04_ROM.BIN >nul || (popd & exit /b 1)
del /q build\ghidra740\build.txt build\ghidra740\check.txt build\ghidra740\roundtrip.txt build\ghidra740\export.txt 2>nul
call "%GHIDRA_INSTALL_DIR%\support\analyzeHeadless.bat" "%PROJ%" apollo %IMPORT% -noanalysis -postScript BuildProject.java -postScript CheckProject.java %EXPORT% > build\ghidra740\rebuild.log 2>&1
if not exist build\ghidra740\check.txt (
    echo rebuild: no check report, see build\ghidra740\rebuild.log
    popd & exit /b 1
)
findstr /b /c:"PASS " /c:"FAIL " /c:"INFO " /c:"FINDING " /c:"RESULT " build\ghidra740\check.txt
set RC=0
if defined EXPORT findstr /b /c:"EXPORT DONE" build\ghidra740\export.txt >nul 2>nul || (
    echo rebuild: ExportAnnotations did not complete, see build\ghidra740\rebuild.log
    set RC=1
)
findstr /b /c:"RESULT PASS" build\ghidra740\check.txt >nul || set RC=1
if defined ROUNDTRIP (
    findstr /b /c:"RESULT PASS" build\ghidra740\export.txt >nul 2>nul || (
        echo roundtrip:
        type build\ghidra740\export.txt 2>nul
        set RC=1
    )
)
popd & exit /b %RC%
