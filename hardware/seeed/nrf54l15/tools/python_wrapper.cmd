@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PLATFORM_DIR=%SCRIPT_DIR%.."
set "PY_CMD="
set "USE_VENV=0"

REM Check for venv Python first (for PyYAML 5.4.1 compatibility)
if exist "%SCRIPT_DIR%.venv\Scripts\python.exe" (
  set "PY_CMD=%SCRIPT_DIR%.venv\Scripts\python.exe"
  set "USE_VENV=1"
  goto :check_pyyaml
)

py -3 --version >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  set "PY_CMD=py -3"
  goto :check_pyyaml
)

python3 --version >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  set "PY_CMD=python3"
  goto :check_pyyaml
)

python --version >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  set "PY_CMD=python"
  goto :check_pyyaml
)

echo Python 3 is required for Seeed nRF54L15 core tooling. 1>&2
echo Install Python 3 from https://www.python.org/downloads/windows/ 1>&2
echo During install, enable "Add python.exe to PATH". 1>&2
echo If Windows App Execution Aliases intercept python.exe, disable them in 1>&2
echo Settings ^> Apps ^> App execution aliases. 1>&2
exit /b 9009

:check_pyyaml
REM Check if PyYAML is compatible (version 5.x)
if "%USE_VENV%"=="1" goto :run

REM Quick check - if PyYAML check fails, create venv
%PY_CMD% -c "import yaml; assert yaml.__version__.startswith('5.')" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
  echo Creating Python virtual environment with PyYAML 5.4.1...
  %PY_CMD% -m venv "%SCRIPT_DIR%.venv" >nul 2>&1
  if exist "%SCRIPT_DIR%.venv\Scripts\pip.exe" (
    "%SCRIPT_DIR%.venv\Scripts\pip.exe" install --quiet "PyYAML==5.4.1" >nul 2>&1
    set "PY_CMD=%SCRIPT_DIR%.venv\Scripts\python.exe"
    set "USE_VENV=1"
  )
)

REM Set PYTHONPATH to include bundled pydeps
set "PYTHONPATH=%SCRIPT_DIR%pydeps;!PYTHONPATH!"

:run
if "%~1"=="" (
  echo python_wrapper.cmd: missing script path argument 1>&2
  exit /b 2
)

%PY_CMD% %*
exit /b %ERRORLEVEL%
