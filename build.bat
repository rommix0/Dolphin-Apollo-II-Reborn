@echo off
rem Builds build\apollo2_tests.exe (and build\apollo2_frames.exe once it exists).
rem Usage: build.bat [test [SUITE_FILTER]]
setlocal EnableDelayedExpansion
if not defined VSCMD_VER (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
if not exist build mkdir build
set CFLAGS=/nologo /std:c11 /W4 /WX /wd4244 /O2 /Zi /Isrc /D_CRT_SECURE_NO_WARNINGS
set LIBSRC=
for %%d in (src\cpu740 src\machine src\engine) do (
    if exist %%d\*.c set LIBSRC=!LIBSRC! %%d\*.c
)
cl %CFLAGS% /Fobuild\ /Fdbuild\ /Febuild\apollo2_tests.exe tests\*.c !LIBSRC! || exit /b 1
if exist src\apps\apollo2_frames.c (
    cl %CFLAGS% /Fobuild\ /Fdbuild\ /Febuild\apollo2_frames.exe src\apps\apollo2_frames.c !LIBSRC! || exit /b 1
)
if /i not "%~1"=="test" exit /b 0
build\apollo2_tests.exe %2
exit /b %errorlevel%
