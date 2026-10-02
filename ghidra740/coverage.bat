@echo off
rem Writes ghidra740\coverage\corpus.cov: the instructions the firmware executes over boot and the golden
rem corpus, plus the indirect JMP/JSR targets (M3 spec §5.1). Needs the ROM (APOLLO2_ROM or .\UK_V2.01_4.04_ROM.BIN).
setlocal
pushd "%~dp0.." || exit /b 1
call .\build.bat || (popd & exit /b 1)
if not exist ghidra740\coverage mkdir ghidra740\coverage
build\apollo2_coverage.exe -o ghidra740\coverage\corpus.cov %*
set RC=%errorlevel%
popd & exit /b %RC%
