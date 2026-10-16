@echo off
rem Doppio clic qui per scaricare le novita' e ricaricare il firmware.
chcp 65001 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0aggiorna.ps1" %*
echo.
pause
