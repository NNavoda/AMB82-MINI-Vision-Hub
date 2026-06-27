/**
 * ============================================================
 *  ESP-NOW Receiver for Mecanum Car
 *  Hardware: ESP32 Dev Module
 *  Role: Receives Mecanum wheel speeds from the Gesture Glove
 *        over ESP-NOW, and drives the motors directly.
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>

// ----------------------------------------------------------
// PIN DEFINITIONS — LEFT MOTOR CONTROLLER
// ----------------------------------------------------------
#define M_FL_PWMA  13
#define M_FL_AIN1   4
#define M_FL_AIN2  16

#define M_RL_BIN1  17
#define M_RL_BIN2  18
#define M_RL_PWMB  19

// ----------------------------------------------------------
// PIN DEFINITIONS — RIGHT MOTOR CONTROLLER
// ----------------------------------------------------------
#define M_FR_BIN1  26
#define M_FR_BIN2  27
#define M_FR_PWMB  14

#define M_RR_AIN1  33
#define M_RR_AIN2  25
#define M_RR_PWMA  32

// ----------------------------------------------------------
// Motor PWM parameters (5 kHz, 8-bit)
// ----------------------------------------------------------
const int PWM_FREQ = 5000;
const int PWM_RES  = 8;       // 0–255

// ----------------------------------------------------------
// ESP-NOW Data Structure
// ----------------------------------------------------------
typedef struct CarCommand {
    int16_t fl; // Front-Left speed
    int16_t fr; // Front-Right speed
    int16_t rl; // Rear-Left speed
    int16_t rr; // Rear-Right speed
    float pitch;
    float roll;
    float yaw_rate;
} CarCommand;

CarCommand incomingCmd;
unsigned long lastRecvTime = 0;
const unsigned long TIMEOUT_MS = 500; // Stop if no signal for 500ms

// ----------------------------------------------------------
// Motor drive helpers
// ----------------------------------------------------------

void driveFL(int spd) {
  if (spd == 0) {
    digitalWrite(M_FL_AIN1, LOW); digitalWrite(M_FL_AIN2, LOW);
    ledcWrite(M_FL_PWMA, 0);
  } else if (spd > 0) {
    digitalWrite(M_FL_AIN1, LOW);  digitalWrite(M_FL_AIN2, HIGH);
    ledcWrite(M_FL_PWMA, spd);
  } else {
    digitalWrite(M_FL_AIN1, HIGH); digitalWrite(M_FL_AIN2, LOW);
    ledcWrite(M_FL_PWMA, -spd);
  }
}

void driveFR(int spd) {
  if (spd == 0) {
    digitalWrite(M_FR_BIN1, LOW); digitalWrite(M_FR_BIN2, LOW);
    ledcWrite(M_FR_PWMB, 0);
  } else if (spd > 0) {
    digitalWrite(M_FR_BIN1, HIGH); digitalWrite(M_FR_BIN2, LOW);
    ledcWrite(M_FR_PWMB, spd);
  } else {
    digitalWrite(M_FR_BIN1, LOW);  digitalWrite(M_FR_BIN2, HIGH);
    ledcWrite(M_FR_PWMB, -spd);
  }
}

void driveRL(int spd) {
  if (spd == 0) {
    digitalWrite(M_RL_BIN1, LOW); digitalWrite(M_RL_BIN2, LOW);
    ledcWrite(M_RL_PWMB, 0);
  } else if (spd > 0) {
    digitalWrite(M_RL_BIN1, HIGH); digitalWrite(M_RL_BIN2, LOW);
    ledcWrite(M_RL_PWMB, spd);
  } else {
    digitalWrite(M_RL_BIN1, LOW);  digitalWrite(M_RL_BIN2, HIGH);
    ledcWrite(M_RL_PWMB, -spd);
  }
}

void driveRR(int spd) {
  if (spd == 0) {
    digitalWrite(M_RR_AIN1, LOW); digitalWrite(M_RR_AIN2, LOW);
    ledcWrite(M_RR_PWMA, 0);
  } else if (spd > 0) {
    digitalWrite(M_RR_AIN1, LOW);  digitalWrite(M_RR_AIN2, HIGH);
    ledcWrite(M_RR_PWMA, spd);
  } else {
    digitalWrite(M_RR_AIN1, HIGH); digitalWrite(M_RR_AIN2, LOW);
    ledcWrite(M_RR_PWMA, -spd);
  }
}

void applySpeeds(int fl, int fr, int rl, int rr) {
  driveFL(fl);
  driveFR(fr);
  driveRL(rl);
  driveRR(rr);
}

void allStop() {
  applySpeeds(0, 0, 0, 0);
}

// ----------------------------------------------------------
// Callback when data is received
// ----------------------------------------------------------
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
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
  Serial.println("\n=== ESP-NOW Motor Receiver Starting ===");

  // Motor direction GPIO
  pinMode(M_FL_AIN1, OUTPUT); pinMode(M_FL_AIN2, OUTPUT);
  pinMode(M_RL_BIN1, OUTPUT); pinMode(M_RL_BIN2, OUTPUT);
  pinMode(M_FR_BIN1, OUTPUT); pinMode(M_FR_BIN2, OUTPUT);
  pinMode(M_RR_AIN1, OUTPUT); pinMode(M_RR_AIN2, OUTPUT);

  // Motor PWM
  ledcAttach(M_FL_PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(M_RL_PWMB, PWM_FREQ, PWM_RES);
  ledcAttach(M_FR_PWMB, PWM_FREQ, PWM_RES);
  ledcAttach(M_RR_PWMA, PWM_FREQ, PWM_RES);

  allStop();

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
  
  Serial.println("ESP-NOW Ready. Waiting for glove commands...");
}

// ----------------------------------------------------------
// Loop
// ----------------------------------------------------------
void loop() {
  // If we haven't received a command in TIMEOUT_MS, stop the motors for safety
  if (millis() - lastRecvTime > TIMEOUT_MS) {
    allStop();
  } else {
    applySpeeds(incomingCmd.fl, incomingCmd.fr, incomingCmd.rl, incomingCmd.rr);
  }
  
  delay(10); // small delay to prevent task watchdog
}
