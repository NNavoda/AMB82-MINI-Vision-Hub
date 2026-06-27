# AMB82-MINI Vision Hub & Distributed Robotic Car Platform

> **Platform:** Realtek AMB82-MINI, ESP32, ESP32-C3  
> **Status:** `v1.1 — Fully functional gesture-controlled car with AI Vision Hub`

---

## 📌 Project Overview

This repository contains the firmware for a **distributed, multi-node autonomous robotic car system**. The system is decoupled into three independent nodes communicating over Wi-Fi:
1. **Vision Hub (AMB82-MINI)**: A dedicated AI camera unit streaming live video over RTSP and running real-time object detection (YOLOv4-Tiny) using its on-chip NPU.
2. **Motor Control Node (ESP32)**: Connects to a Wi-Fi Access Point and hosts an HTTP web server to drive 4 DC motors (via two TB6612FNG drivers) and pan/tilt servos.
3. **Gesture Glove Controller (ESP32-C3)**: A wearable glove equipped with an MPU-6050 IMU that translates hand tilt into HTTP drive commands for the car.

### System Architecture

```text
┌─────────────────────────────────────────────────────────────┐
│              Distributed Robotic Car Platform               │
│                                                             │
│  ┌───────────────┐           ┌───────────────────────────┐  │
│  │ Gesture Glove │   WiFi    │       ESP32 Car Node      │  │
│  │ (ESP32-C3 +   ├──────────►│ (ESP32 + 2x TB6612FNG +   │  │
│  │  MPU-6050)    │ HTTP GET  │  Pan/Tilt Camera Servos)  │  │
│  └───────────────┘           └────────────┬──────────────┘  │
│                                           │                 │
│  ┌───────────────┐           ┌────────────▼──────────────┐  │
│  │  AMB82-MINI   │   WiFi    │ Viewer / Driver Dashboard │  │
│  │  Vision Hub   ├──────────►│ (VLC + Browser Interface) │  │
│  │  (AI Camera)  │ RTSP / AP │                           │  │
│  └───────────────┘           └───────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## ✨ Features

### 📡 **Vision Hub (AMB82-MINI)**
- **RTSP Stream**: H.264 · VGA (640×480) · 20fps · 1 Mbps · Low-latency GOP=10
- **Object Detection**: YOLOv4-Tiny · INT8 NPU · COCO 80 classes
- **Live OSD Overlay**: Bounding boxes + class labels composited before H.264 encoding

### 🚗 **Car Control Node (ESP32)**
- **Wi-Fi Server**: Connects to network and hosts a web dashboard for manual override control.
- **Omnidirectional Drive**: Controls 4 independent motors via dual TB6612FNG H-bridges.
- **Pan/Tilt Camera Mount**: Drives two MG996R servos for independent camera aiming.

### 🧤 **Gesture Glove (ESP32-C3)**
- **MPU-6050 IMU**: Fuses accelerometer and gyroscope data using a complementary filter for stable pitch/roll calculation.
- **Gesture Classification**: Translates hand tilts (Pitch/Roll) into Forward, Reverse, Left (Tank Turn), Right (Tank Turn), and Stop commands.
- **Proportional Speed**: Motor speed scales dynamically based on the steepness of the hand tilt.

---

## 📁 Project Structure

```text
AMB82-MINI-Vision-Hub/
├── RTSP/
│   ├── RTSP.ino                  # AMB82-MINI Vision Hub firmware
│   └── ObjectClassList.h         # COCO 80-class lookup table
├── ESP32-Car/
│   └── WebMotorControl/          
│       └── WebMotorControl.ino   # ESP32 Motor & Servo Web Controller
├── Gesture_Glove/
│   └── Gesture_Glove.ino         # ESP32-C3 MPU-6050 IMU Controller
├── 3D Files/                     # 3D printable parts for the robot chassis
├── .vscode/
│   └── ...                       # Editor configurations
└── README.md
```

---

## 🛠️ Hardware Requirements

- **Vision**: Realtek AMB82-MINI development board + camera module
- **Car**: ESP32 Dev Module (30-pin), 2x TB6612FNG motor drivers, 4x DC Motors, 2x MG996R Servos, Chassis.
- **Glove**: ESP32-C3 Super Mini, MPU-6050 GY-521 IMU module, 3.7V Battery.

---

## 🚀 Setup & Usage

### 1. Car Setup (ESP32)
1. Flash `ESP32-Car/WebMotorControl/WebMotorControl.ino` to the ESP32.
2. The ESP32 will connect to your specified Wi-Fi hotspot. Check the Serial Monitor for its IP address.
3. Open the IP address in a browser to access the manual slider dashboard.

### 2. Vision Hub (AMB82-MINI)
1. Flash `RTSP/RTSP.ino` to the AMB82-MINI.
2. Connect to its `RobotCar-Demo` Wi-Fi AP.
3. Open `rtsp://192.168.1.1:554/` in VLC Media Player to view the live object detection stream.

### 3. Gesture Glove (ESP32-C3)
1. Update the Car's IP address in `Gesture_Glove.ino`.
2. Flash `Gesture_Glove/Gesture_Glove.ino` to the ESP32-C3.
3. The glove will connect to the same Wi-Fi network and begin sending HTTP GET requests to the car as you tilt your hand.

---

## 🗺️ Roadmap

- [x] **v1.0** — Vision Hub: RTSP streaming + YOLOv4 object detection OSD
- [x] **v1.1** — Decentralized nodes: Car web controller + IMU Gesture Glove
- [ ] **v1.2** — Fast-batch HTTP endpoint for lower latency glove-to-car control
- [ ] **v2.0** — Full autonomous navigation loop (Vision Hub sending UDP drive commands)

---

## 📄 License

MIT © 2026 [NNavoda](https://github.com/NNavoda)
