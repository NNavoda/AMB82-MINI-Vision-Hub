# ESP32-C3 Super Mini — Board Setup & Driver Fix Guide

## Problem: Board not appearing in Device Manager

The ESP32-C3 Super Mini uses a **USB-CDC on-chip** interface (no external USB-UART chip like CH340 or CP2102).
Windows requires a specific driver + a boot trick to enumerate it.

---

## Step 1 — Force the board into Download/Bootloader Mode

The board will **not** appear in Device Manager unless it is in Download Mode.

| Step | Action |
|------|--------|
| 1 | Hold down the **BOOT** button (label: `BOOT` or `IO9`) |
| 2 | While holding BOOT, press and release the **RESET** button (label: `RST` or `EN`) |
| 3 | Release the **BOOT** button |
| 4 | Open **Device Manager** — it should now appear as `USB Serial Device (COMx)` or `Unknown Device` |

> If it appears as **Unknown Device**, proceed to Step 2.

---

## Step 2 — Install the ESP32-C3 USB-CDC Driver (Windows)

### Option A — Automatic via Zadig (Recommended for first-time)

1. Download **Zadig** from https://zadig.akeo.ie/
2. Open Zadig → Options → `List All Devices`
3. Find **`USB JTAG/serial debug unit`** or **`ESP32-C3`** in the dropdown
4. Set the driver to **`usbser (USB Serial)`** or **`WinUSB`**
5. Click **Install Driver** / **Replace Driver**

### Option B — Windows Update Driver Manually

1. Device Manager → right-click the Unknown Device → **Update driver**
2. Choose **Browse my computer** → **Let me pick from a list**
3. Select **Ports (COM & LPT)** → **USB Serial Device**

### Option C — Install ESP-IDF USB Driver package

Download and run the official driver installer:
```
https://dl.espressif.com/dl/idf-driver/idf-driver-esp32-usb-jtag-2021-07-15.zip
```
Extract and run `install.bat` as Administrator.

---

## Step 3 — Install the ESP32 Board Package in Arduino IDE

1. Open Arduino IDE → **File → Preferences**
2. In **"Additional boards manager URLs"** add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Boards Manager**
4. Search `esp32` → install **"esp32 by Espressif Systems"** (v2.0.17+ or v3.x)

---

## Step 4 — Arduino IDE Board & Port Settings

| Setting | Value |
|---------|-------|
| **Board** | `ESP32C3 Dev Module` |
| **USB CDC On Boot** | `Enabled` ← **CRITICAL** |
| **Upload Speed** | `921600` |
| **CPU Frequency** | `160MHz` |
| **Flash Mode** | `DIO` |
| **Flash Size** | `4MB (32Mb)` |
| **Partition Scheme** | `Default 4MB with spiffs` |
| **Core Debug Level** | `None` |
| **Erase All Flash** | `Disabled` |
| **Port** | Select the COM port that appeared |

> **USB CDC On Boot → Enabled** is mandatory. Without it, `Serial.begin()` outputs to the UART0 pins,
> not the USB port, and you will see nothing in the Serial Monitor.

---

## Step 5 — Uploading the Sketch

1. Hold **BOOT**, press **RESET**, release **BOOT** (enter Download Mode)
2. Click **Upload** in Arduino IDE
3. When you see `Connecting........_____` stop holding BOOT
4. After upload finishes, press **RESET** once to run the firmware
5. Open Serial Monitor at **115200 baud**

---

## Expected Serial Output

```
==============================================
  Gesture Glove — ESP32-C3 Super Mini BOOT
==============================================
  LED_PIN  : GPIO 8  configured as OUTPUT
  Connecting to SSID : "Redmi Note 10 Pro"
  Entering main loop (non-blocking)…
----------------------------------------------

  ✓  Wi-Fi CONNECTED!
     SSID       : Redmi Note 10 Pro
     IP Address : 192.168.x.x
     Gateway    : 192.168.x.1
     RSSI       : -45 dBm
     Channel    : 6
----------------------------------------------
  LED → FAST BLINK (4 Hz) — handshake OK
```

---

## LED Behaviour Summary

| State | Pattern | Frequency |
|-------|---------|-----------|
| Connecting to Wi-Fi | Slow blink | 1 Hz (500ms ON / 500ms OFF) |
| Connected | Fast blink | 4 Hz (125ms ON / 125ms OFF) |
| Disconnected (after connection) | Slow blink + auto-reconnect | 1 Hz |

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Nothing in Device Manager | Hold BOOT → press RESET → release BOOT |
| `A fatal error occurred: Failed to connect` | Not in download mode — repeat BOOT+RESET sequence |
| Serial monitor blank | Set **USB CDC On Boot → Enabled** and re-upload |
| LED not blinking | Wrong LED_PIN — try `#define LED_PIN 8` or `2` depending on board revision |
| Keeps disconnecting | Move closer to AP, or check SSID/password in sketch |

---

## Next Phase: MPU-6050 Gesture Pipeline

```
SDA → GPIO 6
SCL → GPIO 7
VCC → 3.3V
GND → GND
```

Wire up the MPU-6050 and the I²C address defaults to `0x68`.
The next sketch will add the gesture classification layer.
