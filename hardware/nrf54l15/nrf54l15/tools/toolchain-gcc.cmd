@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
where py >NUL 2>&1
if %ERRORLEVEL% EQU 0 (
  py -3 "%SCRIPT_DIR%toolchain_exec.py" --tool arm-zephyr-eabi-gcc -- %*
) else (
  python "%SCRIPT_DIR%toolchain_exec.py" --tool arm-zephyr-eabi-gcc -- %*
)
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%
