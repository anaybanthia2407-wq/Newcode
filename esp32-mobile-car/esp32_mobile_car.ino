/*
  ESP32 DevKit V1 - Mobile (Phone) Controlled Wi-Fi Car
  -----------------------------------------------------
  The ESP32 creates its own Wi-Fi hotspot. Connect your phone to it, open
  http://192.168.4.1 in the browser and drive with the on-screen joystick.
  No app install and no router needed.

  Motor driver: L298N (default) or TB6612FNG - same pin logic.
  Library needed: "WebSockets" by Markus Sattler (Arduino Library Manager).
  Works with ESP32 Arduino core 2.x and 3.x.

  Safety features:
    - Failsafe: motors stop if no command arrives for FAILSAFE_MS
      (phone out of range, browser closed, Wi-Fi dropped).
    - Acceleration ramp: protects the gearbox and avoids current spikes
      that can brown-out/reset the ESP32.
    - Emergency STOP button with active braking.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>   // "WebSockets" by Markus Sattler

// ======================= USER SETTINGS =======================
const char* AP_SSID = "ESP32-Car";
const char* AP_PASS = "drive1234";      // must be at least 8 characters

// Motor driver pins (all safe, non-strapping GPIOs on the DevKit V1)
const int PIN_ENA = 32;   // Left motor speed (PWM)
const int PIN_IN1 = 33;   // Left motor direction A
const int PIN_IN2 = 25;   // Left motor direction B
const int PIN_IN3 = 26;   // Right motor direction A
const int PIN_IN4 = 27;   // Right motor direction B
const int PIN_ENB = 13;   // Right motor speed (PWM)

// Extras (optional - leave unconnected if not used)
const int PIN_STATUS_LED = 2;   // on-board blue LED
const int PIN_HEADLIGHTS = 4;   // LED(s) through 220 ohm resistor
const int PIN_HORN       = 23;  // active buzzer (5V types via a transistor)

// Flip these if a motor spins the wrong way (or swap its two wires)
const bool LEFT_INVERT  = false;
const bool RIGHT_INVERT = false;

// Tuning
const uint32_t PWM_FREQ    = 1000;  // Hz. L298N: 1-5 kHz. TB6612FNG: up to 20 kHz (silent)
const uint8_t  PWM_RES     = 8;     // bits -> duty 0..255
const int      MIN_DUTY    = 90;    // below this TT motors only hum; raise if wheels don't start
const int      DEADZONE    = 8;     // joystick % ignored around the centre
const int      RAMP_UP     = 10;    // duty change per tick when speeding up
const int      RAMP_DOWN   = 25;    // duty change per tick when slowing down
const uint32_t RAMP_TICK_MS = 10;
const uint32_t FAILSAFE_MS = 600;   // stop if no command for this long
const uint32_t BRAKE_MS    = 300;   // how long the emergency brake is held
// =============================================================

WebServer        http(80);
WebSocketsServer ws(81);

struct Motor {
  int en, inA, inB;
  uint8_t channel;    // LEDC channel (only used on core 2.x)
  bool invert;
  int current;        // signed duty actually applied (-255..255)
  int target;         // signed duty requested     (-255..255)
};

Motor leftMotor  = {PIN_ENA, PIN_IN1, PIN_IN2, 0, LEFT_INVERT,  0, 0};
Motor rightMotor = {PIN_ENB, PIN_IN3, PIN_IN4, 1, RIGHT_INVERT, 0, 0};

int      maxDuty      = 180;  // set from the speed slider (default ~70%)
uint32_t lastCmdMs    = 0;
uint32_t lastRampMs   = 0;
uint32_t brakeUntilMs = 0;
uint8_t  clientCount  = 0;
bool     failsafeActive = true;

// ---------------- PWM helpers (core 2.x and 3.x) ----------------
void pwmAttach(const Motor& m) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(m.en, PWM_FREQ, PWM_RES);
#else
  ledcSetup(m.channel, PWM_FREQ, PWM_RES);
  ledcAttachPin(m.en, m.channel);
#endif
}

void pwmWrite(const Motor& m, int duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(m.en, duty);
#else
  ledcWrite(m.channel, duty);
#endif
}

// ---------------- Motor control ----------------
// speed: -255 (full reverse) .. 0 (coast) .. 255 (full forward)
void applyMotor(const Motor& m, int speed) {
  if (m.invert) speed = -speed;
  if (speed > 0) {
    digitalWrite(m.inA, HIGH);
    digitalWrite(m.inB, LOW);
  } else if (speed < 0) {
    digitalWrite(m.inA, LOW);
    digitalWrite(m.inB, HIGH);
  } else {
    digitalWrite(m.inA, LOW);   // coast
    digitalWrite(m.inB, LOW);
  }
  pwmWrite(m, abs(speed));
}

void brakeMotor(const Motor& m) {
  digitalWrite(m.inA, HIGH);    // both inputs HIGH = short brake (L298N & TB6612)
  digitalWrite(m.inB, HIGH);
  pwmWrite(m, 255);
}

void stopAll() {
  leftMotor.target = rightMotor.target = 0;
}

void emergencyStop() {
  stopAll();
  leftMotor.current = rightMotor.current = 0;
  brakeMotor(leftMotor);
  brakeMotor(rightMotor);
  brakeUntilMs = millis() + BRAKE_MS;
  digitalWrite(PIN_HORN, LOW);
  Serial.println("EMERGENCY STOP");
}

// Joystick percent (-100..100) -> signed PWM duty with a minimum start duty
int pctToDuty(int pct) {
  if (pct == 0) return 0;
  int top = max(maxDuty, MIN_DUTY);
  int mag = MIN_DUTY + (long)abs(pct) * (top - MIN_DUTY) / 100;
  return pct > 0 ? mag : -mag;
}

// Arcade drive: y = forward/back, x = turn. Both -100..100.
void setDrive(int x, int y) {
  x = constrain(x, -100, 100);
  y = constrain(y, -100, 100);
  if (abs(x) < DEADZONE) x = 0;
  if (abs(y) < DEADZONE) y = 0;

  int l = y + x;
  int r = y - x;
  int biggest = max(abs(l), abs(r));
  if (biggest > 100) {          // keep the turn ratio while staying in range
    l = l * 100 / biggest;
    r = r * 100 / biggest;
  }
  leftMotor.target  = pctToDuty(l);
  rightMotor.target = pctToDuty(r);
}

// Move `current` toward `target` gradually; slowing down is faster than speeding up.
void rampMotor(Motor& m) {
  if (m.current == m.target) return;
  if (m.current == 0) {         // skip the dead band where the motor only hums
    int jump = min(MIN_DUTY, abs(m.target));
    m.current = m.target > 0 ? jump : -jump;
    applyMotor(m, m.current);
    return;
  }
  bool slowingDown = (m.target == 0) ||
                     (abs(m.target) < abs(m.current)) ||
                     ((m.target > 0) != (m.current > 0));
  int step = slowingDown ? RAMP_DOWN : RAMP_UP;
  int diff = m.target - m.current;
  if (abs(diff) <= step) m.current = m.target;
  else m.current += (diff > 0 ? step : -step);
  applyMotor(m, m.current);
}

// ---------------- Web page (served from flash) ----------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>ESP32 Car</title>
<style>
  *{box-sizing:border-box;-webkit-tap-highlight-color:transparent;user-select:none;-webkit-user-select:none}
  html,body{margin:0;height:100%;background:#12141a;color:#e8eaf0;font-family:system-ui,sans-serif;touch-action:none;overflow:hidden}
  .wrap{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-around;gap:16px;height:100%;padding:16px}
  #joy{position:relative;width:min(70vw,70vh,300px);aspect-ratio:1;border-radius:50%;
       background:radial-gradient(circle,#252a36 0%,#1b1f29 70%);border:2px solid #343b4d;touch-action:none}
  #knob{position:absolute;left:50%;top:50%;width:36%;aspect-ratio:1;margin:-18% 0 0 -18%;border-radius:50%;
        background:#4f8cff;box-shadow:0 4px 18px #4f8cff66;pointer-events:none}
  .panel{display:flex;flex-direction:column;gap:12px;min-width:200px}
  .status{font-size:14px;padding:6px 10px;border-radius:999px;background:#3a1f24;color:#ff8a8a;text-align:center}
  .status.ok{background:#1d3324;color:#7be29a}
  label{font-size:14px;color:#9aa3b5}
  input[type=range]{width:100%;accent-color:#4f8cff;touch-action:none}
  .row{display:flex;gap:10px}
  button{flex:1;font-size:17px;padding:14px;border:0;border-radius:12px;background:#2a3040;color:#e8eaf0;touch-action:none}
  button.on{background:#e0b341;color:#1a1a1a}
  #stop{background:#d33b3b;font-weight:700;font-size:20px;padding:18px}
  #info{font-size:12px;color:#6c7486;text-align:center}
</style></head><body>
<div class="wrap">
  <div id="joy"><div id="knob"></div></div>
  <div class="panel">
    <div id="status" class="status">Connecting...</div>
    <label>Max speed: <span id="spdv">70</span>%</label>
    <input id="spd" type="range" min="30" max="100" value="70">
    <div class="row">
      <button id="lights">Lights</button>
      <button id="horn">Horn</button>
    </div>
    <button id="stop">STOP</button>
    <div id="info">Keyboard: WASD / arrow keys, Space = stop</div>
  </div>
</div>
<script>
let ws, jx=0, jy=0, active=null, lights=false;
const $=id=>document.getElementById(id);
const joy=$('joy'), knob=$('knob'), st=$('status');

function send(m){ if(ws && ws.readyState===1) ws.send(m); }
function connect(){
  ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen=()=>{ st.textContent='Connected'; st.className='status ok';
                  send('S,'+$('spd').value); send('L,'+(lights?1:0)); };
  ws.onclose=()=>{ st.textContent='Disconnected - retrying...'; st.className='status';
                   setTimeout(connect,1000); };
  ws.onerror=()=>ws.close();
}
connect();

// ---- Joystick ----
function setKnob(dx,dy){ knob.style.transform='translate('+dx+'px,'+dy+'px)'; }
function moveJoy(e){
  const r=joy.getBoundingClientRect(), R=r.width/2, max=R-knob.offsetWidth/2;
  let dx=e.clientX-(r.left+R), dy=e.clientY-(r.top+R);
  const d=Math.hypot(dx,dy);
  if(d>max){ dx=dx*max/d; dy=dy*max/d; }
  setKnob(dx,dy);
  jx=Math.round(dx/max*100); jy=Math.round(-dy/max*100);
}
function releaseJoy(){ active=null; jx=jy=0; setKnob(0,0); }
joy.addEventListener('pointerdown',e=>{ active=e.pointerId; joy.setPointerCapture(e.pointerId); moveJoy(e); });
joy.addEventListener('pointermove',e=>{ if(e.pointerId===active) moveJoy(e); });
['pointerup','pointercancel','lostpointercapture'].forEach(t=>
  joy.addEventListener(t,e=>{ if(e.pointerId===active) releaseJoy(); }));

// ---- Keyboard (laptop) ----
const keys=new Set();
addEventListener('keydown',e=>{ if(e.key===' ') doStop(); keys.add(e.key.toLowerCase()); });
addEventListener('keyup',e=>keys.delete(e.key.toLowerCase()));
function keyAxis(){
  const k=n=>n.some(x=>keys.has(x));
  return [(k(['d','arrowright'])?100:0)-(k(['a','arrowleft'])?100:0),
          (k(['w','arrowup'])?100:0)-(k(['s','arrowdown'])?100:0)];
}

// ---- Send drive command 20x per second (also acts as heartbeat) ----
setInterval(()=>{
  let x=jx, y=jy;
  if(active===null && keys.size){ [x,y]=keyAxis(); if(y!==0) x=Math.round(x*0.6); }
  send('D,'+x+','+y);
},50);

// ---- Controls ----
$('spd').addEventListener('input',e=>{ $('spdv').textContent=e.target.value; send('S,'+e.target.value); });
$('lights').addEventListener('click',()=>{ lights=!lights; $('lights').classList.toggle('on',lights); send('L,'+(lights?1:0)); });
const horn=$('horn');
horn.addEventListener('pointerdown',()=>{ horn.classList.add('on'); send('H,1'); });
['pointerup','pointerleave','pointercancel'].forEach(t=>horn.addEventListener(t,()=>{ horn.classList.remove('on'); send('H,0'); }));
function doStop(){ releaseJoy(); keys.clear(); send('X'); }
$('stop').addEventListener('pointerdown',doStop);

// Stop if the page is hidden (screen locked, app switched)
document.addEventListener('visibilitychange',()=>{ if(document.hidden){ doStop(); send('H,0'); } });
</script></body></html>
)rawliteral";

// ---------------- Command handling ----------------
// Messages from the phone:
//   D,x,y  drive (x,y in -100..100)     S,n  max speed % (30..100)
//   L,0/1  headlights                   H,0/1 horn
//   X      emergency stop
void handleCommand(const char* msg) {
  lastCmdMs = millis();
  int a = 0, b = 0;
  switch (msg[0]) {
    case 'D':
      if (sscanf(msg, "D,%d,%d", &a, &b) == 2 && brakeUntilMs == 0) setDrive(a, b);
      break;
    case 'S':
      if (sscanf(msg, "S,%d", &a) == 1) maxDuty = map(constrain(a, 30, 100), 0, 100, 0, 255);
      break;
    case 'L':
      digitalWrite(PIN_HEADLIGHTS, msg[2] == '1' ? HIGH : LOW);
      break;
    case 'H':
      digitalWrite(PIN_HORN, msg[2] == '1' ? HIGH : LOW);
      break;
    case 'X':
      emergencyStop();
      break;
  }
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      clientCount++;
      lastCmdMs = millis();
      Serial.printf("Client %u connected\n", num);
      break;
    case WStype_DISCONNECTED:
      if (clientCount > 0) clientCount--;
      Serial.printf("Client %u disconnected\n", num);
      stopAll();
      digitalWrite(PIN_HORN, LOW);
      break;
    case WStype_TEXT: {
      char buf[32];
      size_t n = min(length, sizeof(buf) - 1);
      memcpy(buf, payload, n);
      buf[n] = '\0';
      handleCommand(buf);
      break;
    }
    default:
      break;
  }
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(115200);

  int outputs[] = {PIN_IN1, PIN_IN2, PIN_IN3, PIN_IN4, PIN_STATUS_LED, PIN_HEADLIGHTS, PIN_HORN};
  for (int p : outputs) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  pwmAttach(leftMotor);
  pwmAttach(rightMotor);
  applyMotor(leftMotor, 0);
  applyMotor(rightMotor, 0);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setSleep(false);            // lower, steadier latency
  Serial.printf("\nWi-Fi: %s  password: %s\nOpen http://%s\n",
                AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());

  http.on("/", []() { http.send_P(200, "text/html", INDEX_HTML); });
  http.onNotFound([]() { http.send_P(200, "text/html", INDEX_HTML); });
  http.begin();

  ws.begin();
  ws.onEvent(onWsEvent);
}

void loop() {
  http.handleClient();
  ws.loop();

  uint32_t now = millis();

  // Failsafe: no command recently -> stop and silence the horn
  bool timedOut = (now - lastCmdMs > FAILSAFE_MS);
  if (timedOut && !failsafeActive) {
    stopAll();
    digitalWrite(PIN_HORN, LOW);
    Serial.println("Failsafe: no signal, stopping");
  }
  failsafeActive = timedOut;

  // Hold the brake briefly after an emergency stop, then release to coast
  bool braking = (brakeUntilMs != 0);
  if (braking && now >= brakeUntilMs) {
    brakeUntilMs = 0;
    braking = false;
    applyMotor(leftMotor, 0);
    applyMotor(rightMotor, 0);
  }
  if (!braking && now - lastRampMs >= RAMP_TICK_MS) {
    lastRampMs = now;
    rampMotor(leftMotor);
    rampMotor(rightMotor);
  }

  // Status LED: solid = phone connected, blinking = waiting
  digitalWrite(PIN_STATUS_LED, clientCount > 0 ? HIGH : ((now / 500) % 2));
}
