# AMB82-MINI Vision Hub & Distributed Robotic Car Platform

> **Platform:** Realtek AMB82-MINI, ESP32, ESP32-C3  
> **Status:** `v1.4 — ESP-NOW Mecanum Car + AI Vision Hub + Head Tracker`

---

## 📌 Project Overview

This repository contains the firmware for a **distributed, multi-node autonomous robotic car system**. The system is decoupled into multiple independent nodes communicating over Wi-Fi and ESP-NOW:
1. **Vision Hub (AMB82-MINI)**: A dedicated AI camera unit streaming live video over RTSP and running real-time object detection (YOLOv4-Tiny) using its on-chip NPU.
2. **Motor Control Node (ESP32)**: Receives low-latency ESP-NOW commands to independently drive 4 DC motors (via two TB6612FNG drivers) in an omnidirectional Mecanum drive configuration, and controls the camera Pan/Tilt servos.
3. **Gesture Glove Controller (ESP32-C3)**: A wearable glove equipped with an MPU-6050 IMU that calculates Mecanum kinematics from hand tilt/twist and transmits them directly to the car via ESP-NOW.
4. **Head Tracker Unit (ESP32-C3)**: A head-mounted IMU (MPU-9250) that calculates head Pitch and Yaw to seamlessly aim the Pan/Tilt Vision Hub camera in real time via ESP-NOW.

### System Architecture

```text
┌────────────────────────────────────────────────────────────────────────┐
│                   Distributed Robotic Car Platform                     │
│                                                                        │
│  ┌───────────────┐           ┌──────────────────────────────┐          │
│  │ Gesture Glove │  ESP-NOW  │        ESP32 Car Node        │          │
│  │ (ESP32-C3 +   ├──────────►│  (ESP32 + 2x TB6612FNG +     │          │
│  │  MPU-6050)    │ Protocol  │   Mecanum Wheels + Servos)   │          │
│  └───────────────┘           └────────────┬─────────────────┘          │
│                                           │                            │
│  ┌───────────────┐  ESP-NOW               │                            │
│  │ Head Tracker  ├──────────►             │                            │
│  │ (ESP32-C3 +   │                        │                            │
│  │  MPU-9250)    │           ┌────────────▼─────────────────┐          │
│  └───────────────┘           │  Viewer / Driver Dashboard   │          │
│                              │  (VLC Player / Phone VR)     │          │
│  ┌───────────────┐   WiFi    │                              │          │
│  │  AMB82-MINI   ├──────────►│                              │          │
│  │  Vision Hub   │ RTSP / AP │                              │          │
│  └───────────────┘           └──────────────────────────────┘          │
└────────────────────────────────────────────────────────────────────────┘
```

---

## ✨ Features

### 📡 **Vision Hub (AMB82-MINI)**
- **RTSP Stream**: H.264 · VGA (640×480) · 20fps · 1 Mbps · Low-latency GOP=10
- **Object Detection**: YOLOv4-Tiny · INT8 NPU · COCO 80 classes
- **Live OSD Overlay**: Bounding boxes + class labels composited before H.264 encoding

### 🚗 **Car Control Node (ESP32)**
- **ESP-NOW Receiver**: Ultra-low latency, direct peer-to-peer connection.
- **Mecanum Omnidirectional Drive**: Controls 4 independent motors via dual TB6612FNG H-bridges allowing for strafing, diagonal movement, and zero-radius turns.
- **Pan/Tilt Camera Gimbal**: Instantly receives angles from the head tracker and drives two servos with 16-bit PWM to smoothly aim the AMB82-MINI camera.
- **Safety Timeout**: Automatically halts all motors and centers servos if the ESP-NOW connection drops for >500ms.

### 🧤 **Gesture Glove (ESP32-C3)**
- **MPU-6050 IMU**: Fuses accelerometer and gyroscope data using a complementary filter for stable pitch/roll/yaw calculation.
- **Mecanum Kinematics**:
  - **Pitch** (Tilt Forward/Back) = Forward / Reverse Drive
  - **Roll** (Tilt Left/Right) = Strafe Left / Right
  - **Yaw** (Twist Hand) = Tank Spin in place

### 🥽 **Head Tracker Unit (ESP32-C3)**
- **MPU-9250 IMU**: Calculates true pitch/roll and integrates gyroscope Z-axis for responsive yaw mapping.
- **Zero-Drift Centering**: Remotely zeros the pan (yaw) heading simply by pressing the BOOT button on the Car Receiver.
- **Proportional Scaling**: Head movements are smoothly multiplied by a tuned sensitivity curve (Pitch x1.8, Yaw x1.5) to capture full camera range (40°–180°) effortlessly.

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
│   ├── HeadTracker_Receiver/     
│   │   └── HeadTracker_Receiver.ino # ESP32 ESP-NOW Servo Receiver (Latest)
│   ├── ESPNOW_ServoTracker/
│   │   └── ESPNOW_ServoTracker.ino # (Legacy) ESP-NOW Pan/Tilt Head Tracker
│   └── WebMotorControl/          # (Legacy) HTTP Web Dashboard Controller
├── Gesture_Glove/
│   └── Gesture_Glove.ino         # ESP32-C3 ESP-NOW IMU Glove Controller
├── HeadTracker/
│   └── HeadTracker.ino           # ESP32-C3 + MPU9250 Head Tracker Sender
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
- [x] **v1.3** — Servo Protocol: Preliminary Pan/Tilt absolute tracking
- [x] **v1.4** — Head Tracker Unit: Full 2-axis independent tracking with IMU MPU-9250
- [ ] **v2.0** — Full autonomous navigation loop (Vision Hub sending UDP drive commands)

---

## 📄 License

MIT © 2026 [NNavoda](https://github.com/NNavoda)
