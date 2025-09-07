@echo off
REM ------------------------------------------------------------
REM gh-devreset.bat
REM Reset local 'dev' branch to match 'origin/main', remove all
REM untracked files/dirs, and force-push 'dev' to the remote.
REM
REM Usage:
REM   gh-devreset.bat [pathToRepo]
REM If no path is provided, the current directory is used.
REM ------------------------------------------------------------

setlocal ENABLEDELAYEDEXPANSION

REM Determine repo path
if "%~1"=="" (
  set "REPO=%CD%"
) else (
  set "REPO=%~1"
)

REM Basic checks
if not exist "%REPO%\." (
  echo [ERROR] Repository path not found: "%REPO%"
  exit /b 1
)

if not exist "%REPO%\.git" (
  echo [ERROR] Not a Git repository: "%REPO%"
  exit /b 1
)

REM Check Git presence
git --version >NUL 2>&1
if errorlevel 1 (
  echo [ERROR] Git is not installed or not on PATH.
  exit /b 1
)

pushd "%REPO%" >NUL 2>&1

echo.
echo [INFO] Repository: %REPO%
echo.

echo [STEP] Fetching latest refs from origin...
git fetch origin --prune
if errorlevel 1 goto :error

echo [STEP] Switching to 'dev' (create/reset from origin/main if needed)...
git switch dev 2>NUL || git switch -C dev origin/main
if errorlevel 1 goto :error

echo [STEP] Hard-resetting 'dev' to 'origin/main'...
git reset --hard origin/main
if errorlevel 1 goto :error

echo [STEP] Cleaning untracked files and directories (including ignored)...
git clean -xfd
if errorlevel 1 goto :error

echo [STEP] Force-pushing 'dev' to remote...
git push origin dev --force
if errorlevel 1 goto :error

echo.
echo [DONE] 'dev' is now in sync with 'origin/main' locally and on GitHub.
echo.

echo [SUMMARY]
git status -sb
echo.
git log --oneline --decorate -n 1
echo.

popd >NUL 2>&1
exit /b 0

:error
echo.
echo [FAILED] An error occurred. See messages above.
popd >NUL 2>&1
exit /b 1
