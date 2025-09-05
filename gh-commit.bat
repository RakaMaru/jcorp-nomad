@echo off
setlocal enableextensions

REM --- Always run from repo root (this file's folder) ---
cd /d "%~dp0"

echo Fetching origin...
git fetch origin

REM --- Make sure we’re on dev ---
for /f "tokens=1" %%B in ('git rev-parse --abbrev-ref HEAD') do set "CURBR=%%B"
if /i not "%CURBR%"=="dev" (
  echo Switching to dev...
  git checkout dev || goto :ERR
)

echo Merging origin/main into dev (fast-forward if possible)...
git merge --ff-only origin/main
if errorlevel 1 goto :MERGE_CONFLICT

REM --- Get commit message (arg or prompt) ---
set "MSG="
if "%~1"=="" (
  set /p "MSG=Commit message (required): "
) else (
  set "MSG=%*"
)

if not defined MSG (
  echo No commit message provided. Aborting.
  goto :END
)

echo Staging all changes...
git add -A

echo Committing...
git commit -m "%MSG%" >nul 2>&1
if errorlevel 1 (
  echo Nothing to commit (working tree clean).
) else (
  echo Commit created.
)

echo Pushing to origin/dev...
git push origin dev || goto :ERR

echo Done.
goto :END

:MERGE_CONFLICT
echo.
echo Merge failed (not fast-forward). Resolve conflicts, then run this again.
goto :END

:ERR
echo.
echo An unexpected error occurred. Aborting.
goto :END

:END
endlocal
