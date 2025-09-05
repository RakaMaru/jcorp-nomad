@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Change to repo root (optional if you already run from there)
cd /d "%~dp0"

echo( Fetching origin...
git fetch origin

REM Make sure we're on dev
for /f "tokens=*" %%b in ('git branch --show-current') do set CURBR=%%b
if /I not "!CURBR!"=="dev" (
  echo( Switching to dev...
  git checkout dev || goto :error
)

REM Bring dev up to date with main (fast-forward if possible)
echo( Merging origin/main into dev (fast-forward if possible)...
git merge --ff-only origin/main >nul 2>&1
if errorlevel 1 (
  echo( Fast-forward not possible or merge conflict. Skipping merge step.
)

echo(
set /p COMMIT_MSG=Commit message (required): 
if not defined COMMIT_MSG (
  echo Commit message is required.
  exit /b 1
)

echo( Staging all changes...
git add -A || goto :error

echo( Committing...
git commit -m "%COMMIT_MSG%" || goto :after_commit

:after_commit
echo( Pushing to origin/dev...
git push origin dev || goto :error

set CREATE_PR=
set /p CREATE_PR=Create PR dev -> main and try to merge it now? [Y,N]? 
if /I "!CREATE_PR!"=="Y" (
  echo( Creating PR...
  gh pr create --base main --head dev --title "%COMMIT_MSG%" --body "%COMMIT_MSG%" || goto :error

  echo( Attempting merge...
  gh pr merge --merge --auto || goto :error
)

echo(
echo Done.
exit /b 0

:error
echo(
echo Something went wrong. Last command returned error %errorlevel%.
exit /b %errorlevel%
