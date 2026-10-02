@echo off
rem Validates the MELPS740 module against cpu740 (M3 spec §4): random single-instruction vectors from
rem cpu740, run headless through Ghidra's disassembler and p-code emulator. Usage: validate.bat [PER_OPCODE]
setlocal
set "N=64"
if not "%~1"=="" set "N=%~1"
pushd "%~dp0.." || exit /b 1
call .\build.bat || (popd & exit /b 1)
call ghidra740\install.bat >nul || (popd & exit /b 1)
call ghidra740\ghidra_env.bat build\ghidra740 || (popd & exit /b 1)
del /q build\ghidra740\validate.txt 2>nul
build\cpu740_vectors.exe -o build\ghidra740\vectors.txt -n %N% --blank build\ghidra740\blank.bin || (popd & exit /b 1)
call "%GHIDRA_INSTALL_DIR%\support\analyzeHeadless.bat" "%PROJ%" validate -import build\ghidra740\blank.bin -overwrite -deleteProject -processor MELPS740:LE:16:default -cspec default -loader BinaryLoader -loader-baseAddr 0x0 -noanalysis -postScript ValidateModule.java > build\ghidra740\validate.log 2>&1
if not exist build\ghidra740\validate.txt (
    echo validate: no report, see build\ghidra740\validate.log
    popd & exit /b 1
)
findstr /b /c:"executed " /c:"RESULT " build\ghidra740\validate.txt
findstr /b /c:"RESULT PASS" build\ghidra740\validate.txt >nul
set RC=%errorlevel%
popd & exit /b %RC%
