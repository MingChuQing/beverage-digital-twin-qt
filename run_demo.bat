@echo off
setlocal
if exist "%~dp0dist\BeverageTwinDemo.exe" (
  start "" "%~dp0dist\BeverageTwinDemo.exe" --autoplay
  exit /b 0
)
echo Please run build_demo.ps1 -Deploy first, or open beverage_twin_demo.pro in Qt Creator.
pause
