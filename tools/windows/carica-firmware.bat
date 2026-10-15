@echo off
rem Doppio clic qui per compilare e caricare il firmware sull'ESP32.
rem Passa a PowerShell aggirando i criteri di esecuzione, che di default
rem bloccano i .ps1 avviati con un doppio clic.
chcp 65001 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0carica-firmware.ps1" %*
echo.
pause
