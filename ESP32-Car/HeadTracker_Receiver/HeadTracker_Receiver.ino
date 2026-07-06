/**
 * ============================================================
 *  HEAD TRACKER RECEIVER — Car ESP32
 *  Hardware : ESP32 Dev Module
 *  Role     : Receives head angles (pitch, yaw) from the
 *             MPU-9250 head tracker unit via ESP-NOW,
 *             and drives Pan/Tilt servos.
 *             Motors are intentionally DISABLED in this sketch.
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>

// ----------------------------------------------------------
// SERVO PIN DEFINITIONS  (GPIO 21 = Pan, GPIO 22 = Tilt)
// ----------------------------------------------------------
#define SERVO_PAN   21
#define SERVO_TILT  22

// ----------------------------------------------------------
// Servo PWM (50 Hz, 16-bit for fine resolution)
// ----------------------------------------------------------
const int SERVO_FREQ = 50;
const int SERVO_RES  = 16;
const int SERVO_MIN  = 1638;   // 500 µs  -> 0°
const int SERVO_MAX  = 8192;   // 2500 µs -> 180°

// ----------------------------------------------------------
// Servo physical limits and neutral positions
// Pan center: 90° = straight ahead
// Tilt center: 110° = straight ahead (biased so servo has
//              more travel upward: 110->40 = 70° up range)
// ----------------------------------------------------------
const int PAN_CENTER   = 113;
const int TILT_CENTER  = 135;   // shifted so servo has travel both up and down
const int SERVO_MIN_ANG = 40;
const int SERVO_MAX_ANG = 180;

// Sensitivity: degrees of servo movement per degree of head movement
const float YAW_SCALE   = 1.5f;  // wider pan range
const float PITCH_SCALE = 1.8f;  // fuller tilt range

// ----------------------------------------------------------
// Smoothing: higher = more responsive, lower = smoother
// ----------------------------------------------------------
const float SMOOTH = 0.20f;

// ----------------------------------------------------------
// Timeout: if no data for this long, return to center
// ----------------------------------------------------------
const unsigned long TIMEOUT_MS = 800;

// ----------------------------------------------------------
// Data structure — must match HeadTracker exactly
// ----------------------------------------------------------
typedef struct HeadTrackerData {
    float pitch;
    float yaw;
    float roll;
} HeadTrackerData;

HeadTrackerData incoming = {0.0f, 0.0f, 0.0f};
unsigned long lastRecvMs = 0;
bool dataReceived = false;

// Current smoothed servo angles
float currentPan  = PAN_CENTER;
float currentTilt = TILT_CENTER;

// Yaw "zero" — the compass heading when the user is looking
// straight ahead. Set by pressing the BOOT button (GPIO 0).
float yawZero = 0.0f;
bool  yawZeroSet = false;

// ----------------------------------------------------------
// Servo helpers
// ----------------------------------------------------------
void writeServo(int pin, int angleDeg) {
  angleDeg = constrain(angleDeg, 0, 180);
  int duty = map(angleDeg, 0, 180, SERVO_MIN, SERVO_MAX);
  ledcWrite(pin, duty);
}

// ----------------------------------------------------------
// ESP-NOW receive callback
// ----------------------------------------------------------
void OnDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len == sizeof(incoming)) {
    memcpy(&incoming, data, sizeof(incoming));
    lastRecvMs = millis();
    dataReceived = true;

    // Auto-set yaw zero on first packet received
    if (!yawZeroSet) {
      yawZero = incoming.yaw;
      yawZeroSet = true;
      Serial.printf("Yaw zero set to: %.1f deg\n", yawZero);
    }
  }
}

// ----------------------------------------------------------
// Setup
// ----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Head Tracker Receiver Starting ===");

  // Servo PWM setup
  ledcAttach(SERVO_PAN,  SERVO_FREQ, SERVO_RES);
  ledcAttach(SERVO_TILT, SERVO_FREQ, SERVO_RES);

  // Start at center
  writeServo(SERVO_PAN,  PAN_CENTER);
  writeServo(SERVO_TILT, TILT_CENTER);

  // BOOT button for yaw re-zero (built-in on most ESP32 boards)
  pinMode(0, INPUT_PULLUP);

  // WiFi as station (required for ESP-NOW)
  WiFi.mode(WIFI_STA);
  Serial.print("Car MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

  Serial.println("ESP-NOW Ready. Waiting for head tracker data...");
  Serial.println("Tip: Press BOOT (GPIO 0) to re-zero the pan direction.");
}

// ----------------------------------------------------------
// Loop
// ----------------------------------------------------------
void loop() {
  unsigned long now = millis();

  // Re-zero yaw on BOOT button press
  if (digitalRead(0) == LOW && yawZeroSet) {
    yawZero = incoming.yaw;
    Serial.printf("Yaw re-zeroed to: %.1f deg\n", yawZero);
    delay(300); // debounce
  }

  float targetPan  = PAN_CENTER;
  float targetTilt = TILT_CENTER;

  if (dataReceived && (now - lastRecvMs <= TIMEOUT_MS)) {
    // Pan: yaw delta from zero heading (gyro-integrated, turns right = positive)
    float yawDelta = incoming.yaw - yawZero;
    // Wrap to -180..+180
    if (yawDelta >  180.0f) yawDelta -= 360.0f;
    if (yawDelta < -180.0f) yawDelta += 360.0f;

    targetPan  = PAN_CENTER  - yawDelta * YAW_SCALE;  // negated: turn right = pan right

    // Tilt: pitch up (negative pitch from IMU) = servo toward SERVO_MIN_ANG (camera up)
    // pitch down (positive) = servo toward SERVO_MAX_ANG (camera down)
    // TILT_CENTER=110 biases the servo so it can travel 70° up and 40° down
    targetTilt = TILT_CENTER + incoming.pitch * PITCH_SCALE;
  }

  // Clamp to physical limits
  targetPan  = constrain(targetPan,  SERVO_MIN_ANG, SERVO_MAX_ANG);
  targetTilt = constrain(targetTilt, SERVO_MIN_ANG, SERVO_MAX_ANG);

  // Exponential smoothing
  currentPan  += (targetPan  - currentPan)  * SMOOTH;
  currentTilt += (targetTilt - currentTilt) * SMOOTH;

  writeServo(SERVO_PAN,  (int)currentPan);
  writeServo(SERVO_TILT, (int)currentTilt);

  // Debug print every second
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    if (dataReceived) {
      Serial.printf("Recv -> Pitch:%.1f Yaw:%.1f | Servo Pan:%d Tilt:%d\n",
        incoming.pitch, incoming.yaw, (int)currentPan, (int)currentTilt);
    } else {
      Serial.println("Waiting for head tracker...");
    }
  }

  delay(15);  // ~66Hz loop
}
