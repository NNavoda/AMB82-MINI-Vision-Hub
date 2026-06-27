/**
 * ============================================================
 *  GESTURE GLOVE CONTROLLER — ESP-NOW Mecanum v1.1
 *  Hardware : ESP32-C3 Super Mini + MPU-6050
 *  Role     : Reads IMU data, calculates Mecanum kinematics,
 *             and sends motor commands directly to the car 
 *             via low-latency ESP-NOW protocol.
 * ============================================================
 */

#include <Wire.h>
#include <esp_now.h>
#include <WiFi.h>

// ------------------------------------------------------------
//  HARDWARE & TUNING
// ------------------------------------------------------------
#define I2C_SDA 6
#define I2C_SCL 7
#define LED_PIN 8

const float COMP_ALPHA    = 0.98f;
const float DEAD_ZONE_DEG = 12.0f;
const float MAX_ANGLE_DEG = 45.0f;
const float GYRO_DEAD_Z   = 30.0f; // deg/sec
const float GYRO_MAX_Z    = 150.0f;

const int MIN_SPEED = 80;
const int MAX_SPEED = 220;

const int MPU_ADDR = 0x68;
const float ACCEL_SCALE = 16384.0f;
const float GYRO_SCALE  = 131.0f;

// ------------------------------------------------------------
//  ESP-NOW SETUP
// ------------------------------------------------------------
// Car's MAC Address: 30:76:f5:e7:e6:68
uint8_t carMacAddress[] = {0x30, 0x76, 0xF5, 0xE7, 0xE6, 0x68};

esp_now_peer_info_t peerInfo;

typedef struct CarCommand {
    int16_t fl;
    int16_t fr;
    int16_t rl;
    int16_t rr;
    float pitch;
    float roll;
    float yaw_rate;
} CarCommand;

CarCommand cmd;

// ------------------------------------------------------------
//  STATE
// ------------------------------------------------------------
struct ImuData { float ax, ay, az, gx, gy, gz; };

float pitch = 0, roll = 0;
unsigned long lastUpdate = 0;
unsigned long lastSend = 0;
unsigned long lastLedToggle = 0;
bool ledState = false;

// ------------------------------------------------------------
//  MPU-6050
// ------------------------------------------------------------
void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool initMPU() {
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0) return false;
  writeReg(0x6B, 0x00); // Wake up
  writeReg(0x1C, 0x00); // Accel config: ±2g
  writeReg(0x1B, 0x00); // Gyro config: ±250°/s
  return true;
}

bool readIMU(ImuData& d) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  
  if (Wire.requestFrom(MPU_ADDR, 14) != 14) return false;
  uint8_t buf[14];
  for (int i=0; i<14; i++) buf[i] = Wire.read();

  d.ax = (int16_t)(buf[0]  << 8 | buf[1])  / ACCEL_SCALE;
  d.ay = (int16_t)(buf[2]  << 8 | buf[3])  / ACCEL_SCALE;
  d.az = (int16_t)(buf[4]  << 8 | buf[5])  / ACCEL_SCALE;
  d.gx = (int16_t)(buf[8]  << 8 | buf[9])  / GYRO_SCALE;
  d.gy = (int16_t)(buf[10] << 8 | buf[11]) / GYRO_SCALE;
  d.gz = (int16_t)(buf[12] << 8 | buf[13]) / GYRO_SCALE;
  return true;
}

void updateAngles(const ImuData& d, float dt) {
  float pa = atan2f(-d.ax, sqrtf(d.ay*d.ay + d.az*d.az)) * RAD_TO_DEG;
  float ra = atan2f( d.ay, d.az) * RAD_TO_DEG;
  pitch = COMP_ALPHA * (pitch + d.gy * dt) + (1.0f - COMP_ALPHA) * pa;
  roll  = COMP_ALPHA * (roll  + d.gx * dt) + (1.0f - COMP_ALPHA) * ra;
}

// ------------------------------------------------------------
//  KINEMATICS
// ------------------------------------------------------------
int getSignedSpeed(float value, float deadZone, float maxValue) {
  if (fabsf(value) < deadZone) return 0;
  float mag = fabsf(value) - deadZone;
  float frac = constrain(mag / (maxValue - deadZone), 0.0f, 1.0f);
  int speed = (int)(MIN_SPEED + frac * (MAX_SPEED - MIN_SPEED));
  return (value > 0) ? speed : -speed;
}

void calculateMecanum(float p, float r, float yaw_rate) {
  // Pitch < 0 means tilt forward -> Positive Y
  int Y = -getSignedSpeed(p, DEAD_ZONE_DEG, MAX_ANGLE_DEG);
  
  // Roll > 0 means tilt right -> Positive X
  int X = getSignedSpeed(r, DEAD_ZONE_DEG, MAX_ANGLE_DEG);
  
  // Yaw Rate > 0 means twist right -> Positive Z
  int Z = getSignedSpeed(yaw_rate, GYRO_DEAD_Z, GYRO_MAX_Z);

  // Mecanum mixing equations
  int fl = Y + X + Z;
  int fr = Y - X - Z;
  int rl = Y - X + Z;
  int rr = Y + X - Z;

  // Normalize speeds to keep them within PWM limits
  int max_spd = max(max(abs(fl), abs(fr)), max(abs(rl), abs(rr)));
  if (max_spd > MAX_SPEED) {
    fl = fl * MAX_SPEED / max_spd;
    fr = fr * MAX_SPEED / max_spd;
    rl = rl * MAX_SPEED / max_spd;
    rr = rr * MAX_SPEED / max_spd;
  }
  
  // Clamp very low values below MIN_SPEED back to 0
  if (abs(fl) < MIN_SPEED) fl = 0;
  if (abs(fr) < MIN_SPEED) fr = 0;
  if (abs(rl) < MIN_SPEED) rl = 0;
  if (abs(rr) < MIN_SPEED) rr = 0;

  cmd.fl = fl;
  cmd.fr = fr;
  cmd.rl = rl;
  cmd.rr = rr;
  cmd.pitch = p;
  cmd.roll = r;
  cmd.yaw_rate = yaw_rate;
}

// ------------------------------------------------------------
//  MAIN
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  delay(1000);
  Serial.println("\n=== Gesture Glove: ESP-NOW Mecanum ===");

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!initMPU()) {
    Serial.println("MPU-6050 NOT FOUND!");
    while (1) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(100); }
  }
  Serial.println("MPU-6050 Init OK");

  // Init Wi-Fi and ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    while(1) { delay(100); }
  }
  
  // Register peer
  memcpy(peerInfo.peer_addr, carMacAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    while(1) { delay(100); }
  }
  
  Serial.println("ESP-NOW Ready. Sending commands to Car.");
  lastUpdate = micros();
}

void loop() {
  unsigned long now = micros();
  float dt = (now - lastUpdate) / 1000000.0f;
  lastUpdate = now;

  ImuData d;
  if (readIMU(d)) {
    updateAngles(d, dt);
    
    // Calculate and update global 'cmd' struct
    calculateMecanum(pitch, roll, d.gz);
    
    // Send data at 50Hz (every 20ms)
    if (millis() - lastSend >= 20) {
      esp_now_send(carMacAddress, (uint8_t *) &cmd, sizeof(cmd));
      lastSend = millis();
      
      // Blink LED faster if we're moving
      bool isMoving = (cmd.fl != 0 || cmd.fr != 0 || cmd.rl != 0 || cmd.rr != 0);
      unsigned long blinkRate = isMoving ? 100 : 1000;
      if (millis() - lastLedToggle >= blinkRate) {
        lastLedToggle = millis();
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
      }
    }
  } else {
    Serial.println("I2C Error");
  }

  // Sleep remainder of ~5ms loop (200Hz sampling for IMU)
  long wait = 5000 - (micros() - now);
  if (wait > 0) delayMicroseconds(wait);
}
