@echo off
rem Writes ghidra740\coverage\corpus.cov: the instructions the firmware executes over boot and the golden
rem corpus, plus the indirect JMP/JSR targets (M3 spec §5.1). Needs the ROM (APOLLO2_ROM or xtras\UK_V2.01_4.04_ROM.BIN).
setlocal
pushd "%~dp0.." || exit /b 1
call .\build.bat || (popd & exit /b 1)
if not exist ghidra740\coverage mkdir ghidra740\coverage
rem Our own texts after the golden corpus: extra_index.txt reaches the immediate @I+/@I? commands (JMP ($4A) at
rem $C46A); extra_commands.txt reaches the @E, @L and no-op command handlers the golden corpus never sends.
build\apollo2_coverage.exe -o ghidra740\coverage\corpus.cov %* ghidra740\coverage\extra_index.txt ghidra740\coverage\extra_commands.txt
set RC=%errorlevel%
popd & exit /b %RC%
