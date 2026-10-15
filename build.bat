@echo off
rem Builds the program build\apollo2.exe from src\ alone, the oracle tools build\apollo2_frames.exe,
rem build\apollo2_coverage.exe and build\cpu740_vectors.exe, and the tests build\apollo2_tests.exe (M5b spec 2.3).
rem Usage: build.bat [test [SUITE_FILTER] | test-norom [SUITE_FILTER] | dist]
rem dist builds the program alone and packs it into dist\apollo2-COMMIT-win64.zip; it builds and runs no tests.
setlocal EnableDelayedExpansion
if not defined VSCMD_VER (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
if not exist build mkdir build
set CFLAGS=/nologo /std:c11 /W4 /WX /wd4244 /O2 /Zi /D_CRT_SECURE_NO_WARNINGS
set LIBS=winmm.lib
rem M4.0 spec 3.1: src\firmware builds without the emulator. Syntax-check it without /Isrc, so an include of
rem a machine\ or cpu740\ header cannot be found.
set FWSRC=
for %%d in (src\firmware src\firmware\data) do (
    if exist %%d\*.c set FWSRC=!FWSRC! %%d\*.c
)
if defined FWSRC (
    cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS !FWSRC! || exit /b 1
)
rem Two guards that keep the no-ROM proof (M5b spec 4) honest. No file under src\ includes a header by a path through
rem tests\ or oracle\ (a relative "../../tests/oracle/..." include would get past /Isrc). No file under src\ or tests\
rem looks for the ROM itself (its file name, or the APOLLO2_ROM variable read with getenv) except tests\rom_fixture.c,
rem where test_rom() records the request and honours APOLLO2_NO_ROM, and the two oracle tools that read the ROM.
findstr /s /n /r /c:"#include.*tests" /c:"#include.*oracle" src\*.c src\*.h && (
    echo build.bat: a file under src\ includes a header through tests\ or oracle\
    exit /b 1
)
set ROMLOOKUP=
rem findstr /s searches one root at a time, and a backslash in a /c: string is an escape, hence the \\ below.
for %%r in (src tests) do (
    for /f "delims=" %%f in ('findstr /s /m /r /c:"UK_V2\.01_4\.04_ROM\.BIN" /c:"getenv(.APOLLO2_ROM.)" %%r\*.c %%r\*.h ^| findstr /v /i /l /c:"tests\\rom_fixture." /c:"tests\\oracle\\apps\\apollo2_frames.c" /c:"tests\\oracle\\apps\\apollo2_coverage.c"') do (
        echo build.bat: %%f looks for the ROM itself; only tests\rom_fixture.c may
        set ROMLOOKUP=1
    )
)
if defined ROMLOOKUP exit /b 1
rem M5b spec 2.3, step 1: the program, compiled and linked from src\ alone with /Isrc as its only include root and its
rem objects in build\product: an include of an oracle header (tests\oracle) cannot be found, and a call into oracle
rem code cannot link.
set PRODSRC=
for %%d in (src\board src\engine src\firmware src\firmware\data src\synth src\audio src\render) do (
    if exist %%d\*.c set PRODSRC=!PRODSRC! %%d\*.c
)
if not exist build\product mkdir build\product
cl %CFLAGS% /Isrc /Fobuild\product\ /Fdbuild\product\ /Febuild\apollo2.exe src\apps\apollo2.c !PRODSRC! %LIBS% || exit /b 1
if /i "%~1"=="dist" goto dist
rem Step 2: the oracle tools, from tests\oracle\apps, under the names ghidra740\coverage.bat, ghidra740\validate.bat and
rem tools\calibrate.py run them by.
set ORACLESRC=
for %%d in (tests\oracle\cpu740 tests\oracle\machine tests\oracle\engine tests\oracle\render) do (
    if exist %%d\*.c set ORACLESRC=!ORACLESRC! %%d\*.c
)
for %%a in (apollo2_frames apollo2_coverage cpu740_vectors) do (
    cl %CFLAGS% /Isrc /Itests\oracle /Fobuild\ /Fdbuild\ /Febuild\%%a.exe tests\oracle\apps\%%a.c !ORACLESRC! !PRODSRC! ^
        %LIBS% || exit /b 1
)
rem Step 3: the tests, with the oracle and the program's sources.
cl %CFLAGS% /Isrc /Itests\oracle /Fobuild\ /Fdbuild\ /Febuild\apollo2_tests.exe tests\*.c !ORACLESRC! !PRODSRC! %LIBS% ^
    || exit /b 1
if /i "%~1"=="test-norom" goto norom
if /i not "%~1"=="test" exit /b 0
build\apollo2_tests.exe %2
exit /b %errorlevel%

:norom
rem M5b spec 4.4: the suite as if no ROM existed (APOLLO2_NO_ROM: test_rom() returns NULL without looking), then the
rem program itself with APOLLO2_ROM unset (tests\check_cli.bat).
set APOLLO2_NO_ROM=1
build\apollo2_tests.exe %2 || exit /b 1
set APOLLO2_NO_ROM=
set APOLLO2_ROM=
call tests\check_cli.bat build\apollo2.exe || exit /b 1
exit /b 0

:dist
rem The release: apollo2.exe (the static C runtime, so no redistributable is needed) and README.md in
rem dist\apollo2\, zipped with Windows' own tar.
set COMMIT=
for /f %%h in ('git rev-parse --short HEAD') do set COMMIT=%%h
if not defined COMMIT exit /b 1
if exist dist rmdir /s /q dist
mkdir dist\apollo2 || exit /b 1
copy /y build\apollo2.exe dist\apollo2\ >nul || exit /b 1
copy /y README.md dist\apollo2\ >nul || exit /b 1
"%SystemRoot%\System32\tar.exe" -a -c -f dist\apollo2-%COMMIT%-win64.zip -C dist apollo2 || exit /b 1
echo dist\apollo2-%COMMIT%-win64.zip
exit /b 0
