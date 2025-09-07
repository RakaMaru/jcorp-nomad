@echo off
REM ------------------------------------------------------------
REM gh-devsync.bat
REM Fast-forward local 'dev' to the latest 'origin/main' and push.
REM (No hard reset, no git clean, no force push. Safe daily sync.)
REM
REM Usage:
REM   gh-devsync.bat [pathToRepo]
REM ------------------------------------------------------------

setlocal ENABLEDELAYEDEXPANSION

if "%~1"=="" (
  set "REPO=%CD%"
) else (
  set "REPO=%~1"
)

if not exist "%REPO%\." (
  echo [ERROR] Repository path not found: "%REPO%"
  exit /b 1
)

if not exist "%REPO%\.git" (
  echo [ERROR] Not a Git repository: "%REPO%"
  exit /b 1
)

git --version >NUL 2>&1
if errorlevel 1 (
  echo [ERROR] Git is not installed or not on PATH.
  exit /b 1
)

pushd "%REPO%" >NUL 2>&1

echo.
echo [INFO] Repository: %REPO%
echo.

echo [STEP] Fetching from origin...
git fetch origin --prune
if errorlevel 1 goto :error

echo [STEP] Switching to 'dev' (create from origin/main if missing)...
git switch dev 2>NUL || git switch -C dev origin/main
if errorlevel 1 goto :error

echo [STEP] Attempting fast-forward of 'dev' to 'origin/main'...
git merge --ff-only origin/main
if errorlevel 1 (
  echo.
  echo [WARN] Fast-forward failed (diverged history). No changes applied.
  echo        Resolve manually or run gh-devreset.bat to hard reset.
  goto :end
)

echo [STEP] Pushing 'dev' (no force)...
git push origin dev
if errorlevel 1 goto :error

echo.
echo [DONE] 'dev' is now fast-forwarded to 'origin/main' and pushed.
echo.

:end
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
