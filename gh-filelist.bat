@echo off
REM ------------------------------------------------------------
REM gh-filelist.bat
REM Overwrites (or creates) filelist.txt with a directory tree
REM and a flat path list. Skips .git, .venv, venv, __pycache__,
REM editor/cache folders — so a workbench venv cannot bloat git.
REM ------------------------------------------------------------

setlocal EnableExtensions

if "%~1"=="" (
  set "TARGET=%CD%"
) else (
  set "TARGET=%~1"
)

if "%~2"=="" (
  set "OUT=filelist.txt"
) else (
  set "OUT=%~2"
)

if not exist "%TARGET%\." (
  echo [ERROR] Folder not found: "%TARGET%"
  exit /b 1
)

set "PS1=%~dp0scripts\write-filelist.ps1"
if not exist "%PS1%" (
  echo [ERROR] Missing helper: "%PS1%"
  exit /b 1
)

echo [INFO] Target: %TARGET%
echo [STEP] Writing tree + flat list (skipping .git / .venv / caches^)...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -Target "%TARGET%" -Out "%OUT%"
exit /b %ERRORLEVEL%
