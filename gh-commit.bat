@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ===== Repo root sanity check =====
if not exist ".git" (
  echo This doesn't look like a Git repo. Aborting.
  exit /b 1
)

REM ===== Ensure we're on dev =====
for /f "delims=" %%B in ('git rev-parse --abbrev-ref HEAD') do set "CUR_BRANCH=%%B"
if /I not "%CUR_BRANCH%"=="dev" (
  echo Switching to dev...
  git checkout dev || (echo Failed to switch to dev.& exit /b 1)
)

REM ===== Fetch and fast-forward dev with origin/main =====
echo Fetching origin...
git fetch origin || (echo Fetch failed.& exit /b 1)

echo Merging origin/main into dev (fast-forward if possible)...
git merge --ff-only origin/main >nul 2>&1
if errorlevel 1 (
  echo (No fast-forward; continuing without merge.)
) else (
  echo Already up to date.>nul
)

REM ===== Get commit message (args or prompt) =====
set "COMMIT_MSG=%*"
if "%COMMIT_MSG%"=="" (
  <nul set /p "=Commit message (required): "
  set /p "COMMIT_MSG="
  echo(
)
if "%COMMIT_MSG%"=="" (
  echo No commit message provided. Aborting.
  exit /b 1
)

REM ===== Stage, detect if anything changed =====
echo Staging all changes...
git add -A

git diff --cached --quiet
set "DIFF_ERR=%ERRORLEVEL%"
if "%DIFF_ERR%"=="0" (
  echo Nothing to commit. Skipping commit/push.
  goto OPTIONAL_PR
)

REM ===== Commit using a temp file to avoid quoting issues =====
echo Committing...
set "MSGFILE=%TEMP%\ghmsg_%RANDOM%.txt"
> "%MSGFILE%" echo %COMMIT_MSG%
git commit -F "%MSGFILE%" || (del "%MSGFILE%" >nul 2>&1 & echo Commit failed.& exit /b 1)
del "%MSGFILE%" >nul 2>&1

REM ===== Push =====
echo Pushing to origin/dev...
git push origin dev || (echo Push failed.& exit /b 1)

:OPTIONAL_PR
REM ===== Optional PR step =====
where gh >nul 2>&1
if errorlevel 1 goto DONE

set "MAKEPR="
set /p "MAKEPR=Create PR dev -> main and try to merge it now? [Y,N]? "
if /I "%MAKEPR%"=="Y" (
  echo Creating PR...
  gh pr create --base main --head dev --title "%COMMIT_MSG%" --body "%COMMIT_MSG%"
  if errorlevel 1 goto DONE
  echo Attempting merge...
  gh pr merge --merge
)

:DONE
echo Done.
endlocal
exit /b 0
