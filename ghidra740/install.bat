@echo off
rem Installs the MELPS740 module and the ghidra740 scripts into the user's Ghidra extension directory and
rem compiles the SLEIGH language (M3 spec §1, revised 2026-10-02). Never touches C:\ghidra_12.1.3_PUBLIC.
rem A running Ghidra GUI picks up SLEIGH changes only after a restart; script changes take effect on the
rem next run. Writes repo.txt (this checkout's root) so the scripts can find the annotations.
setlocal
rem Unset, or stale (a Ghidra that has since moved), means the standard location.
if not exist "%GHIDRA_INSTALL_DIR%\support\sleigh.bat" set "GHIDRA_INSTALL_DIR=C:\ghidra_12.1.3_PUBLIC"
set "EXT=%APPDATA%\ghidra\ghidra_12.1.3_PUBLIC\Extensions\MELPS740"
pushd "%~dp0.." || exit /b 1
robocopy "ghidra740\module\MELPS740" "%EXT%" /MIR /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo install: copying the module to %EXT% failed
    popd & exit /b 1
)
> "%EXT%\repo.txt" echo %CD%
call "%GHIDRA_INSTALL_DIR%\support\sleigh.bat" "%EXT%\data\languages\m740.slaspec" "%EXT%\data\languages\m740.sla"
if errorlevel 1 (
    echo install: SLEIGH compile failed
    popd & exit /b 1
)
if not exist "%EXT%\data\languages\m740.sla" (
    echo install: SLEIGH compile produced no m740.sla
    popd & exit /b 1
)
echo installed MELPS740 into %EXT%
popd
exit /b 0
