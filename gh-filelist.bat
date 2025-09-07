@echo off
REM ------------------------------------------------------------
REM gh-filelist.bat  (deluxe v5 - fix flat-list pipeline)
REM Overwrites (or creates) filelist.txt with:
REM   • Timestamp header
REM   • ASCII TREE view
REM   • Separator line
REM   • Flat list via DIR /S /B (EXCLUDES .git)
REM   • Totals (directories/files) (EXCLUDES .git)
REM ------------------------------------------------------------

setlocal EnableExtensions EnableDelayedExpansion

REM Resolve target folder and output file
if "%~1"=="" (
  set "TARGET=%CD%"
) else (
  set "TARGET=%~1"
)

if "%~2"=="" (
  set "OUT=filelist.txt"
) else (
  set "OUT=%~2"
)

REM Basic checks
if not exist "%TARGET%\." (
  echo [ERROR] Folder not found: "%TARGET%"
  exit /b 1
)

pushd "%TARGET%" >NUL 2>&1 || (
  echo [ERROR] Unable to enter folder: "%TARGET%"
  exit /b 1
)

set "STAMP=%DATE% %TIME%"

REM -------- Header --------
> "%OUT%" (
  echo ============================================================
  echo File list for: %TARGET%
  echo Generated on : %STAMP%
  echo Script       : gh-filelist.bat
  echo ============================================================
  echo(
  echo [TREE /A]
)

echo [INFO] Target: %TARGET%
echo [STEP] Writing ASCII TREE to "%OUT%"...
tree /A >> "%OUT%" 2>&1
set "EC=%ERRORLEVEL%"
if not "%EC%"=="0" echo [WARN] 'tree' returned exit code %EC%

REM -------- Separator --------
>> "%OUT%" echo(
>> "%OUT%" echo ------------------------------------------------------------
>> "%OUT%" echo [FLAT LIST: dir /s /b  (EXCLUDES .git)]
>> "%OUT%" echo ------------------------------------------------------------

echo [STEP] Appending filtered DIR /S /B (excluding .git) to "%OUT%"...
REM NOTE: No caret before the pipe here; previous version caused 'Invalid switch - "I".'
dir /s /b | findstr /R /I /V /C:"\\\.git\\\\" /C:"\\\.git$" >> "%OUT%" 2>&1
set "EC=%ERRORLEVEL%"
if not "%EC%"=="0" echo [WARN] 'dir/findstr' returned exit code %EC%

REM -------- Totals (exclude .git) --------
for /f %%F in ('dir /s /b /a-d ^| findstr /R /I /V /C:"\\\.git\\\\" /C:"\\\.git$" ^| find /v /c ""') do set FILES=%%F
for /f %%D in ('dir /s /b /ad  ^| findstr /R /I /V /C:"\\\.git\\\\" /C:"\\\.git$" ^| find /v /c ""') do set DIRS=%%D

>> "%OUT%" echo(
>> "%OUT%" echo [TOTALS  (EXCLUDES .git)]
>> "%OUT%" echo Directories: !DIRS!
>> "%OUT%" echo Files      : !FILES!

for %%A in ("%OUT%") do set "SIZE=%%~zA"

echo [DONE] Wrote "%TARGET%\%OUT%"  ^(Dirs: !DIRS!  Files: !FILES!  Size: !SIZE! bytes^)

popd >NUL 2>&1
exit /b 0
