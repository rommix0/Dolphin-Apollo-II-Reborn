@echo off
rem Installs the MELPS740 module and the ghidra740 scripts into the user's Ghidra extension directory and
rem compiles the SLEIGH language (M3 spec §1, revised 2026-10-02). Never touches C:\ghidra_12.1.3_PUBLIC.
rem A running Ghidra GUI picks up SLEIGH changes only after a restart; script changes take effect on the
rem next run. Writes repo.txt (this checkout's root) so the scripts can find the annotations.
rem The installed m740.sla and repo.txt are never purged: the mirror excludes them, the SLEIGH is compiled
rem to build\ghidra740\m740.new.sla, and the installed copy is replaced only when it is missing or differs
rem (copy to a .tmp beside it, then move over it), so an open Ghidra GUI never sees it vanish or half-written.
rem The headless runs have their own settings directory (ghidra_env.bat), so the installed module, .sla and
rem repo.txt included, is then mirrored to its Extensions\MELPS740 (GHIDRA740_EXT) as well.
setlocal
call "%~dp0ghidra_env.bat" || exit /b 1
rem Unset, or stale (a Ghidra that has since moved), means the standard location.
if not exist "%GHIDRA_INSTALL_DIR%\support\sleigh.bat" set "GHIDRA_INSTALL_DIR=C:\ghidra_12.1.3_PUBLIC"
if not exist "%GHIDRA_INSTALL_DIR%\support\sleigh.bat" (
    echo install: no sleigh.bat under %GHIDRA_INSTALL_DIR%\support
    exit /b 1
)
set "EXT=%APPDATA%\ghidra\ghidra_12.1.3_PUBLIC\Extensions\MELPS740"
set "SLA=%EXT%\data\languages\m740.sla"
pushd "%~dp0.." || exit /b 1
set "OUT=%CD%\build\ghidra740"
if not exist "%OUT%" mkdir "%OUT%"
robocopy "ghidra740\module\MELPS740" "%EXT%" /MIR /XF m740.sla m740.sla.tmp repo.txt /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo install: copying the module to %EXT% failed
    popd & exit /b 1
)
rem repo.txt: rewritten only when its content would change.
> "%OUT%\repo.txt.new" echo %CD%
fc /b "%OUT%\repo.txt.new" "%EXT%\repo.txt" >nul 2>&1 || copy /y "%OUT%\repo.txt.new" "%EXT%\repo.txt" >nul
rem Compile beside the installed one, never in place.
del /q "%OUT%\m740.new.sla" 2>nul
call "%GHIDRA_INSTALL_DIR%\support\sleigh.bat" "%EXT%\data\languages\m740.slaspec" "%OUT%\m740.new.sla"
if errorlevel 1 (
    echo install: SLEIGH compile failed
    popd & exit /b 1
)
if not exist "%OUT%\m740.new.sla" (
    echo install: SLEIGH compile produced no m740.sla
    popd & exit /b 1
)
fc /b "%OUT%\m740.new.sla" "%SLA%" >nul 2>&1
if errorlevel 1 (
    copy /y "%OUT%\m740.new.sla" "%SLA%.tmp" >nul || (echo install: could not stage m740.sla & popd & exit /b 1)
    move /y "%SLA%.tmp" "%SLA%" >nul || (echo install: could not replace m740.sla & popd & exit /b 1)
)
robocopy "%EXT%" "%GHIDRA740_EXT%" /MIR /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo install: copying the module to %GHIDRA740_EXT% failed
    popd & exit /b 1
)
echo installed MELPS740 into %EXT% and %GHIDRA740_EXT%
popd
exit /b 0
