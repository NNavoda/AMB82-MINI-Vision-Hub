/**
 * ============================================================
 *  GESTURE GLOVE CONTROLLER — Full Firmware v1.0
 *  Hardware : ESP32-C3 Super Mini  +  MPU-6050
 *  Role     : Reads hand tilt via MPU-6050, classifies into
 *             directional gestures, sends HTTP commands to the
 *             ESP32 car node (WebMotorControl).
 * ============================================================
 *
 *  GESTURE MAP (pitch = forward/backward tilt,
 *               roll  = left/right tilt):
 *
 *    Pitch < -DEAD_ZONE   → FORWARD
 *    Pitch >  DEAD_ZONE   → REVERSE
 *    Roll  < -DEAD_ZONE   → LEFT  (tank spin)
 *    Roll  >  DEAD_ZONE   → RIGHT (tank spin)
 *    Flat (both in zone)  → STOP
 *
 *  SPEED MAPPING:
 *    Scales linearly from MIN_SPEED at threshold
 *    to MAX_SPEED at MAX_ANGLE_DEG.
 *
 *  CHECKLIST:
 *    [x] Wi-Fi Station Mode
 *    [x] MPU-6050 I²C gesture pipeline
 *    [x] Complementary filter (pitch / roll)
 *    [x] HTTP motor commands to car node
 *
 *  WIRING:
 *    MPU-6050 SDA → GPIO 6
 *    MPU-6050 SCL → GPIO 7
 *    MPU-6050 VCC → 3.3 V  (NOT 5V — GPIOs are 3.3 V!)
 *    MPU-6050 GND → GND
 *    MPU-6050 AD0 → GND    (I²C address = 0x68)
 *    MPU-6050 INT → NC
 * ============================================================
 */

#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ──────────────────────────────────────────────────────────────
//  USER CONFIG
// ──────────────────────────────────────────────────────────────

#define WIFI_SSID     "Redmi Note 10 Pro"
#define WIFI_PASSWORD "00000007"

// IP shown by car's Serial Monitor at boot — update after first connection
#define CAR_IP   "172.23.30.208"   // ← CHANGE THIS to your car's IP
#define CAR_PORT 80

// ──────────────────────────────────────────────────────────────
//  HARDWARE
// ──────────────────────────────────────────────────────────────

#define LED_PIN   8       // Onboard blue LED (active HIGH)
#define MPU_SDA   6       // I²C data  → SDA on MPU-6050
#define MPU_SCL   7       // I²C clock → SCL on MPU-6050
#define MPU_ADDR  0x68    // AD0 tied LOW → 0x68

// ──────────────────────────────────────────────────────────────
//  GESTURE TUNING — adjust to your hand/mount orientation
// ──────────────────────────────────────────────────────────────

#define DEAD_ZONE_DEG  12     // ±12° flat zone → STOP
#define MAX_ANGLE_DEG  45     // tilt at which speed hits MAX_SPEED
#define MIN_SPEED      80     // lowest PWM sent (keeps motors turning)
#define MAX_SPEED      220    // highest PWM sent (0-255 range)

// Complementary filter weight (0=all gyro, 1=all accel)
#define COMP_ALPHA     0.96f

// Loop timing
#define SAMPLE_MS      50     // IMU read period  (20 Hz)
#define HTTP_INTERVAL  80     // min ms between HTTP requests

// ──────────────────────────────────────────────────────────────
//  MPU-6050 REGISTERS
// ──────────────────────────────────────────────────────────────
#define MPU_PWR_MGMT_1   0x6B
#define MPU_CONFIG       0x1A
#define MPU_GYRO_CONFIG  0x1B
#define MPU_ACCEL_CONFIG 0x1C
#define MPU_ACCEL_XOUT_H 0x3B

#define ACCEL_SCALE 16384.0f   // LSB/g   at ±2 g
#define GYRO_SCALE    131.0f   // LSB/°/s at ±250 °/s

// ──────────────────────────────────────────────────────────────
//  TYPES
// ──────────────────────────────────────────────────────────────
enum Gesture { G_STOP, G_FORWARD, G_REVERSE, G_LEFT, G_RIGHT };
const char* gestureName[] = { "STOP", "FORWARD", "REVERSE", "LEFT", "RIGHT" };

struct ImuData { float ax, ay, az, gx, gy, gz; };

// ──────────────────────────────────────────────────────────────
//  STATE
// ──────────────────────────────────────────────────────────────
float   pitch = 0.0f, roll = 0.0f;
Gesture currentGesture  = G_STOP;
Gesture lastSentGesture = G_STOP;
int     lastSentSpeed   = 0;

unsigned long lastSampleMs  = 0;
unsigned long lastHttpMs    = 0;
unsigned long lastLedToggle = 0;
bool          ledState      = false;

String carBase;

// ──────────────────────────────────────────────────────────────
//  MPU-6050 I²C HELPERS
// ──────────────────────────────────────────────────────────────
bool mpuWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool mpuRead14(uint8_t* buf) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14);
  for (uint8_t i = 0; i < 14 && Wire.available(); i++) buf[i] = Wire.read();
  return true;
}

bool mpuInit() {
  mpuWriteReg(MPU_PWR_MGMT_1,   0x00);  // wake up
  delay(100);
  mpuWriteReg(MPU_CONFIG,       0x04);  // DLPF ~21 Hz
  mpuWriteReg(MPU_GYRO_CONFIG,  0x00);  // ±250 °/s
  mpuWriteReg(MPU_ACCEL_CONFIG, 0x00);  // ±2 g
  // Verify comms: read WHO_AM_I (0x75) — should return 0x68
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  return Wire.available() && Wire.read() == 0x68;
}

bool mpuRead(ImuData& d) {
  uint8_t buf[14];
  if (!mpuRead14(buf)) return false;
  d.ax = (int16_t)(buf[0]  << 8 | buf[1])  / ACCEL_SCALE;
  d.ay = (int16_t)(buf[2]  << 8 | buf[3])  / ACCEL_SCALE;
  d.az = (int16_t)(buf[4]  << 8 | buf[5])  / ACCEL_SCALE;
  // buf[6..7] = temperature, skip
  d.gx = (int16_t)(buf[8]  << 8 | buf[9])  / GYRO_SCALE;
  d.gy = (int16_t)(buf[10] << 8 | buf[11]) / GYRO_SCALE;
  d.gz = (int16_t)(buf[12] << 8 | buf[13]) / GYRO_SCALE;
  return true;
}

// ──────────────────────────────────────────────────────────────
//  COMPLEMENTARY FILTER
// ──────────────────────────────────────────────────────────────
void updateAngles(const ImuData& d, float dt) {
  float pa = atan2f(-d.ax, sqrtf(d.ay*d.ay + d.az*d.az)) * RAD_TO_DEG;
  float ra = atan2f( d.ay, d.az) * RAD_TO_DEG;
  pitch = COMP_ALPHA * (pitch + d.gy * dt) + (1.0f - COMP_ALPHA) * pa;
  roll  = COMP_ALPHA * (roll  + d.gx * dt) + (1.0f - COMP_ALPHA) * ra;
}

// ──────────────────────────────────────────────────────────────
//  GESTURE CLASSIFICATION
// ──────────────────────────────────────────────────────────────
Gesture classifyGesture(float p, float r) {
  bool fp = p < -DEAD_ZONE_DEG, rp = p > DEAD_ZONE_DEG;
  bool fl = r < -DEAD_ZONE_DEG, fr = r > DEAD_ZONE_DEG;
  if (fp && !fl && !fr) return G_FORWARD;
  if (rp && !fl && !fr) return G_REVERSE;
  if (fl && !fp && !rp) return G_LEFT;
  if (fr && !fp && !rp) return G_RIGHT;
  return G_STOP;
}

int gestureSpeed(float angle) {
  float mag  = fabsf(angle) - DEAD_ZONE_DEG;
  if (mag <= 0) return 0;
  float frac = constrain(mag / (MAX_ANGLE_DEG - DEAD_ZONE_DEG), 0.0f, 1.0f);
  return (int)(MIN_SPEED + frac * (MAX_SPEED - MIN_SPEED));
}

// ──────────────────────────────────────────────────────────────
//  HTTP MOTOR COMMANDS
// ──────────────────────────────────────────────────────────────
void httpGet(const String& url) {
  HTTPClient http;
  http.begin(url);
  http.setTimeout(200);
  http.GET();
  http.end();
}

void setMotor(const char* m, int spd) {
  httpGet(carBase + "/set?motor=" + m + "&speed=" + String(spd));
}

void sendStop() {
  httpGet(carBase + "/stopall");
  Serial.println("[HTTP] STOP ALL");
}

void sendAllMotors(int spd) {
  const char* m[] = {"fl", "fr", "rl", "rr"};
  for (int i = 0; i < 4; i++) setMotor(m[i], spd);
  Serial.printf("[HTTP] ALL %+d\n", spd);
}

void sendTurnLeft(int spd) {
  // Left wheels back, right wheels forward → spins left
  setMotor("fl", -spd); setMotor("rl", -spd);
  setMotor("fr",  spd); setMotor("rr",  spd);
  Serial.printf("[HTTP] TURN LEFT %d\n", spd);
}

void sendTurnRight(int spd) {
  // Left wheels forward, right wheels back → spins right
  setMotor("fl",  spd); setMotor("rl",  spd);
  setMotor("fr", -spd); setMotor("rr", -spd);
  Serial.printf("[HTTP] TURN RIGHT %d\n", spd);
}

void dispatchGesture(Gesture g, int spd) {
  switch (g) {
    case G_FORWARD: sendAllMotors( spd); break;
    case G_REVERSE: sendAllMotors(-spd); break;
    case G_LEFT:    sendTurnLeft(  spd); break;
    case G_RIGHT:   sendTurnRight( spd); break;
    default:        sendStop();          break;
  }
}

// ──────────────────────────────────────────────────────────────
//  LED
// ──────────────────────────────────────────────────────────────
void blinkLED(unsigned long ms) {
  if (millis() - lastLedToggle >= ms) {
    lastLedToggle = millis();
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }
}

// ──────────────────────────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n==============================================");
  Serial.println("  Gesture Glove — Full Firmware v1.0");
  Serial.println("  ESP32-C3 Super Mini + MPU-6050");
  Serial.println("==============================================");

  pinMode(LED_PIN, OUTPUT);

  // ── I²C + MPU-6050 ──────────────────────────────────────────
  Wire.begin(MPU_SDA, MPU_SCL);
  Wire.setClock(400000);
  Serial.print("  MPU-6050 init… ");
  if (!mpuInit()) {
    Serial.println("FAILED!");
    Serial.println("  Check: SDA→GPIO6  SCL→GPIO7  VCC→3V3  GND→GND  AD0→GND");
    // SOS blink forever
    while (true) {
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH); delay(120);
        digitalWrite(LED_PIN, LOW);  delay(120);
      }
      delay(400);
    }
  }
  Serial.println("OK (0x68)");

  // ── Wi-Fi ───────────────────────────────────────────────────
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true); delay(100);
  Serial.printf("  Connecting to \"%s\"", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    blinkLED(300); delay(5);
    Serial.print('.');
  }
  Serial.println("\n  Wi-Fi CONNECTED!");
  Serial.printf("  IP  : %s\n", WiFi.localIP().toString().c_str());

  carBase = "http://" CAR_IP ":" + String(CAR_PORT);
  Serial.printf("  Car : %s\n", carBase.c_str());
  Serial.println("----------------------------------------------");
  Serial.printf("  Dead zone ±%d°   Max angle ±%d°\n", DEAD_ZONE_DEG, MAX_ANGLE_DEG);
  Serial.printf("  Speed range %d – %d PWM\n", MIN_SPEED, MAX_SPEED);
  Serial.println("  Ready — tilt glove to drive!\n");

  lastSampleMs = millis();
}

// ──────────────────────────────────────────────────────────────
//  LOOP
// ──────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── IMU @ 20 Hz ─────────────────────────────────────────────
  if (now - lastSampleMs >= SAMPLE_MS) {
    float dt = (now - lastSampleMs) / 1000.0f;
    lastSampleMs = now;
    ImuData imu;
    if (mpuRead(imu)) updateAngles(imu, dt);
  }

  // ── Classify ─────────────────────────────────────────────────
  currentGesture = classifyGesture(pitch, roll);
  int speed = 0;
  if      (currentGesture == G_FORWARD || currentGesture == G_REVERSE) speed = gestureSpeed(pitch);
  else if (currentGesture == G_LEFT    || currentGesture == G_RIGHT)   speed = gestureSpeed(roll);

  // ── Send HTTP when gesture or speed changes ──────────────────
  bool gChanged = (currentGesture != lastSentGesture);
  bool sChanged = abs(speed - lastSentSpeed) > 15;
  bool ready    = (now - lastHttpMs >= HTTP_INTERVAL);

  if ((gChanged || (sChanged && currentGesture != G_STOP)) && ready) {
    Serial.printf("[GES] %-8s  P=%+5.1f°  R=%+5.1f°  spd=%d\n",
                  gestureName[currentGesture], pitch, roll, speed);
    dispatchGesture(currentGesture, speed);
    lastSentGesture = currentGesture;
    lastSentSpeed   = speed;
    lastHttpMs      = millis();
  }

  // ── LED: fast = active gesture, slow = idle ──────────────────
  blinkLED(currentGesture != G_STOP ? 80 : 800);
}

// ============================================================
//  WIRING REFERENCE
// ============================================================
//
//  MPU-6050 (GY-521 module)    ESP32-C3 Super Mini
//  ─────────────────────────   ──────────────────────────
//  VCC                      →  3V3   (3.3 V — NOT 5V!)
//  GND                      →  GND
//  SDA                      →  GPIO 6
//  SCL                      →  GPIO 7
//  AD0                      →  GND   (address = 0x68)
//  INT, XDA, XCL            →  NC (not connected)
//
//  NOTES:
//  • GY-521 module already has 4.7kΩ pull-ups on SDA/SCL.
//  • Keep wires short (≤10 cm) for reliable 400 kHz I²C.
//  • ESP32-C3 GPIOs are 3.3 V max — never apply 5 V to them.
//  • Onboard LED on GPIO 8: fast blink=gesture, slow blink=idle.
// ============================================================
