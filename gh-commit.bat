@echo off
setlocal EnableExtensions EnableDelayedExpansion
title gh-commit (dev workflow)

rem ===== sanity checks =====
where git >nul 2>&1 || (echo [ERROR] git not found in PATH & goto :end_error)
git rev-parse --is-inside-work-tree >nul 2>&1 || (echo [ERROR] Not a git repo here. cd into your repo first. & goto :end_error)

rem ===== ensure we’re on dev =====
for /f "usebackq tokens=*" %%B in (`git rev-parse --abbrev-ref HEAD`) do set CURBR=%%B
if /I not "!CURBR!"=="dev" (
  echo Switching to dev...
  git switch dev || (echo [ERROR] Failed to switch to dev & goto :end_error)
)

rem ===== update remotes and fast-forward dev with origin/main =====
echo Fetching origin...
git fetch origin || (echo [ERROR] git fetch failed & goto :end_error)

echo Merging origin/main into dev (fast-forward if possible)...
git merge --ff-only origin/main || (
  echo(
  echo [WARN] Could not fast-forward dev with origin/main.
  echo        If this is expected, you can resolve later.
)

rem ===== get commit message (required) =====
set "COMMIT_MSG="
:askmsg
set /p COMMIT_MSG=Commit message (required): 
if not defined COMMIT_MSG goto :askmsg

rem ===== stage and commit =====
echo Staging all changes...
git add -A

echo Committing...
git commit -m "%COMMIT_MSG%" >nul 2>&1
if errorlevel 1 (
  rem If nothing to commit, keep going (push may still be needed)
  echo No changes to commit (working tree might be clean).
) else (
  for /f "usebackq tokens=*" %%h in (`git rev-parse --short HEAD`) do set NEWHEAD=%%h
  echo Created commit !NEWHEAD!
)

rem ===== push dev =====
echo Pushing to origin/dev...
git push origin dev || (echo [ERROR] Push to origin/dev failed & goto :end_error)

rem ===== optional PR creation & merge =====
where gh >nul 2>&1
if errorlevel 1 (
  echo(
  echo [INFO] GitHub CLI (gh) not found. Skipping PR creation/merge step.
  goto :done
)

echo(
choice /C YN /M "Create PR dev -> main and try to merge it now"
if errorlevel 2 goto :done

echo Creating PR...
gh pr create --base main --head dev --title "%COMMIT_MSG%" --body "%COMMIT_MSG%" -f || (
  echo [WARN] PR create failed; continuing without merge.
  goto :done
)

echo Attempting merge...
rem --merge uses a merge commit; change to --squash or --rebase if you prefer
gh pr merge --merge --auto || (
  echo [WARN] Merge did not complete (checks required or branch protection). You can finish it on GitHub.
)

goto :done

:done
echo(
echo ====================================
echo ===           ALL DONE           ===
echo ====================================
endlocal
exit /b 0

:end_error
echo(
echo ====================================
echo ===        FINISHED (ERROR)      ===
echo ====================================
endlocal
exit /b 1
