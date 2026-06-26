// ============================================================
//  WebMotorControl.ino
//  Web-dashboard motor controller for 4-wheel ESP32 car
//  Board  : ESP32 Dev Module (30-pin)
//  Motors : TB6612FNG dual H-bridge (×2)
//  WiFi   : connects to phone hotspot, hosts HTTP server
// ============================================================

#include <WiFi.h>
#include <WebServer.h>

// ----------------------------------------------------------
// WiFi credentials
// ----------------------------------------------------------
const char* SSID     = "Redmi Note 10 Pro";
const char* PASSWORD = "00000007";

// ----------------------------------------------------------
// PIN DEFINITIONS — LEFT MOTOR CONTROLLER
// ----------------------------------------------------------
#define M_FL_PWMA  13
#define M_FL_AIN1   4
#define M_FL_AIN2  16   // Hardware RX2

#define M_RL_BIN1  17   // Hardware TX2
#define M_RL_BIN2  18
#define M_RL_PWMB  19

// ----------------------------------------------------------
// PIN DEFINITIONS — RIGHT MOTOR CONTROLLER
// ----------------------------------------------------------
#define M_FR_PWMA  26
#define M_FR_AIN1  27
#define M_FR_AIN2  14

#define M_RR_BIN1  32
#define M_RR_BIN2  33
#define M_RR_PWMB  25

// ----------------------------------------------------------
// PWM parameters
// ----------------------------------------------------------
const int PWM_FREQ = 5000;   // 5 kHz — quiet operation
const int PWM_RES  = 8;      // 8-bit → 0–255

// ----------------------------------------------------------
// Web server on port 80
// ----------------------------------------------------------
WebServer server(80);

// Current speed for each motor  (-255 … 0 … +255)
int speedFL = 0, speedFR = 0, speedRL = 0, speedRR = 0;

// ----------------------------------------------------------
// Motor drive helpers
// ----------------------------------------------------------

/** Drive Front-Left (inverted direction logic from MotorTest) */
void driveFL(int spd) {
  if (spd == 0) {
    digitalWrite(M_FL_AIN1, LOW); digitalWrite(M_FL_AIN2, LOW);
    ledcWrite(M_FL_PWMA, 0);
  } else if (spd > 0) {                      // FORWARD → AIN1=LOW, AIN2=HIGH
    digitalWrite(M_FL_AIN1, LOW);  digitalWrite(M_FL_AIN2, HIGH);
    ledcWrite(M_FL_PWMA, spd);
  } else {                                   // BACKWARD → AIN1=HIGH, AIN2=LOW
    digitalWrite(M_FL_AIN1, HIGH); digitalWrite(M_FL_AIN2, LOW);
    ledcWrite(M_FL_PWMA, -spd);
  }
}

/** Drive Front-Right (inverted direction logic from MotorTest) */
void driveFR(int spd) {
  if (spd == 0) {
    digitalWrite(M_FR_AIN1, LOW); digitalWrite(M_FR_AIN2, LOW);
    ledcWrite(M_FR_PWMA, 0);
  } else if (spd > 0) {                      // FORWARD → AIN1=LOW, AIN2=HIGH
    digitalWrite(M_FR_AIN1, LOW);  digitalWrite(M_FR_AIN2, HIGH);
    ledcWrite(M_FR_PWMA, spd);
  } else {                                   // BACKWARD → AIN1=HIGH, AIN2=LOW
    digitalWrite(M_FR_AIN1, HIGH); digitalWrite(M_FR_AIN2, LOW);
    ledcWrite(M_FR_PWMA, -spd);
  }
}

/** Drive Rear-Left (standard direction logic from MotorTest) */
void driveRL(int spd) {
  if (spd == 0) {
    digitalWrite(M_RL_BIN1, LOW); digitalWrite(M_RL_BIN2, LOW);
    ledcWrite(M_RL_PWMB, 0);
  } else if (spd > 0) {                      // FORWARD → BIN1=HIGH, BIN2=LOW
    digitalWrite(M_RL_BIN1, HIGH); digitalWrite(M_RL_BIN2, LOW);
    ledcWrite(M_RL_PWMB, spd);
  } else {                                   // BACKWARD → BIN1=LOW, BIN2=HIGH
    digitalWrite(M_RL_BIN1, LOW);  digitalWrite(M_RL_BIN2, HIGH);
    ledcWrite(M_RL_PWMB, -spd);
  }
}

/** Drive Rear-Right (standard direction logic from MotorTest) */
void driveRR(int spd) {
  if (spd == 0) {
    digitalWrite(M_RR_BIN1, LOW); digitalWrite(M_RR_BIN2, LOW);
    ledcWrite(M_RR_PWMB, 0);
  } else if (spd > 0) {                      // FORWARD → BIN1=HIGH, BIN2=LOW
    digitalWrite(M_RR_BIN1, HIGH); digitalWrite(M_RR_BIN2, LOW);
    ledcWrite(M_RR_PWMB, spd);
  } else {                                   // BACKWARD → BIN1=LOW, BIN2=HIGH
    digitalWrite(M_RR_BIN1, LOW);  digitalWrite(M_RR_BIN2, HIGH);
    ledcWrite(M_RR_PWMB, -spd);
  }
}

void allStop() {
  driveFL(0); driveFR(0); driveRL(0); driveRR(0);
  speedFL = speedFR = speedRL = speedRR = 0;
}

// ----------------------------------------------------------
// HTML dashboard (served from PROGMEM to save RAM)
// ----------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>ESP32 Car — Motor Dashboard</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700;900&display=swap');

    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    :root {
      --bg:       #0d0f14;
      --card:     #161b26;
      --border:   #1f2a3a;
      --accent:   #00d4ff;
      --fwd:      #00e5a0;
      --rev:      #ff4f6e;
      --stop:     #4a5568;
      --text:     #e2e8f0;
      --sub:      #7a8ba0;
      --radius:   16px;
      --shadow:   0 8px 32px rgba(0,0,0,0.5);
    }

    body {
      font-family: 'Outfit', sans-serif;
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 24px 16px 48px;
    }

    /* ---- HEADER ---- */
    header {
      text-align: center;
      margin-bottom: 32px;
    }
    header h1 {
      font-size: 2rem;
      font-weight: 900;
      letter-spacing: -0.5px;
      background: linear-gradient(135deg, var(--accent), #7b5cff);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
    }
    header p {
      color: var(--sub);
      font-size: 0.85rem;
      margin-top: 4px;
    }
    #ip-badge {
      display: inline-block;
      margin-top: 10px;
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 50px;
      padding: 4px 14px;
      font-size: 0.78rem;
      color: var(--accent);
      letter-spacing: 0.5px;
    }

    /* ---- CAR GRID ---- */
    .car-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      grid-template-rows: auto auto;
      gap: 16px;
      width: 100%;
      max-width: 560px;
    }

    /* ---- MOTOR CARD ---- */
    .motor-card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      padding: 20px 18px 16px;
      box-shadow: var(--shadow);
      display: flex;
      flex-direction: column;
      gap: 12px;
      transition: border-color 0.25s;
    }
    .motor-card:hover { border-color: var(--accent); }

    .motor-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .motor-label {
      font-weight: 700;
      font-size: 1rem;
      letter-spacing: 0.5px;
    }
    .motor-badge {
      font-size: 0.72rem;
      font-weight: 600;
      padding: 3px 10px;
      border-radius: 50px;
      background: #1a2235;
      border: 1px solid var(--border);
      color: var(--sub);
      text-transform: uppercase;
      transition: all 0.2s;
    }
    .motor-badge.fwd  { background:#0a2e22; border-color:var(--fwd); color:var(--fwd); }
    .motor-badge.rev  { background:#2e0a14; border-color:var(--rev); color:var(--rev); }

    /* speed readout */
    .speed-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      font-size: 0.82rem;
      color: var(--sub);
    }
    .speed-val {
      font-size: 1.6rem;
      font-weight: 700;
      color: var(--text);
      min-width: 60px;
      text-align: right;
      transition: color 0.2s;
    }
    .speed-val.fwd { color: var(--fwd); }
    .speed-val.rev { color: var(--rev); }

    /* slider */
    input[type=range] {
      -webkit-appearance: none;
      width: 100%;
      height: 6px;
      border-radius: 3px;
      outline: none;
      cursor: pointer;
      background: linear-gradient(to right, var(--rev) 0%, var(--stop) 50%, var(--fwd) 100%);
    }
    input[type=range]::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 22px; height: 22px;
      border-radius: 50%;
      background: var(--accent);
      border: 3px solid var(--bg);
      box-shadow: 0 0 8px rgba(0,212,255,0.6);
      cursor: pointer;
      transition: box-shadow 0.2s, transform 0.15s;
    }
    input[type=range]:active::-webkit-slider-thumb {
      transform: scale(1.2);
      box-shadow: 0 0 16px rgba(0,212,255,0.9);
    }

    /* stop button per card */
    .btn-stop-motor {
      width: 100%;
      padding: 8px;
      border-radius: 10px;
      border: 1px solid var(--border);
      background: #1a2235;
      color: var(--sub);
      font-family: 'Outfit', sans-serif;
      font-size: 0.8rem;
      font-weight: 600;
      letter-spacing: 0.5px;
      cursor: pointer;
      transition: all 0.2s;
    }
    .btn-stop-motor:hover {
      background: #ff4f6e22;
      border-color: var(--rev);
      color: var(--rev);
    }

    /* ---- GLOBAL CONTROLS ---- */
    .global-controls {
      margin-top: 20px;
      width: 100%;
      max-width: 560px;
      display: flex;
      gap: 12px;
    }
    .btn {
      flex: 1;
      padding: 14px;
      border-radius: var(--radius);
      border: none;
      font-family: 'Outfit', sans-serif;
      font-size: 0.95rem;
      font-weight: 700;
      letter-spacing: 0.5px;
      cursor: pointer;
      transition: opacity 0.2s, transform 0.15s;
    }
    .btn:active { transform: scale(0.97); }
    .btn-all-stop {
      background: linear-gradient(135deg, #ff4f6e, #c0002a);
      color: #fff;
      box-shadow: 0 4px 20px rgba(255,79,110,0.4);
    }
    .btn-all-fwd {
      background: linear-gradient(135deg, #00e5a0, #00897b);
      color: #000;
      box-shadow: 0 4px 20px rgba(0,229,160,0.3);
    }

    /* ---- STATUS BAR ---- */
    #status-bar {
      margin-top: 24px;
      width: 100%;
      max-width: 560px;
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      padding: 12px 18px;
      font-size: 0.78rem;
      color: var(--sub);
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    #status-dot {
      width: 8px; height: 8px;
      border-radius: 50%;
      background: #888;
      display: inline-block;
      margin-right: 6px;
      transition: background 0.3s;
    }
    #status-dot.ok  { background: var(--fwd); box-shadow: 0 0 6px var(--fwd); }
    #status-dot.err { background: var(--rev); box-shadow: 0 0 6px var(--rev); }
  </style>
</head>
<body>

<header>
  <h1>🚗 ESP32 Motor Dashboard</h1>
  <p>Individual motor control via PWM sliders</p>
  <div id="ip-badge">Loading IP…</div>
</header>

<!-- 2×2 motor card grid -->
<div class="car-grid" id="grid">

  <!-- FL -->
  <div class="motor-card" id="card-fl">
    <div class="motor-header">
      <span class="motor-label">↖ Front Left</span>
      <span class="motor-badge" id="badge-fl">STOP</span>
    </div>
    <div class="speed-row">
      <span>REV ←</span>
      <span class="speed-val" id="val-fl">0</span>
      <span>→ FWD</span>
    </div>
    <input type="range" id="sl-fl" min="-255" max="255" value="0"
           oninput="onSlider('fl', this.value)" onchange="sendSpeed('fl', this.value)"/>
    <button class="btn-stop-motor" onclick="stopMotor('fl')">⏹ Stop FL</button>
  </div>

  <!-- FR -->
  <div class="motor-card" id="card-fr">
    <div class="motor-header">
      <span class="motor-label">Front Right ↗</span>
      <span class="motor-badge" id="badge-fr">STOP</span>
    </div>
    <div class="speed-row">
      <span>REV ←</span>
      <span class="speed-val" id="val-fr">0</span>
      <span>→ FWD</span>
    </div>
    <input type="range" id="sl-fr" min="-255" max="255" value="0"
           oninput="onSlider('fr', this.value)" onchange="sendSpeed('fr', this.value)"/>
    <button class="btn-stop-motor" onclick="stopMotor('fr')">⏹ Stop FR</button>
  </div>

  <!-- RL -->
  <div class="motor-card" id="card-rl">
    <div class="motor-header">
      <span class="motor-label">↙ Rear Left</span>
      <span class="motor-badge" id="badge-rl">STOP</span>
    </div>
    <div class="speed-row">
      <span>REV ←</span>
      <span class="speed-val" id="val-rl">0</span>
      <span>→ FWD</span>
    </div>
    <input type="range" id="sl-rl" min="-255" max="255" value="0"
           oninput="onSlider('rl', this.value)" onchange="sendSpeed('rl', this.value)"/>
    <button class="btn-stop-motor" onclick="stopMotor('rl')">⏹ Stop RL</button>
  </div>

  <!-- RR -->
  <div class="motor-card" id="card-rr">
    <div class="motor-header">
      <span class="motor-label">Rear Right ↘</span>
      <span class="motor-badge" id="badge-rr">STOP</span>
    </div>
    <div class="speed-row">
      <span>REV ←</span>
      <span class="speed-val" id="val-rr">0</span>
      <span>→ FWD</span>
    </div>
    <input type="range" id="sl-rr" min="-255" max="255" value="0"
           oninput="onSlider('rr', this.value)" onchange="sendSpeed('rr', this.value)"/>
    <button class="btn-stop-motor" onclick="stopMotor('rr')">⏹ Stop RR</button>
  </div>

</div>

<!-- Global controls -->
<div class="global-controls">
  <button class="btn btn-all-stop" onclick="stopAll()">⏹ STOP ALL</button>
  <button class="btn btn-all-fwd"  onclick="allForward()">▶ ALL FORWARD</button>
</div>

<!-- Status bar -->
<div id="status-bar">
  <span><span id="status-dot"></span><span id="status-msg">Initialising…</span></span>
  <span id="last-cmd">—</span>
</div>

<script>
  // Populate IP badge
  document.getElementById('ip-badge').textContent = '🌐 ' + location.host;

  // Live UI update while dragging
  function onSlider(motor, val) {
    val = parseInt(val);
    const valEl   = document.getElementById('val-'  + motor);
    const badgeEl = document.getElementById('badge-' + motor);
    valEl.textContent = val;
    valEl.className  = 'speed-val ' + (val > 0 ? 'fwd' : val < 0 ? 'rev' : '');
    badgeEl.textContent = val > 0 ? 'FWD' : val < 0 ? 'REV' : 'STOP';
    badgeEl.className   = 'motor-badge ' + (val > 0 ? 'fwd' : val < 0 ? 'rev' : '');
  }

  // Debounce timer per motor so we don't flood the ESP
  const debounce = {};
  function sendSpeed(motor, val) {
    clearTimeout(debounce[motor]);
    debounce[motor] = setTimeout(() => {
      fetch('/set?motor=' + motor + '&speed=' + val)
        .then(r => r.text())
        .then(t => setStatus(true, t, motor + '=' + val))
        .catch(()=> setStatus(false, 'No response', motor + '=' + val));
    }, 30);
  }

  function stopMotor(motor) {
    document.getElementById('sl-'   + motor).value = 0;
    onSlider(motor, 0);
    sendSpeed(motor, 0);
  }

  function stopAll() {
    ['fl','fr','rl','rr'].forEach(m => {
      document.getElementById('sl-' + m).value = 0;
      onSlider(m, 0);
    });
    fetch('/stopall')
      .then(r => r.text())
      .then(t => setStatus(true, t, 'ALL STOP'))
      .catch(() => setStatus(false, 'No response', 'ALL STOP'));
  }

  function allForward() {
    const spd = 150;
    ['fl','fr','rl','rr'].forEach(m => {
      document.getElementById('sl-' + m).value = spd;
      onSlider(m, spd);
      sendSpeed(m, spd);
    });
  }

  function setStatus(ok, msg, cmd) {
    const dot = document.getElementById('status-dot');
    dot.className = 'ok ' + (ok ? 'ok' : 'err');
    document.getElementById('status-msg').textContent = ok ? 'OK — ' + msg : '⚠ ' + msg;
    document.getElementById('last-cmd').textContent   = cmd;
    clearTimeout(dot._t);
    dot._t = setTimeout(() => dot.className = 'status-dot', 2000);
  }
</script>
</body>
</html>
)rawhtml";

// ----------------------------------------------------------
// HTTP handlers
// ----------------------------------------------------------

/** Serve the dashboard */
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

/** /set?motor=fl&speed=150  (speed: -255 … +255) */
void handleSet() {
  if (!server.hasArg("motor") || !server.hasArg("speed")) {
    server.send(400, "text/plain", "Bad request");
    return;
  }
  String motor = server.arg("motor");
  int    spd   = constrain(server.arg("speed").toInt(), -255, 255);

  if      (motor == "fl") { speedFL = spd; driveFL(spd); }
  else if (motor == "fr") { speedFR = spd; driveFR(spd); }
  else if (motor == "rl") { speedRL = spd; driveRL(spd); }
  else if (motor == "rr") { speedRR = spd; driveRR(spd); }
  else { server.send(400, "text/plain", "Unknown motor"); return; }

  String msg = motor + "=" + String(spd);
  Serial.println("[WEB] Set " + msg);
  server.send(200, "text/plain", msg);
}

/** /stopall */
void handleStopAll() {
  allStop();
  Serial.println("[WEB] All stop");
  server.send(200, "text/plain", "All motors stopped");
}

/** 404 */
void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ----------------------------------------------------------
// setup()
// ----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== WebMotorControl starting ===");

  // GPIO direction pins
  pinMode(M_FL_AIN1, OUTPUT); pinMode(M_FL_AIN2, OUTPUT);
  pinMode(M_RL_BIN1, OUTPUT); pinMode(M_RL_BIN2, OUTPUT);
  pinMode(M_FR_AIN1, OUTPUT); pinMode(M_FR_AIN2, OUTPUT);
  pinMode(M_RR_BIN1, OUTPUT); pinMode(M_RR_BIN2, OUTPUT);

  // Attach PWM (modern ESP32 Arduino core API)
  ledcAttach(M_FL_PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(M_RL_PWMB, PWM_FREQ, PWM_RES);
  ledcAttach(M_FR_PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(M_RR_PWMB, PWM_FREQ, PWM_RES);

  allStop();

  // Connect to hotspot
  Serial.printf("Connecting to '%s' ", SSID);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  // Register routes
  server.on("/",        HTTP_GET, handleRoot);
  server.on("/set",     HTTP_GET, handleSet);
  server.on("/stopall", HTTP_GET, handleStopAll);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started. Open browser to: http://" + WiFi.localIP().toString());
  Serial.println("======================================\n");
}

// ----------------------------------------------------------
// loop()
// ----------------------------------------------------------
void loop() {
  server.handleClient();
}
