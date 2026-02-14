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

set "CMAKE_VERSION=3.31.6"
set "CMAKE_X64_URL=https://github.com/Kitware/CMake/releases/download/v%CMAKE_VERSION%/cmake-%CMAKE_VERSION%-windows-x86_64.msi"
set "CMAKE_INSTALL_ARGS=/qn /norestart"

set "WINGET_PYTHON_ID=Python.Python.3.12"
set "WINGET_GIT_ID=Git.Git"
set "WINGET_CMAKE_ID=Kitware.CMake"
set "WINGET_DTC_ID=oss-winget.dtc"
set "WINGET_NINJA_ID=Ninja-build.Ninja"
set "WINGET_RETRIES=3"
set "WINGET_RETRY_DELAY_SECONDS=4"

set "DOWNLOAD_RETRIES=4"
set "DOWNLOAD_RETRY_DELAY_SECONDS=4"
set "WARMUP_BUILD_RETRIES=2"

set "PYTHON_DOWNLOAD_PAGE=https://www.python.org/downloads/windows/"
set "GIT_DOWNLOAD_PAGE=https://git-scm.com/download/win"
set "CMAKE_DOWNLOAD_PAGE=https://cmake.org/download/"
set "ARDUINO_IDE_DOWNLOAD_PAGE=https://www.arduino.cc/en/software"

rem =========================
rem Internal paths
rem =========================
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "REPO_ROOT=%%~fI"
set "CORE_SOURCE_DIR=%REPO_ROOT%\hardware\nrf54l15\nrf54l15"
set "TEMP_DIR=%TEMP%\nrf54l15-prereqs"
set "SKIP_CORE_COPY=0"
set "SKIP_BOOTSTRAP=0"
set "SHOW_HELP=0"

call :parse_args %*
if errorlevel 1 exit /b 1
if "%SHOW_HELP%"=="1" (
  call :print_help
  exit /b 0
)

call :log Starting Windows prerequisite installer
call :log Core source expected at "%CORE_SOURCE_DIR%"

if not exist "%CORE_SOURCE_DIR%\platform.txt" (
  call :log ERROR: core source not found. Run this script from the repository tree.
  exit /b 1
)

if not exist "%TEMP_DIR%" mkdir "%TEMP_DIR%"

call :log Step 1/7: Ensuring Python is installed...
call :ensure_python
if errorlevel 1 exit /b 1

call :log Step 2/7: Ensuring Git is installed...
call :ensure_git
if errorlevel 1 exit /b 1

call :log Step 3/7: Ensuring CMake is installed...
call :ensure_cmake
if errorlevel 1 exit /b 1

call :log Ensuring DTC is installed...
call :ensure_dtc
if errorlevel 1 exit /b 1

call :log Ensuring Ninja is installed...
call :ensure_ninja
if errorlevel 1 exit /b 1

call :log Step 4/7: Detecting Arduino IDE installation...
call :detect_arduino_ide
if defined ARDUINO_IDE_EXE (
  call :log Arduino IDE detected: "!ARDUINO_IDE_EXE!"
) else (
  call :log Arduino IDE executable not detected. Download: !ARDUINO_IDE_DOWNLOAD_PAGE!
)

call :log Step 5/7: Detecting sketchbook path...
call :detect_sketchbook
if not defined SKETCHBOOK_DIR (
  set "SKETCHBOOK_DIR=%USERPROFILE%\Documents\Arduino"
)
set "SKETCHBOOK_DIR=%SKETCHBOOK_DIR:\"=%"
call :log Detected sketchbook path: "%SKETCHBOOK_DIR%"
set "DEFAULT_TARGET=%SKETCHBOOK_DIR%\hardware\nrf54l15\nrf54l15"

if "%SKIP_CORE_COPY%"=="1" (
  call :log Step 6/7: Skipping core copy step. Flag --skip-core-copy is set.
  set "WARMUP_CORE_DIR=%CORE_SOURCE_DIR%"
) else (
  call :log Step 6/7: Installing core into sketchbook hardware path...
  call :install_core "%SKETCHBOOK_DIR%"
  if errorlevel 1 exit /b 1
  set "WARMUP_CORE_DIR=%DEFAULT_TARGET%"
)

if "%SKIP_BOOTSTRAP%"=="1" (
  call :log Step 7/7: Skipping SDK/bootstrap and warmup build. Flag --skip-bootstrap is set.
  call :log Done.
  exit /b 0
)

call :log Step 7/7: Bootstrapping NCS/SDK and running warmup build...
call :prepare_zephyr "%WARMUP_CORE_DIR%"
if errorlevel 1 (
  call :log ERROR: SDK/bootstrap or warmup build failed.
  call :log You can retry manually from Command Prompt:
  call :log py -3 "%WARMUP_CORE_DIR%\tools\get_nrf_connect.py"
  call :log py -3 "%WARMUP_CORE_DIR%\tools\get_toolchain.py"
  call :log py -3 "%WARMUP_CORE_DIR%\tools\build_zephyr_lib.py"
  exit /b 1
)

call :log Done.
exit /b 0

:parse_args
if "%~1"=="" goto :eof
if /I "%~1"=="--skip-core-copy" (
  set "SKIP_CORE_COPY=1"
  shift
  goto parse_args
)
if /I "%~1"=="--skip-bootstrap" (
  set "SKIP_BOOTSTRAP=1"
  shift
  goto parse_args
)
if /I "%~1"=="--help" (
  set "SHOW_HELP=1"
  shift
  goto parse_args
)
call :log ERROR: Unknown argument: %~1
call :log Use --help to list options.
exit /b 1

:print_help
echo.
echo nRF54L15 Windows setup helper
echo.
echo Usage:
echo   install_windows.bat [--skip-core-copy] [--skip-bootstrap]
echo.
echo Default behavior:
echo   1^) Ensure Python, Git, CMake, DTC, and Ninja are installed.
echo   2^) Copy core into sketchbook hardware path.
echo   3^) Download/bootstrap NCS workspace and Zephyr SDK.
echo   4^) Build Zephyr base artifacts so IDE compiles are sketch-focused.
echo.
echo Options:
echo   --skip-core-copy   Skip copying repo core into sketchbook.
echo   --skip-bootstrap   Skip NCS/SDK download and warmup build.
echo   --help             Show this help text.
echo.
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

:ensure_cmake
set "CMAKE_BIN_DIR="
call :resolve_cmake
if not errorlevel 1 (
  for /f "delims=" %%V in ('cmake --version 2^>nul ^| findstr /R /C:"cmake version"') do call :log Detected %%V
  goto :eof
)

call :log CMake not found. Installing...
call :winget_install "%WINGET_CMAKE_ID%"
call :resolve_cmake
if not errorlevel 1 (
  for /f "delims=" %%V in ('cmake --version 2^>nul ^| findstr /R /C:"cmake version"') do call :log Installed %%V via winget/known path
  goto :eof
)

set "CMAKE_MSI=%TEMP_DIR%\cmake-%CMAKE_VERSION%-windows-x86_64.msi"
call :download_file "%CMAKE_X64_URL%" "%CMAKE_MSI%"
if errorlevel 1 (
  call :log ERROR: CMake download failed. Update CMAKE_X64_URL and retry.
  call :log CMake downloads: %CMAKE_DOWNLOAD_PAGE%
  exit /b 1
)

call :run_msi "%CMAKE_MSI%" %CMAKE_INSTALL_ARGS%
if errorlevel 1 (
  call :log ERROR: CMake installer failed. Try manual install from: %CMAKE_DOWNLOAD_PAGE%
  exit /b 1
)

call :resolve_cmake
if not errorlevel 1 (
  for /f "delims=" %%V in ('cmake --version 2^>nul ^| findstr /R /C:"cmake version"') do call :log Installed %%V
  goto :eof
)

call :log ERROR: CMake installed but still not visible in this shell PATH.
call :log Reopen terminal and retry, or install manually from: %CMAKE_DOWNLOAD_PAGE%
exit /b 1

:ensure_dtc
call :resolve_dtc
if not errorlevel 1 (
  for /f "delims=" %%V in ('dtc --version 2^>nul ^| findstr /R /C:"Version:" /C:"DTC"') do call :log Detected %%V
  goto :eof
)

call :log Device Tree Compiler (dtc) not found. Installing via winget...
call :winget_install "%WINGET_DTC_ID%"
call :resolve_dtc
if not errorlevel 1 (
  for /f "delims=" %%V in ('dtc --version 2^>nul ^| findstr /R /C:"Version:" /C:"DTC"') do call :log Installed %%V via winget
  goto :eof
)

call :log ERROR: dtc still not found after installation.
call :log Install manually: winget install --id %WINGET_DTC_ID% --exact
exit /b 1

:ensure_ninja
call :resolve_ninja
if not errorlevel 1 (
  for /f "delims=" %%V in ('ninja --version 2^>nul') do call :log Detected ninja %%V
  goto :eof
)

call :log Ninja build tool not found. Installing via winget...
call :winget_install "%WINGET_NINJA_ID%"
call :resolve_ninja
if not errorlevel 1 (
  for /f "delims=" %%V in ('ninja --version 2^>nul') do call :log Installed ninja %%V via winget
  goto :eof
)

call :log ERROR: ninja still not found after installation.
call :log Install manually: winget install --id %WINGET_NINJA_ID% --exact
exit /b 1

:resolve_ninja
where ninja >nul 2>&1
if not errorlevel 1 exit /b 0

set "NINJA_BIN_DIR="
for /d %%D in ("%LocalAppData%\Microsoft\WinGet\Packages\Ninja-build.Ninja*") do (
  for /f "delims=" %%F in ('dir /b /s "%%~fD\ninja.exe" 2^>nul') do (
    set "NINJA_BIN_DIR=%%~dpF"
    goto have_ninja_dir
  )
)

:have_ninja_dir
if defined NINJA_BIN_DIR (
  set "PATH=%NINJA_BIN_DIR%;%PATH%"
  where ninja >nul 2>&1
  if not errorlevel 1 (
    call :log Using ninja from "%NINJA_BIN_DIR%"
    exit /b 0
  )
)
exit /b 1

:resolve_dtc
where dtc >nul 2>&1
if not errorlevel 1 exit /b 0

set "DTC_BIN_DIR="
for /d %%D in ("%LocalAppData%\Microsoft\WinGet\Packages\oss-winget.dtc*") do (
  if exist "%%~fD\usr\bin\dtc.exe" (
    set "DTC_BIN_DIR=%%~fD\usr\bin"
    goto have_dtc_dir
  )
)

:have_dtc_dir
if defined DTC_BIN_DIR (
  set "PATH=%DTC_BIN_DIR%;%PATH%"
  where dtc >nul 2>&1
  if not errorlevel 1 (
    call :log Using dtc from "%DTC_BIN_DIR%"
    exit /b 0
  )
)
exit /b 1

:resolve_cmake
where cmake >nul 2>&1
if not errorlevel 1 exit /b 0

set "CMAKE_BIN_DIR="
if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_BIN_DIR=%ProgramFiles%\CMake\bin"
if not defined CMAKE_BIN_DIR if exist "%ProgramFiles(x86)%\CMake\bin\cmake.exe" set "CMAKE_BIN_DIR=%ProgramFiles(x86)%\CMake\bin"
if not defined CMAKE_BIN_DIR if exist "%LocalAppData%\Programs\CMake\bin\cmake.exe" set "CMAKE_BIN_DIR=%LocalAppData%\Programs\CMake\bin"

if defined CMAKE_BIN_DIR (
  set "PATH=%CMAKE_BIN_DIR%;%PATH%"
  where cmake >nul 2>&1
  if not errorlevel 1 (
    call :log Using CMake from "%CMAKE_BIN_DIR%"
    exit /b 0
  )
)
exit /b 1

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

:run_msi
set "MSI_FILE=%~1"
shift
call :log Running MSI installer: "%MSI_FILE%"
msiexec /i "%MSI_FILE%" %*
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

rem Use /E (not /MIR) so local bootstrap caches under tools\ are preserved
rem across reruns (ncs, zephyr-sdk, host tools archives, etc.).
robocopy "%CORE_SOURCE_DIR%" "%TARGET_DIR%" /E /NFL /NDL /NJH /NJS /NP ^
  /XD "%CORE_SOURCE_DIR%\tools\ncs" "%CORE_SOURCE_DIR%\tools\zephyr-sdk" "%CORE_SOURCE_DIR%\tools\.arduino-zephyr-build" "%CORE_SOURCE_DIR%\tools\mingit" "%CORE_SOURCE_DIR%\variants\xiao_nrf54l15\zephyr_lib" ^
  /XF "zephyr-sdk-*.7z" "zephyr-sdk-*.tar.xz" "zephyr-sdk-*.zip" "hosttools_*.tar.xz" "MinGit-*.zip" >nul
set "ROBO_RC=%ERRORLEVEL%"
if %ROBO_RC% GEQ 8 (
  call :log ERROR: robocopy failed with exit code %ROBO_RC%
  exit /b 1
)

call :log Core installed to "%TARGET_DIR%"
exit /b 0

:prepare_zephyr
set "WARMUP_CORE=%~1"
set "WARMUP_CORE=%WARMUP_CORE:\"=%"
set "NCS_SCRIPT=%WARMUP_CORE%\tools\get_nrf_connect.py"
set "SDK_SCRIPT=%WARMUP_CORE%\tools\get_toolchain.py"
set "BUILD_SCRIPT=%WARMUP_CORE%\tools\build_zephyr_lib.py"

if not exist "%NCS_SCRIPT%" (
  call :log ERROR: Missing "%NCS_SCRIPT%"
  exit /b 1
)
if not exist "%SDK_SCRIPT%" (
  call :log ERROR: Missing "%SDK_SCRIPT%"
  exit /b 1
)
if not exist "%BUILD_SCRIPT%" (
  call :log ERROR: Missing "%BUILD_SCRIPT%"
  exit /b 1
)

call :log Bootstrapping nRF Connect workspace...
call :run_python_file "%NCS_SCRIPT%"
if errorlevel 1 exit /b 1

call :log Bootstrapping Zephyr SDK toolchain...
call :run_python_file "%SDK_SCRIPT%"
if errorlevel 1 exit /b 1

call :warmup_zephyr "%WARMUP_CORE%"
if errorlevel 1 exit /b 1

call :log Warmup build completed.
exit /b 0

:run_python_file
set "PY_SCRIPT=%~1"
set "PY_ARGS=%~2 %~3 %~4 %~5 %~6 %~7 %~8 %~9"
if not exist "%PY_SCRIPT%" (
  call :log ERROR: Python script not found: "%PY_SCRIPT%"
  exit /b 1
)

set "PY_KIND="
where py >nul 2>&1
if not errorlevel 1 (
  py -3 --version >nul 2>&1
  if not errorlevel 1 set "PY_KIND=py"
)
if not defined PY_KIND (
  where python >nul 2>&1
  if not errorlevel 1 set "PY_KIND=python"
)
if not defined PY_KIND (
  call :log ERROR: Python launcher not found for script execution.
  exit /b 1
)

if /I "!PY_KIND!"=="py" (
  py -3 "%PY_SCRIPT%" %PY_ARGS%
) else (
  python "%PY_SCRIPT%" %PY_ARGS%
)
if errorlevel 1 exit /b 1
exit /b 0

:warmup_zephyr
set "WARMUP_CORE=%~1"
set "WARMUP_CORE=%WARMUP_CORE:\"=%"
set "BUILD_SCRIPT=%WARMUP_CORE%\tools\build_zephyr_lib.py"

if not exist "%BUILD_SCRIPT%" (
  call :log ERROR: warmup script not found at "%BUILD_SCRIPT%"
  exit /b 1
)

set "OLD_ZEPHYR_BASE=%ZEPHYR_BASE%"
set "ZEPHYR_BASE="
for /L %%I in (1,1,%WARMUP_BUILD_RETRIES%) do (
  call :log Warmup build attempt %%I/%WARMUP_BUILD_RETRIES% in "!WARMUP_CORE!"
  call :run_python_file "%BUILD_SCRIPT%" --quiet
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
