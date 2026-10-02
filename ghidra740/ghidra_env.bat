@echo off
rem Shared Ghidra environment for the ghidra740 batch files. Usage: call ghidra740\ghidra_env.bat [PROJECT_DIR]
rem No setlocal here: it sets GHIDRA_INSTALL_DIR, XDG_CONFIG_HOME, GHIDRA740_EXT and PROJ in the caller's scope.
rem GHIDRA_INSTALL_DIR unset, or stale (a Ghidra that has since moved), means the standard location.
if not exist "%GHIDRA_INSTALL_DIR%\support\analyzeHeadless.bat" set "GHIDRA_INSTALL_DIR=C:\ghidra_12.1.3_PUBLIC"
rem Headless Ghidra keeps its settings (OSGi bundle cache, application.log, preferences, Extensions) in
rem build\ghidra740\settings, never in the GUI's %APPDATA%\ghidra: each start recreates the settings
rem directory's osgi\felixcache, which breaks script loading in a running GUI. launch.properties rule 2
rem (XDG_CONFIG_HOME), not rule 1 (-Dapplication.settingsdir): sleigh.bat and LaunchSupport take no extra VM
rem arguments. 8.3 form, as for PROJ below. GHIDRA740_EXT is where install.bat puts the headless module copy.
for %%I in ("%~dp0..\build\ghidra740\settings") do set "XDG_CONFIG_HOME=%%~fI"
if not exist "%XDG_CONFIG_HOME%" mkdir "%XDG_CONFIG_HOME%" || exit /b 1
for %%I in ("%XDG_CONFIG_HOME%") do set "XDG_CONFIG_HOME=%%~sI"
set "GHIDRA740_EXT=%XDG_CONFIG_HOME%\ghidra\ghidra_12.1.3_PUBLIC\Extensions\MELPS740"
if "%~1"=="" exit /b 0
if not exist "%~1" mkdir "%~1" || exit /b 1
rem Ghidra rejects a project path with a directory starting with "." (e.g. a .worktrees checkout); its 8.3 form has none.
for %%I in ("%~f1") do set "PROJ=%%~sI"
exit /b 0
