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
REM Fast-forward dev with origin/main if possible (no merges)
REM --------------------------------------------------
echo Merging origin/main into dev ^(fast-forward if possible^)...
git merge --ff-only origin/main >nul 2>&1
if errorlevel 1 (
  REM Either already up to date or not fast-forward; try visible for messages
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
  goto :finish
)

echo Staging all changes...
git add -A || goto :fail

echo Committing...
git commit -m "%MSG%" || echo Nothing to commit.

echo Pushing to origin/dev...
git push origin dev || goto :fail

REM --------------------------------------------------
REM Skip PR if dev has no commits ahead of main
REM --------------------------------------------------
for /f %%c in ('git rev-list --count main..dev') do set AHEAD=%%c
if "%AHEAD%"=="0" (
  echo No commits between dev and main; skipping PR step.
  goto sync_block
)

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
) else (
  echo Skipping PR/merge by user choice.
)

REM --------------------------------------------------
REM Always run sync next
REM --------------------------------------------------
:sync_block
echo.
echo === Syncing local main with origin/main...
git checkout main || goto :fail
git pull origin main || goto :fail

echo.
echo === Syncing dev with main...
git checkout dev || goto :fail
git merge main --no-edit || goto :fail
git push origin dev || goto :fail

echo.
echo === Final status check ===
git log --oneline --decorate -n 5
git status

echo.
echo ✅ Dev and Main are fully synced with origin!

:finish
REM Ensure we end on dev even if a future edit reorders steps
git checkout dev >nul 2>&1
echo Done.
endlocal
exit /b 0

:fail
echo/
echo ERROR: a command failed. Aborting.
REM Try to get you back on dev if possible
git checkout dev >nul 2>&1
endlocal
exit /b 1
