@echo off
setlocal
set "twinExe=%~dp0dist\BeverageTwinDemo.exe"
if not exist "%twinExe%" set "twinExe=%~dp0BeverageTwinDemo.exe"
if not exist "%twinExe%" (
  echo Build with build_demo.ps1 -Deploy first.
  pause
  exit /b 1
)
start "" "%twinExe%" --sender --extended --flavor 2 --format 2
