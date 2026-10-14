@echo off
rem Builds build\apollo2_tests.exe, plus build\apollo2_frames.exe and build\apollo2.exe once their sources exist.
rem Usage: build.bat [test [SUITE_FILTER]]
setlocal EnableDelayedExpansion
if not defined VSCMD_VER (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
if not exist build mkdir build
set CFLAGS=/nologo /std:c11 /W4 /WX /wd4244 /O2 /Zi /Isrc /D_CRT_SECURE_NO_WARNINGS
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
rem M5a spec 3.1: src\board and the native files build without the emulator too. Syntax-check them against an include
rem root holding only the headers they may use (src\board, src\firmware and the native headers of src\engine, copied
rem to build\noemu), so an include of a machine\ or cpu740\ header cannot be found.
if exist build\noemu rmdir /s /q build\noemu
for %%d in (board firmware\data engine) do mkdir build\noemu\%%d
for %%d in (board firmware firmware\data) do copy /y src\%%d\*.h build\noemu\%%d >nul
for %%f in (frame_list native_host native_engine) do copy /y src\engine\%%f.h build\noemu\engine >nul
cl /nologo /std:c11 /W4 /WX /wd4244 /Zs /D_CRT_SECURE_NO_WARNINGS /Ibuild\noemu src\board\*.c src\engine\frame_list.c ^
    src\engine\native_host.c src\engine\native_engine.c || exit /b 1
set LIBSRC=
for %%d in (src\cpu740 src\machine src\board src\engine src\firmware src\firmware\data src\synth src\audio src\render) do (
    if exist %%d\*.c set LIBSRC=!LIBSRC! %%d\*.c
)
cl %CFLAGS% /Fobuild\ /Fdbuild\ /Febuild\apollo2_tests.exe tests\*.c !LIBSRC! %LIBS% || exit /b 1
for %%a in (apollo2_frames apollo2 apollo2_coverage cpu740_vectors) do (
    if exist src\apps\%%a.c (
        cl %CFLAGS% /Fobuild\ /Fdbuild\ /Febuild\%%a.exe src\apps\%%a.c !LIBSRC! %LIBS% || exit /b 1
    )
)
if /i not "%~1"=="test" exit /b 0
build\apollo2_tests.exe %2
exit /b %errorlevel%
