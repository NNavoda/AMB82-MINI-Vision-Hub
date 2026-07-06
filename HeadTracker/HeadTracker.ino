/**
 * ============================================================
 *  HEAD TRACKER - ESP-NOW Sender
 *  Hardware : ESP32-C3 Super Mini + MPU-9250
 *  Role     : Reads pitch/roll via complementary filter and
 *             yaw via tilt-compensated AK8963 magnetometer.
 *             Sends HeadTrackerData to Car ESP32 via ESP-NOW.
 *  Car MAC  : 30:76:F5:E7:E6:68
 * ============================================================
 */

#include <Wire.h>
#include <esp_now.h>
#include <WiFi.h>
#include <math.h>

// ============================================================
//  PIN DEFINITIONS  (ESP32-C3 Super Mini)
// ============================================================
#define I2C_SDA  6
#define I2C_SCL  7
#define LED_PIN  8

// ============================================================
//  MPU-9250 / AK8963 REGISTER MAP
// ============================================================
#define MPU_ADDR         0x68
#define AK8963_ADDR      0x0C

#define REG_PWR_MGMT_1   0x6B
#define REG_ACCEL_CFG    0x1C
#define REG_GYRO_CFG     0x1B
#define REG_INT_PIN_CFG  0x37
#define REG_ACCEL_XOUT   0x3B

#define AK_WIA           0x00
#define AK_CNTL1         0x0A
#define AK_XOUT_L        0x03
#define AK_ASA_X         0x10
#define AK_ST1           0x02

// ============================================================
//  TUNING
// ============================================================
const float ACCEL_SCALE      = 16384.0f;
const float GYRO_SCALE       = 131.0f;
const float MAG_SCALE        = 0.6f;
const float COMP_ALPHA       = 0.97f;
const int   SEND_INTERVAL_MS = 20;
const float MAG_DECLINATION  = 0.5f;

// ============================================================
//  ESP-NOW - Car MAC Address
// ============================================================
uint8_t carMacAddress[] = {0x30, 0x76, 0xF5, 0xE7, 0xE6, 0x68};
esp_now_peer_info_t peerInfo;

// ============================================================
//  DATA STRUCTURES  (defined before any function uses them)
// ============================================================
struct ImuData {
  float ax, ay, az;
  float gx, gy, gz;
};

typedef struct HeadTrackerData {
  float pitch;
  float yaw;
  float roll;
} HeadTrackerData;

// ============================================================
//  GLOBAL STATE
// ============================================================
HeadTrackerData headData = {0.0f, 0.0f, 0.0f};
float pitch = 0.0f, roll = 0.0f, yaw = 0.0f;
unsigned long lastUpdateUs = 0;
unsigned long lastSendMs   = 0;
unsigned long lastLedMs    = 0;
bool ledState = false;
bool magOK    = false;

float magAdjX = 1.0f, magAdjY = 1.0f, magAdjZ = 1.0f;
float magOffsetX = 0.0f, magOffsetY = 0.0f, magOffsetZ = 0.0f;

// ============================================================
//  I2C HELPERS
// ============================================================
void writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)addr, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

bool readBytes(uint8_t addr, uint8_t reg, uint8_t count, uint8_t* buf) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, count) != count) return false;
  for (int i = 0; i < count; i++) buf[i] = Wire.read();
  return true;
}

// ============================================================
//  MPU-9250 INIT
// ============================================================
bool initMPU9250() {
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("MPU-9250 not found at 0x68!");
    return false;
  }
  writeReg(MPU_ADDR, REG_PWR_MGMT_1, 0x00); delay(100);
  writeReg(MPU_ADDR, REG_PWR_MGMT_1, 0x01); delay(50);
  writeReg(MPU_ADDR, REG_ACCEL_CFG,  0x00);
  writeReg(MPU_ADDR, REG_GYRO_CFG,   0x00);
  writeReg(MPU_ADDR, REG_INT_PIN_CFG, 0x02); delay(10);
  return true;
}

// ============================================================
//  AK8963 INIT
// ============================================================
bool initAK8963() {
  uint8_t wia = readReg(AK8963_ADDR, AK_WIA);
  if (wia != 0x48) {
    Serial.printf("AK8963 WHO_AM_I=0x%02X (expected 0x48)\n", wia);
    return false;
  }
  writeReg(AK8963_ADDR, AK_CNTL1, 0x00); delay(10);
  writeReg(AK8963_ADDR, AK_CNTL1, 0x0F); delay(10);
  uint8_t asa[3];
  readBytes(AK8963_ADDR, AK_ASA_X, 3, asa);
  magAdjX = (asa[0] - 128.0f) / 256.0f + 1.0f;
  magAdjY = (asa[1] - 128.0f) / 256.0f + 1.0f;
  magAdjZ = (asa[2] - 128.0f) / 256.0f + 1.0f;
  Serial.printf("Mag adj: X=%.3f Y=%.3f Z=%.3f\n", magAdjX, magAdjY, magAdjZ);
  writeReg(AK8963_ADDR, AK_CNTL1, 0x00); delay(10);
  writeReg(AK8963_ADDR, AK_CNTL1, 0x16); delay(10);
  return true;
}

// ============================================================
//  IMU READ
// ============================================================
bool readIMU(ImuData& d) {
  uint8_t buf[14];
  if (!readBytes(MPU_ADDR, REG_ACCEL_XOUT, 14, buf)) return false;
  d.ax = (int16_t)(buf[0]  << 8 | buf[1])  / ACCEL_SCALE;
  d.ay = (int16_t)(buf[2]  << 8 | buf[3])  / ACCEL_SCALE;
  d.az = (int16_t)(buf[4]  << 8 | buf[5])  / ACCEL_SCALE;
  d.gx = (int16_t)(buf[8]  << 8 | buf[9])  / GYRO_SCALE;
  d.gy = (int16_t)(buf[10] << 8 | buf[11]) / GYRO_SCALE;
  d.gz = (int16_t)(buf[12] << 8 | buf[13]) / GYRO_SCALE;
  return true;
}

// ============================================================
//  MAGNETOMETER READ
// ============================================================
bool readMag(float& mx, float& my, float& mz) {
  uint8_t st1 = readReg(AK8963_ADDR, AK_ST1);
  if (!(st1 & 0x01)) return false;
  uint8_t buf[7];
  if (!readBytes(AK8963_ADDR, AK_XOUT_L, 7, buf)) return false;
  if (buf[6] & 0x08) return false;
  mx = (int16_t)(buf[1] << 8 | buf[0]) * magAdjX * MAG_SCALE - magOffsetX;
  my = (int16_t)(buf[3] << 8 | buf[2]) * magAdjY * MAG_SCALE - magOffsetY;
  mz = (int16_t)(buf[5] << 8 | buf[4]) * magAdjZ * MAG_SCALE - magOffsetZ;
  return true;
}

// ============================================================
//  ANGLE UPDATE (Complementary Filter + Gyro Yaw)
// ============================================================
void updateAngles(const ImuData& d, float dt) {
  float pitchAcc = atan2f(-d.ax, sqrtf(d.ay*d.ay + d.az*d.az)) * RAD_TO_DEG;
  float rollAcc  = atan2f( d.ay, d.az) * RAD_TO_DEG;
  pitch = COMP_ALPHA * (pitch + d.gy * dt) + (1.0f - COMP_ALPHA) * pitchAcc;
  roll  = COMP_ALPHA * (roll  + d.gx * dt) + (1.0f - COMP_ALPHA) * rollAcc;

  // Integrate gyro Z for yaw — works without magnetometer
  // Negate gz so turning right = positive yaw (pan right)
  yaw += -d.gz * dt;
  if (yaw <   0.0f) yaw += 360.0f;
  if (yaw > 360.0f) yaw -= 360.0f;
}

// ============================================================
//  MAGNETOMETER YAW CORRECTION (optional slow drift correction)
// ============================================================
void updateYaw(float mx, float my, float mz) {
  // Tilt-compensate the mag vector
  float cp = cosf(pitch * DEG_TO_RAD), sp = sinf(pitch * DEG_TO_RAD);
  float cr = cosf(roll  * DEG_TO_RAD), sr = sinf(roll  * DEG_TO_RAD);
  float mxH =  mx * cp + mz * sp;
  float myH = -mx * sr * sp + my * cr - mz * sr * cp;
  float magYaw = atan2f(-myH, mxH) * RAD_TO_DEG + MAG_DECLINATION;
  if (magYaw <   0.0f) magYaw += 360.0f;
  if (magYaw > 360.0f) magYaw -= 360.0f;

  // Gently pull gyro yaw toward magnetometer heading to prevent drift
  // (small correction factor so mag noise doesn't cause jitter)
  float err = magYaw - yaw;
  if (err >  180.0f) err -= 360.0f;
  if (err < -180.0f) err += 360.0f;
  yaw += 0.02f * err;  // 2% mag blend per update
  if (yaw <   0.0f) yaw += 360.0f;
  if (yaw > 360.0f) yaw -= 360.0f;
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  delay(1000);

  Serial.println("\n=== Head Tracker Unit Starting ===");

  WiFi.mode(WIFI_STA);
  Serial.print("Head Tracker MAC: ");
  Serial.println(WiFi.macAddress());

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (!initMPU9250()) {
    Serial.println("FATAL: MPU-9250 init failed! Check wiring.");
    while (1) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(200); }
  }
  Serial.println("MPU-9250 accel/gyro: OK");

  magOK = initAK8963();
  Serial.println(magOK ? "AK8963 mag: OK  (full pitch+yaw)" : "AK8963 NOT found (pitch/tilt only)");

  if (esp_now_init() != ESP_OK) {
    Serial.println("FATAL: ESP-NOW init failed!");
    while (1) { delay(100); }
  }

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, carMacAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("FATAL: Failed to add car as peer!");
    while (1) { delay(100); }
  }

  Serial.printf("Targeting Car MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
    carMacAddress[0], carMacAddress[1], carMacAddress[2],
    carMacAddress[3], carMacAddress[4], carMacAddress[5]);
  Serial.println("Sending head angles at 50Hz...\n");

  lastUpdateUs = micros();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  unsigned long nowUs = micros();
  float dt = (nowUs - lastUpdateUs) / 1000000.0f;
  if (dt > 0.05f) dt = 0.05f;
  lastUpdateUs = nowUs;

  ImuData d;
  if (readIMU(d)) {
    updateAngles(d, dt);
  }

  if (magOK) {
    float mx, my, mz;
    if (readMag(mx, my, mz)) updateYaw(mx, my, mz);
  }

  unsigned long nowMs = millis();
  if (nowMs - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = nowMs;
    headData.pitch = pitch;
    headData.yaw   = yaw;
    headData.roll  = roll;
    esp_now_send(carMacAddress, (uint8_t*)&headData, sizeof(headData));

    static unsigned long lastPrint = 0;
    if (nowMs - lastPrint >= 500) {
      lastPrint = nowMs;
      Serial.printf("Pitch:%6.1f  Roll:%6.1f  Yaw:%6.1f\n", pitch, roll, yaw);
    }
    if (nowMs - lastLedMs >= 200) {
      lastLedMs = nowMs;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
    }
  }

  long waitUs = 5000L - (long)(micros() - nowUs);
  if (waitUs > 0) delayMicroseconds(waitUs);
}