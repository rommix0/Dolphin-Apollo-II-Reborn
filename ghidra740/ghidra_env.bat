@echo off
rem Shared Ghidra environment for the ghidra740 batch files. Usage: call ghidra740\ghidra_env.bat [PROJECT_DIR]
rem No setlocal here: it sets GHIDRA_INSTALL_DIR and PROJ in the caller's scope.
rem GHIDRA_INSTALL_DIR unset, or stale (a Ghidra that has since moved), means the standard location.
if not exist "%GHIDRA_INSTALL_DIR%\support\analyzeHeadless.bat" set "GHIDRA_INSTALL_DIR=C:\ghidra_12.1.3_PUBLIC"
if "%~1"=="" exit /b 0
if not exist "%~1" mkdir "%~1" || exit /b 1
rem Ghidra rejects a project path with a directory starting with "." (e.g. a .worktrees checkout); its 8.3 form has none.
for %%I in ("%~f1") do set "PROJ=%%~sI"
exit /b 0
