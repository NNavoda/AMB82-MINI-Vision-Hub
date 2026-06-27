/**
 * ============================================================
 *  ESP-NOW Servo Head Tracker (Temporary Placeholder)
 *  Hardware: ESP32 Dev Module
 *  Role: Receives raw angles from the Gesture Glove
 *        and translates them smoothly into Pan/Tilt servo movements.
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>

// ----------------------------------------------------------
// PIN DEFINITIONS — CAMERA SERVO (MG996R, 180°)
// ----------------------------------------------------------
#define SERVO_PAN   21   // Horizontal servo — GPIO 21
#define SERVO_TILT  22   // Vertical servo   — GPIO 22

// ----------------------------------------------------------
// Servo PWM parameters (50 Hz, 16-bit for fine resolution)
// ----------------------------------------------------------
const int SERVO_FREQ = 50;
const int SERVO_RES  = 16;
const int SERVO_MIN  = 1638;   // 500 µs pulse  → 0°
const int SERVO_MAX  = 8192;   // 2500 µs pulse → 180°

const int SERVO_LIMIT_MIN = 50;
const int SERVO_LIMIT_MAX = 180;
const int PAN_SLIDER_CENTER  = 117;
const int TILT_SLIDER_CENTER = 135;

// ----------------------------------------------------------
// ESP-NOW Data Structure
// ----------------------------------------------------------
typedef struct CarCommand {
    int16_t fl;
    int16_t fr;
    int16_t rl;
    int16_t rr;
    float pitch;
    float roll;
    float yaw_rate;
} CarCommand;

CarCommand incomingCmd = {0, 0, 0, 0, 0.0f, 0.0f, 0.0f};
unsigned long lastRecvTime = 0;
const unsigned long TIMEOUT_MS = 500;

// Servo Angles (floats for smooth transition filtering)
float currentPan = PAN_SLIDER_CENTER;
float currentTilt = TILT_SLIDER_CENTER;

// ----------------------------------------------------------
// Servo helpers
// ----------------------------------------------------------
void writeServo(int pin, int angle) {
  int duty = map(angle, 0, 180, SERVO_MIN, SERVO_MAX);
  ledcWrite(pin, duty);
}

// ----------------------------------------------------------
// Callback when data is received
// ----------------------------------------------------------
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Only copy if the struct sizes match (in case glove firmware is out of sync)
  if (len == sizeof(incomingCmd)) {
    memcpy(&incomingCmd, incomingData, sizeof(incomingCmd));
    lastRecvTime = millis();
  }
}

// ----------------------------------------------------------
// Setup
// ----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP-NOW Servo Tracker Starting ===");

  // Servo PWM
  ledcAttach(SERVO_PAN,  SERVO_FREQ, SERVO_RES);
  ledcAttach(SERVO_TILT, SERVO_FREQ, SERVO_RES);

  // Initial position
  writeServo(SERVO_PAN, SERVO_LIMIT_MIN + SERVO_LIMIT_MAX - PAN_SLIDER_CENTER);
  writeServo(SERVO_TILT, TILT_SLIDER_CENTER);

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  Serial.print("Car MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register callback
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  
  Serial.println("ESP-NOW Ready. Waiting for glove angles to move servos...");
}

// ----------------------------------------------------------
// Loop
// ----------------------------------------------------------
void loop() {
  unsigned long now = millis();

  // If signal is lost, slowly return to center
  float targetPitch = 0.0f;
  float targetRoll = 0.0f;
  
  if (now - lastRecvTime <= TIMEOUT_MS) {
    targetPitch = incomingCmd.pitch;
    targetRoll = incomingCmd.roll;
  }

  // 1. Calculate absolute target angles.
  float sensitivity = 1.2f;
  
  // Invert tilt direction (+ instead of -)
  float targetTilt = TILT_SLIDER_CENTER + (targetPitch * sensitivity);
  
  // Use Roll for panning (absolute stability via accelerometer)
  float targetPan  = PAN_SLIDER_CENTER  - (targetRoll * sensitivity);

  // Clamp limits
  targetTilt = constrain(targetTilt, SERVO_LIMIT_MIN, SERVO_LIMIT_MAX);
  targetPan = constrain(targetPan, SERVO_LIMIT_MIN, SERVO_LIMIT_MAX);

  // 2. Smoothly transition current angles towards target angles
  // The factor (0.2) controls the speed/smoothness. Higher = faster, lower = smoother.
  currentTilt += (targetTilt - currentTilt) * 0.2f;
  currentPan  += (targetPan - currentPan) * 0.2f;
  
  // Write to servos
  // Pan is physically inverted mechanically according to WebMotorControl logic
  int panPhysical = SERVO_LIMIT_MIN + SERVO_LIMIT_MAX - (int)currentPan;
  writeServo(SERVO_PAN, panPhysical);
  writeServo(SERVO_TILT, (int)currentTilt);
  
  delay(15);
}
