@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem =========================
rem Editable configuration
rem =========================
set "PYTHON_VERSION=3.12.8"
set "PYTHON_X64_URL=https://www.python.org/ftp/python/%PYTHON_VERSION%/python-%PYTHON_VERSION%-amd64.exe"
set "PYTHON_INSTALL_ARGS=/quiet InstallAllUsers=0 PrependPath=1 Include_pip=1 Include_launcher=1 Include_test=0"

set "GIT_VERSION=2.47.1"
set "GIT_X64_URL=https://github.com/git-for-windows/git/releases/download/v%GIT_VERSION%.windows.1/Git-%GIT_VERSION%-64-bit.exe"
set "GIT_INSTALL_ARGS=/VERYSILENT /NORESTART"

set "WINGET_PYTHON_ID=Python.Python.3.12"
set "WINGET_GIT_ID=Git.Git"
set "WINGET_RETRIES=3"
set "WINGET_RETRY_DELAY_SECONDS=4"

set "DOWNLOAD_RETRIES=4"
set "DOWNLOAD_RETRY_DELAY_SECONDS=4"
set "WARMUP_BUILD_RETRIES=2"

set "PYTHON_DOWNLOAD_PAGE=https://www.python.org/downloads/windows/"
set "GIT_DOWNLOAD_PAGE=https://git-scm.com/download/win"
set "ARDUINO_IDE_DOWNLOAD_PAGE=https://www.arduino.cc/en/software"

rem =========================
rem Internal paths
rem =========================
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "REPO_ROOT=%%~fI"
set "CORE_SOURCE_DIR=%REPO_ROOT%\hardware\nrf54l15\nrf54l15"
set "TEMP_DIR=%TEMP%\nrf54l15-prereqs"

call :log Starting Windows prerequisite installer
call :log Core source expected at "%CORE_SOURCE_DIR%"

if not exist "%CORE_SOURCE_DIR%\platform.txt" (
  call :log ERROR: core source not found. Run this script from the repository tree.
  exit /b 1
)

if not exist "%TEMP_DIR%" mkdir "%TEMP_DIR%"

call :ensure_python
if errorlevel 1 exit /b 1

call :ensure_git
if errorlevel 1 exit /b 1

call :detect_arduino_ide
if defined ARDUINO_IDE_EXE (
  call :log Arduino IDE detected: "!ARDUINO_IDE_EXE!"
) else (
  call :log Arduino IDE executable not detected. Download: !ARDUINO_IDE_DOWNLOAD_PAGE!
)

call :detect_sketchbook
if not defined SKETCHBOOK_DIR (
  set "SKETCHBOOK_DIR=%USERPROFILE%\Documents\Arduino"
)
set "SKETCHBOOK_DIR=%SKETCHBOOK_DIR:\"=%"
call :log Detected sketchbook path: "%SKETCHBOOK_DIR%"
set "DEFAULT_TARGET=%SKETCHBOOK_DIR%\hardware\nrf54l15\nrf54l15"
set "WARMUP_CORE_DIR=%CORE_SOURCE_DIR%"

choice /C YN /N /M "Install this core now to %DEFAULT_TARGET% ? [Y/N]: "
if errorlevel 2 (
  call :log Skipping core copy step.
  goto :done
)

call :install_core "%SKETCHBOOK_DIR%"
if errorlevel 1 exit /b 1
set "WARMUP_CORE_DIR=%DEFAULT_TARGET%"

:done
choice /C YN /N /M "Run one-time Zephyr warmup build now to avoid first-compile IDE ECONNRESET? [Y/N]: "
if errorlevel 2 goto :finish
call :warmup_zephyr "%WARMUP_CORE_DIR%"
if errorlevel 1 (
  call :log WARNING: warmup build failed. You can retry later from Command Prompt.
  call :log Run: python "!WARMUP_CORE_DIR!\tools\build_zephyr_lib.py"
) else (
  call :log Warmup build completed.
)

:finish
call :log Done.
exit /b 0

:ensure_python
where python >nul 2>&1
if not errorlevel 1 (
  for /f "delims=" %%V in ('python --version 2^>nul') do call :log Detected %%V
  goto :eof
)

call :log Python not found. Installing...
call :winget_install "%WINGET_PYTHON_ID%"
where python >nul 2>&1
if not errorlevel 1 (
  for /f "delims=" %%V in ('python --version 2^>nul') do call :log Installed %%V via winget
  goto :eof
)

set "PYTHON_EXE=%TEMP_DIR%\python-%PYTHON_VERSION%-amd64.exe"
call :download_file "%PYTHON_X64_URL%" "%PYTHON_EXE%"
if errorlevel 1 (
  call :log ERROR: Python download failed. Update PYTHON_X64_URL and retry.
  call :log Python downloads: %PYTHON_DOWNLOAD_PAGE%
  exit /b 1
)

call :run_installer "%PYTHON_EXE%" %PYTHON_INSTALL_ARGS%
if errorlevel 1 (
  call :log ERROR: Python installer failed. Try manual install from: %PYTHON_DOWNLOAD_PAGE%
  exit /b 1
)

where python >nul 2>&1
if errorlevel 1 (
  where py >nul 2>&1
  if not errorlevel 1 (
    for /f "delims=" %%V in ('py --version 2^>nul') do call :log Installed %%V - py launcher detected
    goto :eof
  )
  call :log WARNING: Python installed but not yet visible in this shell PATH. Reopen terminal if needed.
  goto :eof
)
for /f "delims=" %%V in ('python --version 2^>nul') do call :log Installed %%V
goto :eof

:ensure_git
where git >nul 2>&1
if not errorlevel 1 (
  for /f "delims=" %%V in ('git --version 2^>nul') do call :log Detected %%V
  goto :eof
)

call :log Git not found. Installing...
call :winget_install "%WINGET_GIT_ID%"
where git >nul 2>&1
if not errorlevel 1 (
  for /f "delims=" %%V in ('git --version 2^>nul') do call :log Installed %%V via winget
  goto :eof
)

set "GIT_EXE=%TEMP_DIR%\Git-%GIT_VERSION%-64-bit.exe"
call :download_file "%GIT_X64_URL%" "%GIT_EXE%"
if errorlevel 1 (
  call :log ERROR: Git download failed. Update GIT_X64_URL and retry.
  call :log Git downloads: %GIT_DOWNLOAD_PAGE%
  exit /b 1
)

call :run_installer "%GIT_EXE%" %GIT_INSTALL_ARGS%
if errorlevel 1 (
  call :log ERROR: Git installer failed. Try manual install from: %GIT_DOWNLOAD_PAGE%
  exit /b 1
)

where git >nul 2>&1
if errorlevel 1 (
  call :log WARNING: Git installed but not yet visible in this shell PATH. Reopen terminal if needed.
  goto :eof
)
for /f "delims=" %%V in ('git --version 2^>nul') do call :log Installed %%V
goto :eof

:winget_install
where winget >nul 2>&1
if errorlevel 1 goto :eof

set "WINGET_ID=%~1"
for /L %%I in (1,1,%WINGET_RETRIES%) do (
  call :log Trying winget install %%I/%WINGET_RETRIES%: !WINGET_ID!
  winget install --id "%WINGET_ID%" --exact --accept-source-agreements --accept-package-agreements -h >nul 2>&1
  if not errorlevel 1 goto :eof
  timeout /t %WINGET_RETRY_DELAY_SECONDS% /nobreak >nul
)
call :log winget install failed for !WINGET_ID!; falling back to direct installer.
goto :eof

:download_file
set "DL_URL=%~1"
set "DL_OUT=%~2"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ProgressPreference='SilentlyContinue';" ^
  "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;" ^
  "$u='%DL_URL%'; $o='%DL_OUT%'; $ok=$false;" ^
  "for($i=1;$i -le %DOWNLOAD_RETRIES%;$i++){try{Invoke-WebRequest -Uri $u -OutFile $o -UseBasicParsing; $ok=$true; break}catch{Start-Sleep -Seconds %DOWNLOAD_RETRY_DELAY_SECONDS%}}" ^
  "if($ok){exit 0}else{exit 1}"
if not errorlevel 1 exit /b 0

where curl >nul 2>&1
if not errorlevel 1 (
  call :log PowerShell download failed due to network reset. Trying curl fallback...
  curl.exe -L --retry %DOWNLOAD_RETRIES% --retry-all-errors --connect-timeout 30 --output "%DL_OUT%" "%DL_URL%" >nul 2>&1
  if not errorlevel 1 exit /b 0
)

call :log ERROR: download failed from %DL_URL%
exit /b 1

:run_installer
set "INSTALLER=%~1"
shift
call :log Running installer: "%INSTALLER%"
"%INSTALLER%" %*
if errorlevel 1 exit /b 1
exit /b 0

:detect_arduino_ide
set "ARDUINO_IDE_EXE="
for %%P in (
  "%LocalAppData%\Programs\Arduino IDE\Arduino IDE.exe"
  "%ProgramFiles%\Arduino IDE\Arduino IDE.exe"
  "%ProgramFiles(x86)%\Arduino IDE\Arduino IDE.exe"
) do (
  if exist %%~P (
    set "ARDUINO_IDE_EXE=%%~fP"
    goto :eof
  )
)

for /f "delims=" %%P in ('where arduino-ide 2^>nul') do (
  set "ARDUINO_IDE_EXE=%%~fP"
  goto :eof
)

goto :eof

:detect_sketchbook
set "SKETCHBOOK_DIR="
set "SETTINGS_JSON=%USERPROFILE%\.arduinoIDE\settings.json"
if exist "%SETTINGS_JSON%" (
  for /f "usebackq delims=" %%S in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "try{$j=Get-Content -Raw '%SETTINGS_JSON%' | ConvertFrom-Json; if($j.'sketchbook.path'){[Console]::Write($j.'sketchbook.path')} elseif($j.sketchbookPath){[Console]::Write($j.sketchbookPath)}} catch {}"`) do set "SKETCHBOOK_DIR=%%S"
)
if defined SKETCHBOOK_DIR goto :eof

set "PREFS_TXT=%LOCALAPPDATA%\Arduino15\preferences.txt"
if exist "%PREFS_TXT%" (
  for /f "usebackq tokens=1,* delims==" %%A in (`findstr /B /C:"sketchbook.path=" "%PREFS_TXT%"`) do (
    if /I "%%A"=="sketchbook.path" set "SKETCHBOOK_DIR=%%B"
  )
)
if defined SKETCHBOOK_DIR goto :eof

if exist "%USERPROFILE%\Documents\Arduino" set "SKETCHBOOK_DIR=%USERPROFILE%\Documents\Arduino"
if defined SKETCHBOOK_DIR goto :eof

if exist "%USERPROFILE%\Arduino" set "SKETCHBOOK_DIR=%USERPROFILE%\Arduino"
goto :eof

:install_core
set "SKETCHBOOK=%~1"
set "SKETCHBOOK=%SKETCHBOOK:\"=%"
set "TARGET_PARENT=%SKETCHBOOK%\hardware\nrf54l15"
set "TARGET_DIR=%TARGET_PARENT%\nrf54l15"

if not exist "%SKETCHBOOK%" (
  mkdir "%SKETCHBOOK%" >nul 2>&1
)
if not exist "%SKETCHBOOK%" (
  powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "try { New-Item -Path '%SKETCHBOOK%' -ItemType Directory -Force | Out-Null; exit 0 } catch { exit 1 }" >nul 2>&1
)

if not exist "%TARGET_PARENT%" (
  mkdir "%TARGET_PARENT%" >nul 2>&1
)
if not exist "%TARGET_PARENT%" (
  powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "try { New-Item -Path '%TARGET_PARENT%' -ItemType Directory -Force | Out-Null; exit 0 } catch { exit 1 }" >nul 2>&1
)
if not exist "%TARGET_PARENT%" (
  call :log ERROR: failed to create "%TARGET_PARENT%"
  exit /b 1
)

robocopy "%CORE_SOURCE_DIR%" "%TARGET_DIR%" /MIR /NFL /NDL /NJH /NJS /NP >nul
set "ROBO_RC=%ERRORLEVEL%"
if %ROBO_RC% GEQ 8 (
  call :log ERROR: robocopy failed with exit code %ROBO_RC%
  exit /b 1
)

call :log Core installed to "%TARGET_DIR%"
exit /b 0

:warmup_zephyr
set "WARMUP_CORE=%~1"
set "WARMUP_CORE=%WARMUP_CORE:\"=%"
set "BUILD_SCRIPT=%WARMUP_CORE%\tools\build_zephyr_lib.py"

if not exist "%BUILD_SCRIPT%" (
  call :log ERROR: warmup script not found at "%BUILD_SCRIPT%"
  exit /b 1
)

set "PY_KIND="
where python >nul 2>&1
if not errorlevel 1 set "PY_KIND=python"
if not defined PY_KIND (
  where py >nul 2>&1
  if not errorlevel 1 set "PY_KIND=py"
)
if not defined PY_KIND (
  call :log ERROR: Python launcher not found for warmup build.
  exit /b 1
)

set "OLD_ZEPHYR_BASE=%ZEPHYR_BASE%"
set "ZEPHYR_BASE="
for /L %%I in (1,1,%WARMUP_BUILD_RETRIES%) do (
  call :log Warmup build attempt %%I/%WARMUP_BUILD_RETRIES% in "!WARMUP_CORE!"
  if /I "!PY_KIND!"=="python" (
    python "!BUILD_SCRIPT!" --quiet
  ) else (
    py -3 "!BUILD_SCRIPT!" --quiet
  )
  if not errorlevel 1 (
    set "ZEPHYR_BASE=!OLD_ZEPHYR_BASE!"
    exit /b 0
  )
  timeout /t 3 /nobreak >nul
)
set "ZEPHYR_BASE=!OLD_ZEPHYR_BASE!"
exit /b 1

:log
echo [nrf54l15-setup] %*
exit /b 0
