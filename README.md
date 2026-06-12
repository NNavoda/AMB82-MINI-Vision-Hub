# AMB82-MINI Vision Hub — Autonomous Robotic Car Platform

> **Node Role:** Isolated Vision Hub · No motor or actuator logic  
> **Platform:** Realtek AMB82-MINI (RTL8735B · Arm v8M @ 500 MHz · NPU)  
> **Status:** `v1.0 — Open Day Demo`

---

## 📌 Project Overview

This repository contains the firmware for the **Vision Hub node** of a distributed, multi-node autonomous robotic car system. The AMB82-MINI acts as a dedicated AI camera unit — it streams live video over RTSP and runs real-time object detection using its on-chip Neural Processing Unit (NPU), completely independent of the drive/control subsystem.

### System Architecture

```
┌─────────────────────────────────────────────────────────┐
│              Distributed Robotic Car Platform            │
│                                                         │
│   ┌──────────────────┐        ┌──────────────────────┐  │
│   │  AMB82-MINI      │  WiFi  │  Control Node(s)     │  │
│   │  Vision Hub      │◄──────►│  (ESP32 / RPi etc.)  │  │
│   │  [This Repo]     │  AP    │                      │  │
│   └──────────────────┘        └──────────────────────┘  │
│           │                                             │
│     ┌─────▼──────┐                                      │
│     │ Viewer     │  rtsp://192.168.1.1:554/             │
│     │ (VLC etc.) │                                      │
│     └────────────┘                                      │
└─────────────────────────────────────────────────────────┘
```

### On-Device Pipeline

```
Camera ISP
  │
  ├── Channel 0 (H.264 VGA 640×480 @ 20fps)
  │       └── StreamIO ──► RTSP Server ──► Network Clients
  │                ▲
  │           OSD Overlay (bounding boxes drawn pre-encoding)
  │
  └── Channel 3 (Raw RGB 416×416 @ 20fps)
          └── StreamIO ──► YOLOv4-Tiny NPU ──► onDetectionResult()
```

---

## ✨ Features

| Feature | Detail |
|---------|--------|
| 📡 **Wi-Fi SoftAP** | SSID `RobotCar-Demo` · Channel 1 · No ESP-NOW interference |
| 🎥 **RTSP Stream** | H.264 · VGA (640×480) · 20fps · 1 Mbps · Low-latency GOP=10 |
| 🤖 **Object Detection** | YOLOv4-Tiny · INT8 NPU · COCO 80 classes · Up to 14 objects/frame |
| 🟩 **Live OSD Overlay** | Bounding boxes + class labels composited before H.264 encoding |
| ⚡ **Hardware Offload** | All video, encoding, and inference via DMA + hardware interrupts |

---

## 🛠️ Hardware Requirements

- **[Realtek AMB82-MINI](https://www.amebaiot.com/en/amebapro2-arduino-peripherals-examples/)** development board
- USB-C cable (power + programming)
- Camera module (onboard / compatible)
- Wi-Fi client device (phone, laptop) for viewing the stream

---

## 💻 Software Requirements

| Tool | Version | Purpose |
|------|---------|---------|
| [Arduino CLI](https://arduino.cc/en/software) | ≥ 1.5.x | Compile + Upload |
| Realtek AmebaPro2 SDK | 4.1.0 | Board support package |
| VS Code / Antigravity IDE | Any | Code editor |
| VLC Media Player | Any | RTSP stream viewer |

### Board Manager URL (add to Arduino CLI config)
```
https://github.com/Ameba-AIoT/ameba-arduino-pro2/raw/main/Arduino_package/package_realtek_amebapro2_index.json
```

---

## 🚀 Setup & Upload

### 1. Install Arduino CLI & Board Package
```powershell
# Download arduino-cli.exe to C:\Arduino\ then:
arduino-cli config add board_manager.additional_urls "https://github.com/Ameba-AIoT/ameba-arduino-pro2/raw/main/Arduino_package/package_realtek_amebapro2_index.json"
arduino-cli core update-index
arduino-cli core install realtek:AmebaPro2
```

### 2. Clone this repository
```bash
git clone https://github.com/NNavoda/AMB82-MINI-Vision-Hub.git
cd AMB82-MINI-Vision-Hub
```

### 3. Enter Boot Mode on AMB82-MINI
1. **Hold** the `BOOT` button
2. **Press + release** the `RESET` button
3. **Release** `BOOT`

### 4. Upload firmware
```powershell
arduino-cli upload -p COM5 --fqbn realtek:AmebaPro2:Ameba_AMB82-MINI "RTSP\RTSP.ino"
```
> Replace `COM5` with your actual port (check Device Manager → Ports)

---

## 📺 Viewing the Stream

1. Connect to Wi-Fi: **`RobotCar-Demo`** / password: **`openday2025`**
2. Open **VLC** → Media → Open Network Stream:
   ```
   rtsp://192.168.1.1:554/
   ```
3. For lower latency, set VLC network cache to **100ms**:  
   `Tools → Preferences → Input/Codecs → Network caching = 100`

---

## 🧠 Object Detection Model

| Parameter | Value |
|-----------|-------|
| Model | YOLOv4-Tiny (INT8 quantised) |
| Input resolution | 416 × 416 |
| Dataset | COCO (80 classes) |
| Confidence threshold | 0.30 |
| NMS threshold | 0.45 |
| Max detections/frame | 14 |

**Detectable classes include:** person · bicycle · car · motorbike · bus · truck · bottle · chair · laptop · phone · and 70 more.

---

## 📁 Project Structure

```
AMB82-MINI-Vision-Hub/
├── RTSP/
│   ├── RTSP.ino            # Main sketch — Vision Hub firmware
│   └── ObjectClassList.h   # COCO 80-class lookup table
├── .vscode/
│   ├── arduino.json        # Board & port configuration
│   ├── tasks.json          # One-click compile/upload tasks
│   ├── settings.json       # Editor settings
│   └── keybindings.json    # Arduino IDE-style shortcuts
├── .gitignore
├── LICENSE                 # MIT
└── README.md
```

---

## 🗺️ Roadmap

This is the first milestone of a larger autonomous driving project:

- [x] **v1.0** — Vision Hub: RTSP streaming + YOLOv4 object detection OSD
- [ ] **v1.1** — Transmit detection results over UDP to control node
- [ ] **v1.2** — Lane detection integration
- [ ] **v2.0** — Full autonomous navigation loop

---

## 📄 License

MIT © 2026 [NNavoda](https://github.com/NNavoda)
