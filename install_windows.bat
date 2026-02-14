@echo off
setlocal EnableExtensions

call "%~dp0tools\install\windows_prereqs.bat" %*
exit /b %ERRORLEVEL%
