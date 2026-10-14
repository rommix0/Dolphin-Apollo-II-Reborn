@echo off
rem Checks the program itself (the M5b plan's Review Focus 1, 2 and 4): the M5a options --engine, --rom and --rom-any
rem are usage errors that write nothing and leave an existing file as it was; the program needs no ROM, run from a
rem folder with no ROM file and APOLLO2_ROM unset, naming a missing file or naming a file that is not the ROM, and
rem writes there the WAV it writes in the repo root; 1501 characters are refused. Every run writes a WAV with -o:
rem nothing is ever played. Usage, from the repo root: tests\check_cli.bat EXE
setlocal EnableExtensions DisableDelayedExpansion
if "%~1"=="" (
    echo usage: tests\check_cli.bat EXE
    exit /b 1
)
set "EXE=%~f1"
set FAILS=0
set "WORK=%TEMP%\apollo2_check_cli_%RANDOM%"
if exist "%WORK%" rmdir /s /q "%WORK%"
mkdir "%WORK%" || exit /b 1

rem The reference: "Hello." rendered in the repo root, with APOLLO2_ROM as the caller has it.
"%EXE%" -o "%WORK%\root.wav" "Hello." 2>"%WORK%\err.txt"
call :expect_rc %errorlevel% 0 "Hello. in the repo root"
call :expect_wav "%WORK%\root.wav" "Hello. in the repo root"

pushd "%WORK%"
echo keep>keep.wav

rem Review Focus 1: the M5a options are unknown options: the usage text, exit code 1, no WAV written.
"%EXE%" --engine rom -o engine_rom.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 1 "--engine rom"
call :expect_usage "--engine rom"
call :expect_absent engine_rom.wav "--engine rom"
"%EXE%" --engine hybrid -o engine_hybrid.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 1 "--engine hybrid"
call :expect_usage "--engine hybrid"
call :expect_absent engine_hybrid.wav "--engine hybrid"
"%EXE%" --rom UK_V2.01_4.04_ROM.BIN -o rom.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 1 "--rom PATH"
call :expect_usage "--rom PATH"
call :expect_absent rom.wav "--rom PATH"
"%EXE%" --rom-any -o rom_any.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 1 "--rom-any"
call :expect_usage "--rom-any"
call :expect_absent rom_any.wav "--rom-any"
"%EXE%" --engine rom -o keep.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 1 "--engine rom onto an existing file"
call :expect_kept "--engine rom onto an existing file"

rem Review Focus 2: no ROM file here; APOLLO2_ROM unset, naming a missing file, naming a file that is not the ROM.
set APOLLO2_ROM=
"%EXE%" -o unset.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 0 "APOLLO2_ROM unset"
call :expect_same unset.wav "APOLLO2_ROM unset"
set "APOLLO2_ROM=%WORK%\missing.bin"
"%EXE%" -o missing.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 0 "APOLLO2_ROM missing"
call :expect_same missing.wav "APOLLO2_ROM missing"
echo not a ROM>notrom.bin
set "APOLLO2_ROM=%WORK%\notrom.bin"
"%EXE%" -o notrom.wav "Hello." 2>err.txt
call :expect_rc %errorlevel% 0 "APOLLO2_ROM not the ROM"
call :expect_same notrom.wav "APOLLO2_ROM not the ROM"
set APOLLO2_ROM=

rem Review Focus 4: 1501 characters are refused (exit 1), and an existing file is left as it was.
set "LONG="
for /l %%i in (1,1,34) do call set "LONG=%%LONG%%the quick brown fox jumps over the lazy dog "
>long1501.txt echo %LONG%dogs.
"%EXE%" --text-file long1501.txt -o keep.wav 2>err.txt
call :expect_rc %errorlevel% 1 "1501 characters"
findstr /b /l /c:"text too long: 1501 characters (at most 1500)" err.txt >nul || call :fail "1501 characters: no 'text too long' line"
call :expect_kept "1501 characters"

popd
rmdir /s /q "%WORK%"
if %FAILS% neq 0 (
    echo check_cli: %FAILS% failure^(s^)
    exit /b 1
)
echo check_cli: ok
exit /b 0

:fail
echo check_cli: %~1
set /a FAILS+=1
exit /b 0

:expect_rc
if "%~1"=="%~2" exit /b 0
call :fail "%~3: exit code %~1, want %~2"
exit /b 0

:expect_usage
findstr /b /l /c:"usage: apollo2 [options] TEXT" err.txt >nul || call :fail "%~1: no usage text"
findstr /b /l /c:"exit codes: 0 ok, 1 usage/file error, 2 firmware fault, 3 cycle limit, 4 audio error, 5 unmodelled access logged" err.txt >nul || call :fail "%~1: no exit-code line"
exit /b 0

:expect_absent
if exist "%~1" call :fail "%~2: %~1 was written"
exit /b 0

:expect_kept
for %%f in (keep.wav) do if %%~zf neq 6 (
    call :fail "%~1: keep.wav was changed"
    exit /b 0
)
findstr /b /l /x /c:"keep" keep.wav >nul || call :fail "%~1: keep.wav was changed"
exit /b 0

:expect_wav
if not exist "%~1" (
    call :fail "%~2: no WAV written"
    exit /b 0
)
for %%f in ("%~1") do if %%~zf leq 44 call :fail "%~2: the WAV holds no sample"
exit /b 0

:expect_same
call :expect_wav "%~1" "%~2"
if not exist "%~1" exit /b 0
fc /b "%~1" root.wav >nul || call :fail "%~2: the WAV differs from the repo root's"
exit /b 0
