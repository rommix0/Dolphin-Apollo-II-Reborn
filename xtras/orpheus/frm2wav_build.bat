@echo off
rem Builds build\frm2wav\frm2wav.exe from xtras\orpheus\frm2wav.c and src\synth (MSVC 2022).
rem Usage: frm2wav IN.frm OUT.wav [nopeak] [nolpf]
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0..\.."
if not exist build\frm2wav mkdir build\frm2wav
cl /nologo /std:c11 /W4 /wd4244 /O2 /D_CRT_SECURE_NO_WARNINGS /Isrc /Fobuild\frm2wav\ /Febuild\frm2wav\frm2wav.exe ^
    xtras\orpheus\frm2wav.c src\synth\*.c
