@echo off
REM ------------------------------------------------------------
REM gh-dev-sync.bat  (hardened v2)
REM Fast-forward local 'dev' to the latest 'origin/main' and push.
REM No hard reset, no git clean, no force push.
REM ------------------------------------------------------------

setlocal

REM Repo path
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

git --version >NUL 2>&1 || (echo [ERROR] Git not found & exit /b 1)

pushd "%REPO%" >NUL 2>&1

echo [INFO] Repository: %REPO%
echo [STEP] Fetching from origin...
git fetch origin --prune || goto :error

echo [STEP] Switching to 'dev' (create from origin/main if missing)...
git switch dev 2>NUL || git switch -C dev origin/main || goto :error

echo [STEP] Attempting fast-forward of 'dev' to 'origin/main'...
git merge --ff-only origin/main || goto :ffFailed

echo [STEP] Pushing 'dev' (no force)...
git push origin dev || goto :error

echo [DONE] 'dev' is now fast-forwarded to 'origin/main' and pushed.
goto :summary

:ffFailed
echo [WARN] Fast-forward failed (diverged history). No changes applied.
echo        Resolve manually or run gh-dev-reset.bat to hard reset.
goto :summary

:summary
echo [SUMMARY]
git status -sb
git log --oneline --decorate -n 1

popd >NUL 2>&1
exit /b 0

:error
echo [FAILED] An error occurred. See messages above.
popd >NUL 2>&1
exit /b 1
