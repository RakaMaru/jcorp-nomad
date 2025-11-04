@echo off
set "GH_USER=RakaMaru"
echo [OK] Switching to %GH_USER%...
gh auth switch -u %GH_USER% -h github.com >nul 2>&1 || gh auth login --hostname github.com --git-protocol ssh --web
echo [OK] Active: %GH_USER%
gh auth status
pause