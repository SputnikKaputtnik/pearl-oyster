@echo off
rem Double-click to install Oyster - Pearl on a Meta Quest connected by USB (see README.txt).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
pause
