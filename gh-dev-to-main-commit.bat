@echo off
setlocal EnableExtensions

REM --------------------------------------------------
REM Ensure we're on dev (switch only if not already)
REM --------------------------------------------------
for /f "delims=" %%b in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set "BRANCH=%%b"
if /I not "%BRANCH%"=="dev" (
  echo Switching to dev...
  git checkout dev || goto :fail
)

echo Fetching origin...
git fetch origin || goto :fail

REM --------------------------------------------------
REM Fast-forward dev with main if possible
REM --------------------------------------------------
echo Merging origin/main into dev (fast-forward if possible)...
git merge --ff-only origin/main >nul 2>&1
if errorlevel 1 (
  git merge --ff-only origin/main
)

REM --------------------------------------------------
REM Commit message (arg1 or prompt)
REM --------------------------------------------------
set "MSG=%~1"
if not defined MSG (
  echo/
  set /p "MSG=Commit message (required): "
)
if not defined MSG (
  echo Commit message is required. Aborting.
  goto :done
)

echo Staging all changes...
git add -A || goto :fail

echo Committing...
git commit -m "%MSG%" || echo Nothing to commit.

echo Pushing to origin/dev...
git push origin dev || goto :fail

REM --------------------------------------------------
REM Optional PR step: pass -y as arg2 to auto-approve
REM --------------------------------------------------
set "AUTOFLAG=%~2"
set "PRCHOICE="
if /I "%AUTOFLAG%"=="-y" (
  set "PRCHOICE=Y"
) else (
  set /p "PRCHOICE=Create PR dev -> main and try to merge it now? [Y,N]? "
)

if /I "%PRCHOICE%"=="Y" (
  echo Creating PR...
  gh pr create --base main --head dev --title "%MSG%" --body "%MSG%" || goto :fail
  echo Attempting merge...
  gh pr merge --merge || goto :fail

  echo === Syncing local main with origin/main...
  git checkout main || goto :fail
  git pull origin main || goto :fail

  echo === Switching back to dev...
  git checkout dev || goto :fail
  git merge main --no-edit || goto :fail
  git push origin dev || goto :fail
)

:done
echo/
echo ==================================================
echo Done. You are now back on the DEV branch,
echo    synced with origin/main.
echo ==================================================
exit /b 0

:fail
echo/
echo ERROR: a command failed. Aborting.
exit /b 1
