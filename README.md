# AMB82-MINI Vision Hub & Distributed Robotic Car Platform

> **Platform:** Realtek AMB82-MINI, ESP32, ESP32-C3  
> **Status:** `v1.2 — Ultra-low latency ESP-NOW Mecanum Car + AI Vision Hub`

---

## 📌 Project Overview

This repository contains the firmware for a **distributed, multi-node autonomous robotic car system**. The system is decoupled into three independent nodes communicating over Wi-Fi and ESP-NOW:
1. **Vision Hub (AMB82-MINI)**: A dedicated AI camera unit streaming live video over RTSP and running real-time object detection (YOLOv4-Tiny) using its on-chip NPU.
2. **Motor Control Node (ESP32)**: Receives low-latency ESP-NOW commands to independently drive 4 DC motors (via two TB6612FNG drivers) in an omnidirectional Mecanum drive configuration.
3. **Gesture Glove Controller (ESP32-C3)**: A wearable glove equipped with an MPU-6050 IMU that calculates Mecanum kinematics from hand tilt/twist and transmits them directly to the car via ESP-NOW.

### System Architecture

```text
┌─────────────────────────────────────────────────────────────┐
│              Distributed Robotic Car Platform               │
│                                                             │
│  ┌───────────────┐           ┌───────────────────────────┐  │
│  │ Gesture Glove │  ESP-NOW  │       ESP32 Car Node      │  │
│  │ (ESP32-C3 +   ├──────────►│ (ESP32 + 2x TB6612FNG +   │  │
│  │  MPU-6050)    │ Protocol  │  Mecanum Wheels)          │  │
│  └───────────────┘           └────────────┬──────────────┘  │
│                                           │                 │
│  ┌───────────────┐           ┌────────────▼──────────────┐  │
│  │  AMB82-MINI   │   WiFi    │ Viewer / Driver Dashboard │  │
│  │  Vision Hub   ├──────────►│ (VLC Player / Phone VR)   │  │
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
- **ESP-NOW Receiver**: Ultra-low latency, direct peer-to-peer connection with the glove.
- **Mecanum Omnidirectional Drive**: Controls 4 independent motors via dual TB6612FNG H-bridges allowing for strafing, diagonal movement, and zero-radius turns.
- **Safety Timeout**: Automatically halts all motors if the ESP-NOW connection drops for >500ms.

### 🧤 **Gesture Glove (ESP32-C3)**
- **MPU-6050 IMU**: Fuses accelerometer and gyroscope data using a complementary filter for stable pitch/roll/yaw calculation.
- **Mecanum Kinematics**:
  - **Pitch** (Tilt Forward/Back) = Forward / Reverse Drive
  - **Roll** (Tilt Left/Right) = Strafe Left / Right
  - **Yaw** (Twist Hand) = Tank Spin in place
- **Proportional Speed**: Motor speed scales dynamically based on the steepness of the hand tilt.

---

## 📁 Project Structure

```text
AMB82-MINI-Vision-Hub/
├── RTSP/
│   ├── RTSP.ino                  # AMB82-MINI Vision Hub firmware
│   └── ObjectClassList.h         # COCO 80-class lookup table
├── ESP32-Car/
│   ├── ESPNOW_Receiver/          
│   │   └── ESPNOW_Receiver.ino   # ESP32 ESP-NOW Mecanum Motor Controller
│   └── WebMotorControl/          # (Legacy) HTTP Web Dashboard Controller
├── Gesture_Glove/
│   └── Gesture_Glove.ino         # ESP32-C3 ESP-NOW IMU Glove Controller
├── 3D Files/                     # 3D printable parts for the robot chassis
├── .vscode/
│   └── ...                       # Editor configurations
└── README.md
```

---

## 🛠️ Hardware Requirements

- **Vision**: Realtek AMB82-MINI development board + camera module
- **Car**: ESP32 Dev Module (30-pin), 2x TB6612FNG motor drivers, 4x Mecanum wheels w/ DC Motors, Chassis.
- **Glove**: ESP32-C3 Super Mini, MPU-6050 GY-521 IMU module, 3.7V Battery.

---

## 🚀 Setup & Usage

### 1. Car Setup (ESP32)
1. Flash `ESP32-Car/ESPNOW_Receiver/ESPNOW_Receiver.ino` to the ESP32.
2. The ESP32 will boot and wait for incoming ESP-NOW packets on its MAC address.

### 2. Vision Hub (AMB82-MINI)
1. Flash `RTSP/RTSP.ino` to the AMB82-MINI.
2. Connect to its `RobotCar-Demo` Wi-Fi AP.
3. Open `rtsp://192.168.1.1:554/` in VLC Media Player to view the live object detection stream.

### 3. Gesture Glove (ESP32-C3)
1. Ensure the Car's MAC address is correctly hardcoded in `Gesture_Glove.ino`.
2. Flash `Gesture_Glove/Gesture_Glove.ino` to the ESP32-C3.
3. Put the glove on. Tilting and twisting your hand will instantly drive the Mecanum wheels.

---

## 🗺️ Roadmap

- [x] **v1.0** — Vision Hub: RTSP streaming + YOLOv4 object detection OSD
- [x] **v1.1** — Decentralized nodes: Car web controller + IMU Gesture Glove
- [x] **v1.2** — ESP-NOW upgrade: Ultra-low latency Mecanum drive kinematics
- [ ] **v2.0** — Full autonomous navigation loop (Vision Hub sending UDP drive commands)

---

## 📄 License

MIT © 2026 [NNavoda](https://github.com/NNavoda)
