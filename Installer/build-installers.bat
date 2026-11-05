@echo off
title Build plugin installers
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-installers.ps1"
echo.
pause
