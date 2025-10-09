@echo off
setlocal enabledelayedexpansion

:: =========================
:: Generic dev reset script
:: - Works in any Git repo
:: - Confirms with user
:: - Detects main vs master
:: =========================

:: Verify Git is available
git --version >nul 2>&1 || (
  echo [ERROR] Git is not installed or not in PATH.
  pause
  exit /b 1
)

:: Verify we're inside a Git work tree
git rev-parse --is-inside-work-tree >nul 2>&1 || (
  echo [ERROR] This folder is not a Git repository.
  pause
  exit /b 1
)

:: Remote to use (default: origin)
set "REMOTE=origin"

:: Detect base branch on remote: main or master
set "BASE=main"
git rev-parse --verify %REMOTE%/main >nul 2>&1 || set "BASE=master"
git rev-parse --verify %REMOTE%/%BASE% >nul 2>&1 || (
  echo [ERROR] Could not find %REMOTE%/main or %REMOTE%/master on this repo.
  pause
  exit /b 1
)

echo.
echo [INFO] Repository: %cd%
echo [INFO] Will reset local 'dev' branch to '%REMOTE%/%BASE%'.
echo [INFO] This will permanently delete ALL untracked and ignored files/directories.
echo.
set /p CONFIRM="Type YES to continue or anything else to cancel: "
if /I not "%CONFIRM%"=="YES" (
  echo.
  echo [CANCELLED] Operation aborted by user. No changes made.
  echo.
  pause
  exit /b 0
)

echo.
echo [STEP] Fetching latest refs from %REMOTE%...
git fetch %REMOTE%

echo [STEP] Switching to 'dev' (create from %REMOTE%/%BASE% if needed)...
git checkout dev 2>nul || git checkout -b dev %REMOTE%/%BASE%

echo [STEP] Hard-resetting 'dev' to '%REMOTE%/%BASE%'...
git reset --hard %REMOTE%/%BASE%

echo [STEP] Cleaning untracked files and directories (including ignored)...
git clean -xfd

echo [STEP] Force-pushing 'dev' to remote '%REMOTE%'...
git push %REMOTE% dev --force

echo.
echo [DONE] 'dev' is now in sync with '%REMOTE%/%BASE%' locally and on the remote.
echo.

echo [SUMMARY - last 5 commits]
git log --oneline -5 --graph --decorate

echo.
pause
endlocal
