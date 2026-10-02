@echo off
rem Dumps the scratch project (run ghidra740\rebuild.bat first) to build\ghidra740\dump\ for labelling:
rem functions.txt, data.txt, undefined.txt and one file per function with disassembly and decompiled C.
setlocal
pushd "%~dp0.." || exit /b 1
call ghidra740\install.bat >nul || (popd & exit /b 1)
call ghidra740\ghidra_env.bat build\ghidra740\project || (popd & exit /b 1)
call "%GHIDRA_INSTALL_DIR%\support\analyzeHeadless.bat" "%PROJ%" apollo -process UK_V2.01_4.04_ROM.BIN -noanalysis -readOnly -postScript DumpProject.java > build\ghidra740\dump.log 2>&1
if not exist build\ghidra740\dump\functions.txt (
    echo dump: no output, see build\ghidra740\dump.log
    popd & exit /b 1
)
echo dumped to build\ghidra740\dump
popd & exit /b 0
