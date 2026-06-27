// ============================================================
//  WebMotorControl.ino
//  Web-dashboard motor + camera servo controller
//  Board  : ESP32 Dev Module (30-pin)
//  Motors : TB6612FNG dual H-bridge (×2)
//  Servos : MG996R pan (D21) + tilt (D22), 180° range
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
#define M_FR_PWMA  14
#define M_FR_AIN1  26
#define M_FR_AIN2  27

#define M_RR_BIN1  33
#define M_RR_BIN2  25
#define M_RR_PWMB  32

// ----------------------------------------------------------
// PIN DEFINITIONS — CAMERA SERVO (MG996R, 180°)
// ----------------------------------------------------------
#define SERVO_PAN   21   // Horizontal servo — GPIO 21
#define SERVO_TILT  22   // Vertical servo   — GPIO 22

// ----------------------------------------------------------
// Motor PWM parameters (5 kHz, 8-bit)
// ----------------------------------------------------------
const int PWM_FREQ = 5000;
const int PWM_RES  = 8;       // 0–255

// ----------------------------------------------------------
// Servo PWM parameters (50 Hz, 16-bit for fine resolution)
//   MG996R pulse: 500 µs (0°) → 1500 µs (90°) → 2500 µs (180°)
//   At 50 Hz, period = 20 ms = 20000 µs
//   Duty at 16-bit (65536 steps):
//     0°  → 500/20000 × 65536 = 1638
//     90° → 1500/20000 × 65536 = 4915
//    180° → 2500/20000 × 65536 = 8192
// ----------------------------------------------------------
const int SERVO_FREQ = 50;
const int SERVO_RES  = 16;
const int SERVO_MIN  = 1638;   // 500 µs pulse  → 0°
const int SERVO_MAX  = 8192;   // 2500 µs pulse → 180°

// ----------------------------------------------------------
// Servo calibration & limits
//   Hard lower limit = 50° (below this the bracket collides)
//   Pan  : physical 113° = camera straight-ahead
//          INVERTED — higher slider value → camera moves RIGHT
//          Slider centre = 230 - 113 = 117  (230 = 50+180)
//   Tilt : physical 135° = camera straight-ahead (standard, not inverted)
// ----------------------------------------------------------
const int SERVO_LIMIT_MIN = 50;    // degrees — hard lower bound
const int SERVO_LIMIT_MAX = 180;   // degrees — hard upper bound
const int PAN_PHYS_CENTER  = 113;  // physical servo angle for straight-ahead
const int TILT_PHYS_CENTER = 135;  // physical servo angle for straight-ahead
// Slider value that maps to PAN_PHYS_CENTER after inversion:
//   slider_center = LIMIT_MIN + LIMIT_MAX - PAN_PHYS_CENTER = 50+180-113 = 117
const int PAN_SLIDER_CENTER  = 117;
const int TILT_SLIDER_CENTER = 135;

// ----------------------------------------------------------
// Web server on port 80
// ----------------------------------------------------------
WebServer server(80);

// Current state
int speedFL = 0, speedFR = 0, speedRL = 0, speedRR = 0;
int anglePan  = PAN_SLIDER_CENTER;   // slider value
int angleTilt = TILT_SLIDER_CENTER;  // slider value (= physical for tilt)

// ----------------------------------------------------------
// Servo helpers
// ----------------------------------------------------------

void writeServo(int pin, int angle) {
  int duty = map(angle, 0, 180, SERVO_MIN, SERVO_MAX);
  ledcWrite(pin, duty);
}

// ----------------------------------------------------------
// Motor drive helpers
// ----------------------------------------------------------

/** Drive Front-Left (inverted direction logic from MotorTest) */
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

/** Drive Front-Right — direction fully inverted vs original to fix reverse */
void driveFR(int spd) {
  if (spd == 0) {
    digitalWrite(M_FR_AIN1, LOW); digitalWrite(M_FR_AIN2, LOW);
    ledcWrite(M_FR_PWMA, 0);
  } else if (spd > 0) {
    digitalWrite(M_FR_AIN1, HIGH); digitalWrite(M_FR_AIN2, LOW);
    ledcWrite(M_FR_PWMA, spd);
  } else {
    digitalWrite(M_FR_AIN1, LOW);  digitalWrite(M_FR_AIN2, HIGH);
    ledcWrite(M_FR_PWMA, -spd);
  }
}

/** Drive Rear-Left (standard direction logic from MotorTest) */
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

/** Drive Rear-Right — direction fully inverted vs original to fix reverse */
void driveRR(int spd) {
  if (spd == 0) {
    digitalWrite(M_RR_BIN1, LOW); digitalWrite(M_RR_BIN2, LOW);
    ledcWrite(M_RR_PWMB, 0);
  } else if (spd > 0) {
    digitalWrite(M_RR_BIN1, LOW);  digitalWrite(M_RR_BIN2, HIGH);
    ledcWrite(M_RR_PWMB, spd);
  } else {
    digitalWrite(M_RR_BIN1, HIGH); digitalWrite(M_RR_BIN2, LOW);
    ledcWrite(M_RR_PWMB, -spd);
  }
}

void allStop() {
  driveFL(0); driveFR(0); driveRL(0); driveRR(0);
  speedFL = speedFR = speedRL = speedRR = 0;
}

// ----------------------------------------------------------
// HTML dashboard (PROGMEM)
// ----------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>ESP32 Car — Motor &amp; Camera Dashboard</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700;900&display=swap');

    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    :root {
      --bg:        #0d0f14;
      --card:      #161b26;
      --border:    #1f2a3a;
      --accent:    #00d4ff;
      --fwd:       #00e5a0;
      --rev:       #ff4f6e;
      --stop:      #4a5568;
      --servo:     #a78bfa;
      --servo-low: #f59e0b;
      --text:      #e2e8f0;
      --sub:       #7a8ba0;
      --radius:    16px;
      --shadow:    0 8px 32px rgba(0,0,0,0.5);
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
    header { text-align: center; margin-bottom: 28px; }
    header h1 {
      font-size: 2rem; font-weight: 900; letter-spacing: -0.5px;
      background: linear-gradient(135deg, var(--accent), #7b5cff);
      -webkit-background-clip: text; -webkit-text-fill-color: transparent;
    }
    header p { color: var(--sub); font-size: 0.85rem; margin-top: 4px; }
    #ip-badge {
      display: inline-block; margin-top: 10px;
      background: var(--card); border: 1px solid var(--border);
      border-radius: 50px; padding: 4px 14px;
      font-size: 0.78rem; color: var(--accent); letter-spacing: 0.5px;
    }

    /* ---- SECTION TITLES ---- */
    .section-title {
      width: 100%; max-width: 560px;
      display: flex; align-items: center; gap: 10px;
      margin: 24px 0 12px;
      font-size: 0.75rem; font-weight: 700; letter-spacing: 1.5px;
      text-transform: uppercase; color: var(--sub);
    }
    .section-title::after {
      content: ''; flex: 1; height: 1px; background: var(--border);
    }

    /* ---- MOTOR GRID (2×2) ---- */
    .car-grid {
      display: grid; grid-template-columns: 1fr 1fr;
      gap: 14px; width: 100%; max-width: 560px;
    }

    /* ---- MOTOR CARD ---- */
    .motor-card {
      background: var(--card); border: 1px solid var(--border);
      border-radius: var(--radius); padding: 18px 16px 14px;
      box-shadow: var(--shadow); display: flex; flex-direction: column;
      gap: 10px; transition: border-color 0.25s;
    }
    .motor-card:hover { border-color: var(--accent); }

    .motor-header { display: flex; justify-content: space-between; align-items: center; }
    .motor-label  { font-weight: 700; font-size: 0.95rem; letter-spacing: 0.4px; }
    .motor-badge  {
      font-size: 0.7rem; font-weight: 600; padding: 3px 10px;
      border-radius: 50px; background: #1a2235; border: 1px solid var(--border);
      color: var(--sub); text-transform: uppercase; transition: all 0.2s;
    }
    .motor-badge.fwd { background:#0a2e22; border-color:var(--fwd); color:var(--fwd); }
    .motor-badge.rev { background:#2e0a14; border-color:var(--rev); color:var(--rev); }

    .speed-row {
      display: flex; align-items: center; justify-content: space-between;
      font-size: 0.8rem; color: var(--sub);
    }
    .speed-val {
      font-size: 1.5rem; font-weight: 700; color: var(--text);
      min-width: 55px; text-align: right; transition: color 0.2s;
    }
    .speed-val.fwd { color: var(--fwd); }
    .speed-val.rev { color: var(--rev); }

    /* ---- SERVO GRID (1×2) ---- */
    .servo-grid {
      display: grid; grid-template-columns: 1fr 1fr;
      gap: 14px; width: 100%; max-width: 560px;
    }

    /* ---- SERVO CARD ---- */
    .servo-card {
      background: var(--card); border: 1px solid var(--border);
      border-radius: var(--radius); padding: 18px 16px 14px;
      box-shadow: var(--shadow); display: flex; flex-direction: column;
      gap: 10px; transition: border-color 0.25s;
    }
    .servo-card:hover { border-color: var(--servo); }

    .servo-header { display: flex; justify-content: space-between; align-items: center; }
    .servo-label  { font-weight: 700; font-size: 0.95rem; letter-spacing: 0.4px; }
    .servo-badge  {
      font-size: 0.75rem; font-weight: 700; padding: 3px 10px;
      border-radius: 50px; background: #1a1535; border: 1px solid var(--servo);
      color: var(--servo);
    }

    .angle-row {
      display: flex; align-items: center; justify-content: space-between;
      font-size: 0.8rem; color: var(--sub);
    }
    .angle-val {
      font-size: 1.5rem; font-weight: 700; color: var(--servo);
      min-width: 55px; text-align: center;
    }

    /* ---- SHARED SLIDER STYLE ---- */
    input[type=range] {
      -webkit-appearance: none; width: 100%; height: 6px;
      border-radius: 3px; outline: none; cursor: pointer;
    }
    /* Motor slider: rev→stop→fwd gradient */
    .motor-card input[type=range] {
      background: linear-gradient(to right, var(--rev) 0%, var(--stop) 50%, var(--fwd) 100%);
    }
    /* Servo slider: amber→purple gradient */
    .servo-card input[type=range] {
      background: linear-gradient(to right, var(--servo-low) 0%, var(--servo) 100%);
    }
    input[type=range]::-webkit-slider-thumb {
      -webkit-appearance: none; width: 22px; height: 22px;
      border-radius: 50%; background: var(--accent);
      border: 3px solid var(--bg);
      box-shadow: 0 0 8px rgba(0,212,255,0.6);
      cursor: pointer; transition: box-shadow 0.2s, transform 0.15s;
    }
    .servo-card input[type=range]::-webkit-slider-thumb {
      background: var(--servo);
      box-shadow: 0 0 8px rgba(167,139,250,0.6);
    }
    input[type=range]:active::-webkit-slider-thumb {
      transform: scale(1.2); box-shadow: 0 0 16px rgba(0,212,255,0.9);
    }
    .servo-card input[type=range]:active::-webkit-slider-thumb {
      box-shadow: 0 0 16px rgba(167,139,250,0.9);
    }

    /* ---- STOP / CENTER BUTTONS (per card) ---- */
    .btn-stop-motor, .btn-center-servo {
      width: 100%; padding: 8px; border-radius: 10px;
      border: 1px solid var(--border); background: #1a2235;
      font-family: 'Outfit', sans-serif; font-size: 0.8rem; font-weight: 600;
      letter-spacing: 0.5px; cursor: pointer; transition: all 0.2s; color: var(--sub);
    }
    .btn-stop-motor:hover  { background:#ff4f6e22; border-color:var(--rev); color:var(--rev); }
    .btn-center-servo:hover{ background:#a78bfa22; border-color:var(--servo); color:var(--servo); }

    /* ---- GLOBAL CONTROLS ---- */
    .global-controls {
      margin-top: 16px; width: 100%; max-width: 560px;
      display: flex; gap: 12px;
    }
    .btn {
      flex: 1; padding: 13px; border-radius: var(--radius); border: none;
      font-family: 'Outfit', sans-serif; font-size: 0.92rem; font-weight: 700;
      letter-spacing: 0.5px; cursor: pointer; transition: opacity 0.2s, transform 0.15s;
    }
    .btn:active { transform: scale(0.97); }
    .btn-all-stop {
      background: linear-gradient(135deg, #ff4f6e, #c0002a); color: #fff;
      box-shadow: 0 4px 20px rgba(255,79,110,0.4);
    }
    .btn-all-fwd {
      background: linear-gradient(135deg, #00e5a0, #00897b); color: #000;
      box-shadow: 0 4px 20px rgba(0,229,160,0.3);
    }
    .btn-center-all-cam {
      background: linear-gradient(135deg, #a78bfa, #6d28d9); color: #fff;
      box-shadow: 0 4px 20px rgba(167,139,250,0.35);
    }

    /* ---- STATUS BAR ---- */
    #status-bar {
      margin-top: 20px; width: 100%; max-width: 560px;
      background: var(--card); border: 1px solid var(--border);
      border-radius: var(--radius); padding: 11px 16px;
      font-size: 0.77rem; color: var(--sub);
      display: flex; justify-content: space-between; align-items: center;
    }
    #status-dot {
      width: 8px; height: 8px; border-radius: 50%; background: #888;
      display: inline-block; margin-right: 6px; transition: background 0.3s;
    }
    #status-dot.ok  { background: var(--fwd); box-shadow: 0 0 6px var(--fwd); }
    #status-dot.err { background: var(--rev); box-shadow: 0 0 6px var(--rev); }
  </style>
</head>
<body>

<header>
  <h1>🚗 ESP32 Car Dashboard</h1>
  <p>Motor drive &amp; Camera pan/tilt control</p>
  <div id="ip-badge">Loading IP…</div>
</header>

<!-- ====== MOTOR SECTION ====== -->
<div class="section-title">🔧 Drive Motors</div>

<div class="car-grid">

  <!-- FL -->
  <div class="motor-card">
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
  <div class="motor-card">
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
  <div class="motor-card">
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
  <div class="motor-card">
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

<!-- Motor global controls -->
<div class="global-controls">
  <button class="btn btn-all-stop" onclick="stopAll()">⏹ STOP ALL</button>
  <button class="btn btn-all-fwd"  onclick="allForward()">▶ ALL FORWARD</button>
</div>

<!-- ====== CAMERA SECTION ====== -->
<div class="section-title">📷 Camera Pan / Tilt</div>

<div class="servo-grid">

  <!-- PAN (horizontal, D21) — inverted: slider↑ = camera RIGHT -->
  <div class="servo-card">
    <div class="servo-header">
      <span class="servo-label">↔ Pan</span>
      <span class="servo-badge" id="badge-pan">117°</span>
    </div>
    <div class="angle-row">
      <span>← LEFT</span>
      <span class="angle-val" id="val-pan">117</span>
      <span>RIGHT →</span>
    </div>
    <input type="range" id="sl-pan" min="50" max="180" value="117"
           oninput="onServo('pan', this.value)" onchange="sendServo('pan', this.value)"/>
    <button class="btn-center-servo" onclick="centerServo('pan')">⊙ Centre Pan</button>
  </div>

  <!-- TILT (vertical, D22) — direct: slider↑ = camera UP -->
  <div class="servo-card">
    <div class="servo-header">
      <span class="servo-label">↕ Tilt</span>
      <span class="servo-badge" id="badge-tilt">135°</span>
    </div>
    <div class="angle-row">
      <span>↓ DOWN</span>
      <span class="angle-val" id="val-tilt">135</span>
      <span>UP ↑</span>
    </div>
    <input type="range" id="sl-tilt" min="50" max="180" value="135"
           oninput="onServo('tilt', this.value)" onchange="sendServo('tilt', this.value)"/>
    <button class="btn-center-servo" onclick="centerServo('tilt')">⊙ Centre Tilt</button>
  </div>

</div>

<!-- Servo global control -->
<div class="global-controls">
  <button class="btn btn-center-all-cam" onclick="centerAll()">⊙ CENTRE CAMERA</button>
</div>

<!-- ====== STATUS BAR ====== -->
<div id="status-bar">
  <span><span id="status-dot"></span><span id="status-msg">Initialising…</span></span>
  <span id="last-cmd">—</span>
</div>

<script>
  document.getElementById('ip-badge').textContent = '🌐 ' + location.host;

  // ---- MOTOR LOGIC ----
  function onSlider(motor, val) {
    val = parseInt(val);
    const valEl   = document.getElementById('val-'   + motor);
    const badgeEl = document.getElementById('badge-' + motor);
    valEl.textContent   = val;
    valEl.className     = 'speed-val ' + (val > 0 ? 'fwd' : val < 0 ? 'rev' : '');
    badgeEl.textContent = val > 0 ? 'FWD' : val < 0 ? 'REV' : 'STOP';
    badgeEl.className   = 'motor-badge ' + (val > 0 ? 'fwd' : val < 0 ? 'rev' : '');
  }

  const debounce = {};
  function sendSpeed(motor, val) {
    clearTimeout(debounce[motor]);
    debounce[motor] = setTimeout(() => {
      fetch('/set?motor=' + motor + '&speed=' + val)
        .then(r => r.text())
        .then(t => setStatus(true, t, 'motor ' + motor + '=' + val))
        .catch(()=> setStatus(false, 'No response', 'motor ' + motor));
    }, 30);
  }

  function stopMotor(motor) {
    document.getElementById('sl-' + motor).value = 0;
    onSlider(motor, 0);
    sendSpeed(motor, 0);
  }

  function stopAll() {
    ['fl','fr','rl','rr'].forEach(m => { document.getElementById('sl-'+m).value=0; onSlider(m,0); });
    fetch('/stopall')
      .then(r => r.text())
      .then(t => setStatus(true, t, 'ALL STOP'))
      .catch(() => setStatus(false, 'No response', 'ALL STOP'));
  }

  function allForward() {
    const spd = 150;
    ['fl','fr','rl','rr'].forEach(m => {
      document.getElementById('sl-'+m).value = spd; onSlider(m, spd); sendSpeed(m, spd);
    });
  }

  // ---- SERVO LOGIC ----
  // Centre slider values matching C++ constants:
  //   pan  slider centre = 117  (→ physical 113°, camera straight)
  //   tilt slider centre = 135  (= physical 135°, camera straight)
  const SERVO_CENTER = { pan: 117, tilt: 135 };

  function onServo(axis, val) {
    val = parseInt(val);
    document.getElementById('val-'   + axis).textContent = val;
    document.getElementById('badge-' + axis).textContent = val + '°';
  }

  function sendServo(axis, val) {
    clearTimeout(debounce['sv_' + axis]);
    debounce['sv_' + axis] = setTimeout(() => {
      fetch('/servo?axis=' + axis + '&angle=' + val)
        .then(r => r.text())
        .then(t => setStatus(true, t, 'servo ' + axis + '=' + val + '°'))
        .catch(()=> setStatus(false, 'No response', 'servo ' + axis));
    }, 30);
  }

  function centerServo(axis) {
    const c = SERVO_CENTER[axis];
    document.getElementById('sl-' + axis).value = c;
    onServo(axis, c);
    sendServo(axis, c);
  }

  function centerAll() {
    ['pan','tilt'].forEach(a => { centerServo(a); });
  }

  // ---- STATUS ----
  function setStatus(ok, msg, cmd) {
    const dot = document.getElementById('status-dot');
    dot.className = ok ? 'ok' : 'err';
    document.getElementById('status-msg').textContent = ok ? 'OK — ' + msg : '⚠ ' + msg;
    document.getElementById('last-cmd').textContent   = cmd;
    clearTimeout(dot._t);
    dot._t = setTimeout(() => dot.className = '', 2000);
  }
</script>
</body>
</html>
)rawhtml";

// ----------------------------------------------------------
// HTTP handlers
// ----------------------------------------------------------

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

/** /set?motor=fl&speed=150  (speed: -255 … +255) */
void handleSet() {
  if (!server.hasArg("motor") || !server.hasArg("speed")) {
    server.send(400, "text/plain", "Bad request"); return;
  }
  String motor = server.arg("motor");
  int    spd   = constrain(server.arg("speed").toInt(), -255, 255);

  if      (motor == "fl") { speedFL = spd; driveFL(spd); }
  else if (motor == "fr") { speedFR = spd; driveFR(spd); }
  else if (motor == "rl") { speedRL = spd; driveRL(spd); }
  else if (motor == "rr") { speedRR = spd; driveRR(spd); }
  else { server.send(400, "text/plain", "Unknown motor"); return; }

  String msg = motor + "=" + String(spd);
  Serial.println("[MOTOR] " + msg);
  server.send(200, "text/plain", msg);
}

/** /stopall */
void handleStopAll() {
  allStop();
  Serial.println("[MOTOR] All stop");
  server.send(200, "text/plain", "All motors stopped");
}

/** /servo?axis=pan&angle=<50..180>  (slider value)
 *  Pan is physically inverted: physical = LIMIT_MIN + LIMIT_MAX - slider
 *  Tilt is direct: physical = slider
 */
void handleServo() {
  if (!server.hasArg("axis") || !server.hasArg("angle")) {
    server.send(400, "text/plain", "Bad request"); return;
  }
  String axis  = server.arg("axis");
  int    angle = constrain(server.arg("angle").toInt(),
                           SERVO_LIMIT_MIN, SERVO_LIMIT_MAX);

  if (axis == "pan") {
    anglePan = angle;
    // Invert: higher slider → lower physical angle → camera moves RIGHT
    int physical = SERVO_LIMIT_MIN + SERVO_LIMIT_MAX - angle;
    writeServo(SERVO_PAN, physical);
    Serial.printf("[SERVO] pan slider=%d → physical=%d°\n", angle, physical);
  } else if (axis == "tilt") {
    angleTilt = angle;
    writeServo(SERVO_TILT, angle);
    Serial.printf("[SERVO] tilt=%d°\n", angle);
  } else {
    server.send(400, "text/plain", "Unknown axis"); return;
  }

  String msg = axis + "=" + String(angle);
  server.send(200, "text/plain", msg);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ----------------------------------------------------------
// setup()
// ----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== WebMotorControl + CameraServo starting ===");

  // Motor direction GPIO
  pinMode(M_FL_AIN1, OUTPUT); pinMode(M_FL_AIN2, OUTPUT);
  pinMode(M_RL_BIN1, OUTPUT); pinMode(M_RL_BIN2, OUTPUT);
  pinMode(M_FR_AIN1, OUTPUT); pinMode(M_FR_AIN2, OUTPUT);
  pinMode(M_RR_BIN1, OUTPUT); pinMode(M_RR_BIN2, OUTPUT);

  // Motor PWM — 5 kHz, 8-bit
  ledcAttach(M_FL_PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(M_RL_PWMB, PWM_FREQ, PWM_RES);
  ledcAttach(M_FR_PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(M_RR_PWMB, PWM_FREQ, PWM_RES);

  // Servo PWM — 50 Hz, 16-bit (separate frequency domain)
  ledcAttach(SERVO_PAN,  SERVO_FREQ, SERVO_RES);
  ledcAttach(SERVO_TILT, SERVO_FREQ, SERVO_RES);

  allStop();
  // Pan: slider centre 117 → physical 230-117=113° (straight ahead)
  writeServo(SERVO_PAN,  SERVO_LIMIT_MIN + SERVO_LIMIT_MAX - PAN_SLIDER_CENTER);
  // Tilt: slider centre 135 = physical 135° (straight ahead)
  writeServo(SERVO_TILT, TILT_SLIDER_CENTER);
  Serial.printf("Servos centred: pan physical=%d° tilt=%d°\n",
                SERVO_LIMIT_MIN + SERVO_LIMIT_MAX - PAN_SLIDER_CENTER,
                TILT_SLIDER_CENTER);

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
  server.on("/servo",   HTTP_GET, handleServo);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started → http://" + WiFi.localIP().toString());
  Serial.println("==============================================\n");
}

// ----------------------------------------------------------
// loop()
// ----------------------------------------------------------
void loop() {
  server.handleClient();
}
