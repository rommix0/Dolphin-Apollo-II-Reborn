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
set LIBSRC=
for %%d in (src\cpu740 src\machine src\engine src\synth src\audio src\render) do (
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
