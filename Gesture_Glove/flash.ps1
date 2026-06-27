$esptool  = "C:\Users\navod\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.2.0\esptool.exe"
$sketch   = "C:\Users\navod\AppData\Local\arduino\sketches\CC08FC0736F4C7DD122D5B2C1A36DA26"
$parts    = "C:\Users\navod\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.8\tools\partitions"

Write-Host ""
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  ESP32-C3 Super Mini — Flash Tool" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "ACTION REQUIRED:" -ForegroundColor Yellow
Write-Host "  While esptool says 'Connecting......'" -ForegroundColor Yellow
Write-Host "  ===> Hold BOOT, press RST, release BOOT <===" -ForegroundColor Yellow
Write-Host ""
Write-Host "Starting esptool now..." -ForegroundColor Green
Write-Host ""

& $esptool `
    --chip esp32c3 `
    --port COM6 `
    --baud 921600 `
    --before default-reset `
    --after hard-reset `
    write-flash -z `
    --flash-mode keep `
    --flash-freq keep `
    --flash-size keep `
    0x0     "$sketch\Gesture_Glove.ino.bootloader.bin" `
    0x8000  "$sketch\Gesture_Glove.ino.partitions.bin" `
    0xe000  "$parts\boot_app0.bin" `
    0x10000 "$sketch\Gesture_Glove.ino.bin"

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "SUCCESS! Firmware flashed." -ForegroundColor Green
    Write-Host "Press RST once more to boot into the sketch." -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "FAILED. Try again — do BOOT+RST while 'Connecting......' is shown." -ForegroundColor Red
}
