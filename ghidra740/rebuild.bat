@echo off
rem Rebuilds the headless scratch project build\ghidra740\project from the ROM and ghidra740\annotations
rem (M3 spec §5.4). Never touches apollo_ghidra\, so it works while the Ghidra GUI is open.
setlocal
pushd "%~dp0.." || exit /b 1
set "ROM=%APOLLO2_ROM%"
if not defined ROM set "ROM=UK_V2.01_4.04_ROM.BIN"
set "ROM=%ROM:/=\%"
if not exist "%ROM%" (
    echo rebuild: ROM not found: %ROM%
    popd & exit /b 1
)
call ghidra740\install.bat >nul || (popd & exit /b 1)
call ghidra740\ghidra_env.bat build\ghidra740\project || (popd & exit /b 1)
copy /y "%ROM%" build\ghidra740\UK_V2.01_4.04_ROM.BIN >nul || (popd & exit /b 1)
del /q build\ghidra740\build.txt 2>nul
call "%GHIDRA_INSTALL_DIR%\support\analyzeHeadless.bat" "%PROJ%" apollo -import build\ghidra740\UK_V2.01_4.04_ROM.BIN -overwrite -processor MELPS740:LE:16:default -cspec default -loader BinaryLoader -loader-baseAddr 0x0 -noanalysis -postScript BuildProject.java > build\ghidra740\rebuild.log 2>&1
if not exist build\ghidra740\build.txt (
    echo rebuild: no build report, see build\ghidra740\rebuild.log
    popd & exit /b 1
)
type build\ghidra740\build.txt
findstr /b /c:"ERROR" build\ghidra740\build.txt >nul && (popd & exit /b 1)
popd & exit /b 0
