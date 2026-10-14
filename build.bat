@echo off
rem Builds the program build\apollo2.exe from src\ alone, the oracle tools build\apollo2_frames.exe,
rem build\apollo2_coverage.exe and build\cpu740_vectors.exe, and the tests build\apollo2_tests.exe (M5b spec 2.3).
rem Usage: build.bat [test [SUITE_FILTER]]
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
rem M5b spec 2.3, step 1: the program, compiled and linked from src\ alone with /Isrc as its only include root and its
rem objects in build\product: an include of an oracle header (tests\oracle) cannot be found, and a call into oracle
rem code cannot link.
set PRODSRC=
for %%d in (src\board src\engine src\firmware src\firmware\data src\synth src\audio src\render) do (
    if exist %%d\*.c set PRODSRC=!PRODSRC! %%d\*.c
)
if not exist build\product mkdir build\product
cl %CFLAGS% /Isrc /Fobuild\product\ /Fdbuild\product\ /Febuild\apollo2.exe src\apps\apollo2.c !PRODSRC! %LIBS% || exit /b 1
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
if /i not "%~1"=="test" exit /b 0
build\apollo2_tests.exe %2
exit /b %errorlevel%
