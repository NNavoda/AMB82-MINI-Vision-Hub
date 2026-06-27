@echo off
setlocal EnableDelayedExpansion

set ESPTOOL=C:\Users\navod\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.2.0\esptool.exe
set SKETCH=C:\Users\navod\AppData\Local\arduino\sketches\CC08FC0736F4C7DD122D5B2C1A36DA26
set PARTS=C:\Users\navod\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.8\tools\partitions
set PORT=COM6

echo.
echo ==========================================
echo   ESP32-C3 -- Auto-Retry Flash Tool
echo ==========================================
echo.
echo Waiting for board in download mode on %PORT%...
echo.
echo  1. Hold BOOT button
echo  2. Tap RST button
echo  3. Release BOOT
echo  (Repeat if needed -- this will keep retrying)
echo.

:RETRY
"%ESPTOOL%" --chip esp32c3 --port %PORT% --baud 460800 --before no-reset --after hard-reset --connect-attempts 3 write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x0 "%SKETCH%\Gesture_Glove.ino.bootloader.bin" 0x8000 "%SKETCH%\Gesture_Glove.ino.partitions.bin" 0xe000 "%PARTS%\boot_app0.bin" 0x10000 "%SKETCH%\Gesture_Glove.ino.bin"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ==========================================
    echo   SUCCESS! Firmware flashed to %PORT%
    echo   Press RST to boot the sketch.
    echo ==========================================
    pause
    exit /b 0
)

echo.
echo Connection failed. Do BOOT+RST again and retrying in 3 seconds...
timeout /t 3 /nobreak >nul
goto RETRY
