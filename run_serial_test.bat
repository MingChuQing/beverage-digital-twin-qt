@echo off
setlocal
set "twinExe=%~dp0dist\BeverageTwinDemo.exe"
if not exist "%twinExe%" set "twinExe=%~dp0BeverageTwinDemo.exe"
if not exist "%twinExe%" (
  echo Build with build_demo.ps1 -Deploy first.
  pause
  exit /b 1
)
set "receiverPort=COM10"
set "senderPort=COM3"
if not "%~1"=="" set "receiverPort=%~1"
if not "%~2"=="" set "senderPort=%~2"
if /I "%receiverPort%"=="%senderPort%" (
  echo Receiver and sender must use different serial ports.
  pause
  exit /b 1
)
start "" "%twinExe%" --port "%receiverPort%" --baud 115200
timeout /t 1 /nobreak >nul
start "" "%twinExe%" --sender --extended --flavor 2 --format 2 --port "%senderPort%" --baud 115200
