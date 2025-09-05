@echo off
setlocal ENABLEDELAYEDEXPANSION

rem --- sanity: must be in a git repo ---
git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
  echo Not a git repository here.
  exit /b 1
)

rem --- ensure we are on dev ---
for /f "delims=" %%A in ('git branch --show-current') do set CURR=%%A
if /i not "%CURR%"=="dev" (
  echo You are on "%CURR%". Switching to dev...
  git checkout dev || exit /b 1
)

rem --- fetch and merge latest main into dev (fast-forward or conflict) ---
echo Fetching origin...
git fetch origin || exit /b 1
echo Merging origin/main into dev (fast-forward if possible)...
git merge --ff-only origin/main >nul 2>&1
if errorlevel 1 (
  echo Non-ff merge or conflicts. Skipping auto-merge. You can merge manually later.
)

:askmsg
set "MSG="
set /p MSG=Commit message (required): 
if not defined MSG goto askmsg

echo.
echo Staging all changes...
git add -A || exit /b 1

echo Committing...
git commit -m "%MSG%"
if errorlevel 1 (
  echo Nothing to commit or commit failed.
  goto maybePush
)

:maybePush
echo Pushing to origin/dev...
git push origin dev || exit /b 1

rem --- optional PR flow if gh is installed ---
where gh >nul 2>&1
if errorlevel 1 (
  echo gh CLI not found; skipping PR step.
  goto done
)

choice /c YN /m "Create PR dev -> main and try to merge it now?"
if errorlevel 2 goto done

echo Creating PR...
gh pr create --base main --head dev --title "%MSG%" --body "Automated commit: %MSG%"
if errorlevel 1 (
  echo Failed to create PR. Skipping merge.
  goto done
)

echo Attempting merge...
gh pr merge --merge --auto
if errorlevel 1 (
  echo Merge not completed (may need approvals or checks). Skipping.
  goto done
)

echo Pulling updated main and re-syncing dev...
git checkout main && git pull origin main
git checkout dev && git merge --ff-only main
git push origin dev

:done
echo.
echo Done.
endlocal
exit /b 0
