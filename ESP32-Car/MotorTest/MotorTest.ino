// ==========================================
// PIN DEFINITIONS: LEFT MOTOR CONTROLLER
// ==========================================
#define M_FL_PWMA  13
#define M_FL_AIN1  4
#define M_FL_AIN2  16  // Hardware Pin RX2

#define M_RL_BIN1  17  // Hardware Pin TX2
#define M_RL_BIN2  18
#define M_RL_PWMB  19

// ==========================================
// PIN DEFINITIONS: RIGHT MOTOR CONTROLLER
// ==========================================
#define M_FR_PWMA  26  
#define M_FR_AIN1  27
#define M_FR_AIN2  14

#define M_RR_BIN1  32  
#define M_RR_BIN2  33
#define M_RR_PWMB  25

// --- TB6612FNG Setup Parameters ---
const int PWM_FREQ = 5000;   // 5 kHz carrier frequency for quiet operation
const int PWM_RES  = 8;      // 8-bit resolution (0 to 255)
const int TEST_DUTY = 150;   // Safe 58% speed baseline for bench testing

void setup() {
  Serial.begin(115200);
  
  // Initialize Control Register Output Directions
  pinMode(M_FL_AIN1, OUTPUT);   pinMode(M_FL_AIN2, OUTPUT);
  pinMode(M_RL_BIN1, OUTPUT);   pinMode(M_RL_BIN2, OUTPUT);
  pinMode(M_FR_AIN1, OUTPUT);   pinMode(M_FR_AIN2, OUTPUT);
  pinMode(M_RR_BIN1, OUTPUT);   pinMode(M_RR_BIN2, OUTPUT);

  // Bind Pins to PWM Timers using modern ESP32 API
  ledcAttach(M_FL_PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(M_RL_PWMB, PWM_FREQ, PWM_RES);
  ledcAttach(M_FR_PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(M_RR_PWMB, PWM_FREQ, PWM_RES);

  allStop();
  printMenu();
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    input.toUpperCase();

    if (input.length() == 2) {
      char motor = input.charAt(0);
      char dir   = input.charAt(1);
      executeCommand(motor, dir);
    } else if (input == "STOP") {
      allStop();
      Serial.println(">>> ALL MOTORS SHUT DOWN");
    }
  }
}

void executeCommand(char motor, char dir) {
  // Clear trace pins before assigning target motor sequence
  allStop(); 
  
  bool fwd = (dir == 'F');
  bool rev = (dir == 'B');

  if (!fwd && !rev) {
    Serial.println("ERROR: Invalid Direction. Use 'F' or 'B'.");
    return;
  }

  Serial.print("Executing: Motor ");
  Serial.print(motor);
  Serial.println(fwd ? " FORWARD" : " BACKWARD");

  switch(motor) {
    case '1': // Front Left (FL) - Inverted direction logic
      digitalWrite(M_FL_AIN1, fwd ? LOW : HIGH);
      digitalWrite(M_FL_AIN2, fwd ? HIGH : LOW);
      ledcWrite(M_FL_PWMA, TEST_DUTY);
      break;
      
    case '2': // Front Right (FR) - Inverted direction logic
      digitalWrite(M_FR_AIN1, fwd ? LOW : HIGH);
      digitalWrite(M_FR_AIN2, fwd ? HIGH : LOW);
      ledcWrite(M_FR_PWMA, TEST_DUTY);
      break;
      
    case '3': // Rear Left (RL) - Standard direction logic
      digitalWrite(M_RL_BIN1, fwd ? HIGH : LOW);
      digitalWrite(M_RL_BIN2, fwd ? LOW : HIGH);
      ledcWrite(M_RL_PWMB, TEST_DUTY);
      break;
      
    case '4': // Rear Right (RR) - Standard direction logic
      digitalWrite(M_RR_BIN1, fwd ? HIGH : LOW);
      digitalWrite(M_RR_BIN2, fwd ? LOW : HIGH);
      ledcWrite(M_RR_PWMB, TEST_DUTY);
      break;
      
    default:
      Serial.println("ERROR: Invalid Motor Index. Use 1(FL), 2(FR), 3(RL), 4(RR).");
      break;
  }
}

void allStop() {
  digitalWrite(M_FL_AIN1, LOW); digitalWrite(M_FL_AIN2, LOW); ledcWrite(M_FL_PWMA, 0);
  digitalWrite(M_RL_BIN1, LOW); digitalWrite(M_RL_BIN2, LOW); ledcWrite(M_RL_PWMB, 0);
  digitalWrite(M_FR_AIN1, LOW); digitalWrite(M_FR_AIN2, LOW); ledcWrite(M_FR_PWMA, 0);
  digitalWrite(M_RR_BIN1, LOW); digitalWrite(M_RR_BIN2, LOW); ledcWrite(M_RR_PWMB, 0);
}

void printMenu() {
  Serial.println("\n--- RECONFIGURED PCB MOTOR TEST MENU ---");
  Serial.println("Format: [Motor Number][Direction]");
  Serial.println("Motors: 1 = FL, 2 = FR, 3 = RL, 4 = RR");
  Serial.println("Directions: F = Forward, B = Backward");
  Serial.println("----------------------------------------\n");
}