@echo off
cd /d "%~dp0"
for %%I in (.) do set "folder=%%~nxI"
git remote set-url origin git@github.com:RakaMaru/%folder%.git 2>nul
git config --local core.sshCommand "ssh -i ""C:\Users\reyno\.ssh\id_ed25519_rakamaru""" 2>nul
echo [OK] Remote: git@github.com:RakaMaru/%folder%.git
echo [OK] Key:    C:\Users\reyno\.ssh\id_ed25519_rakamaru
git ls-remote origin HEAD >nul 2>&1 && echo [VERIFIED] SSH works! || echo [ERROR] Key missing
pause