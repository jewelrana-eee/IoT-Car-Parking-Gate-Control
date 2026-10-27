#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include "esp_task_wdt.h"

#define WDT_TIMEOUT_SEC 10

const char *AP_SSID = "YOUR_WIFI_NAME";
const char *AP_PASS = "YOUR_WIFI_PASSWORD";

WebServer server(80);

// ---------------- Pin map ----------------
#define PIN_IR_SLOT1   34   
#define PIN_IR_SLOT2   35   
#define PIN_IR_SLOT3   33   
#define PIN_IR_SLOT4   25   

#define PIN_ULTRA_TRIG 16   
#define PIN_ULTRA_ECHO 17
#define PIN_IR_INNER   32   

#define PIN_GATE_SERVO 27   
#define PIN_OLED_SDA   21
#define PIN_OLED_SCL   22

#define PIN_LED_LEFT   26   
#define PIN_LED_RIGHT  13   
#define PIN_BUZZER     23

#define ULTRA_THRESHOLD_CM   8     
#define ULTRA_SAMPLES         5     

#define TOTAL_CAPACITY 4
int carsInside = 0; 
volatile int pendingValidEntries = 0; 

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

unsigned long bootSplashStartTime = 0;
bool bootSplashDone = false;
const unsigned long BOOT_SPLASH_DURATION = 2000;

Servo gateServo;
const int GATE_CLOSED_ANGLE = 87;
const int GATE_OPEN_ANGLE   = 0;

// ---- Servo timing (dynamic, non-blocking detach) ----
const unsigned long SERVO_MS_PER_DEGREE = 8;    // tune to your servo's actual speed
const unsigned long SERVO_SETTLE_MARGIN = 150;  // extra buffer after travel completes
unsigned long servoSettleDuration = 400;        // recalculated per move

int currentServoAngle = -1; 
bool servoNeedsDetaching = false;
unsigned long servoMoveStartAt = 0;

// ---- Repark grace period (prevents false illegal on self-nudge) ----
// MIN_REPARK_GAP_MS filters out sensor bounce/flicker (which resolves in
// well under a second) from a genuine "car nudged out and back" event
// (which realistically takes at least a couple of seconds). Without this
// floor, a brief IR flicker during placement could silently clear an
// illegal flag via the repark/correction branches below.
#define MIN_REPARK_GAP_MS 1500   // below this, treat as sensor bounce — NOT a real repark
#define REPARK_GRACE_MS 10000    // 10 sec window

// ---- Illegal self-correction grace ----
// If a car was flagged illegal (e.g. parked in a RESERVED/MAINTENANCE
// slot) and then moves itself to a different, valid slot shortly after,
// this window lets that move be treated as a correction rather than a
// brand-new illegal entry. The original violation stays in the event
// log either way — this only stops a second/duplicate buzzer+OLED alert.
unsigned long lastIllegalVacatedAt = 0;
const unsigned long ILLEGAL_CORRECTION_GRACE_MS = 15000;   // 15 sec window

// ---- Non-blocking ultrasonic (interrupt-driven echo timing) ----
// Replaces the old pulseIn()-based blocking read. A background state
// machine fires the trigger pulse and an ISR captures the echo width,
// so loop() is never stalled waiting on the sensor.
volatile unsigned long ultraEchoRiseAtUs = 0;
volatile unsigned long ultraEchoDurationUs = 0;
volatile bool ultraEchoCaptured = false;

enum UltraState { ULTRA_IDLE, ULTRA_TRIG_LOW_SETTLE, ULTRA_TRIG_PULSE, ULTRA_WAIT_ECHO };
UltraState ultraState = ULTRA_IDLE;
unsigned long ultraStateChangedAtUs = 0;
unsigned long ultraLastTriggerAtMs = 0;
const unsigned long ULTRA_TRIGGER_INTERVAL_MS = 60;
const unsigned long ULTRA_ECHO_TIMEOUT_US = 25000;

long ultraDistanceSamples[ULTRA_SAMPLES];
int ultraSampleTotalCount = 0;
long ultraAveragedDistanceCm = 999;
bool outerCarPresentFlag = false;

void IRAM_ATTR ultrasonicEchoISR() {
  if (digitalRead(PIN_ULTRA_ECHO) == HIGH) {
    ultraEchoRiseAtUs = micros();
  } else {
    ultraEchoDurationUs = micros() - ultraEchoRiseAtUs;
    ultraEchoCaptured = true;
  }
}

struct Slot {
  int   irPin;
  char  side;        
  bool  occupied;
  bool  prevOccupied;
  bool  rawState;
  unsigned long lastStateChange;
  bool  isIllegal;     
  String overrideState; 
  unsigned long lastVacatedAt;
};

Slot slots[4] = {
  { PIN_IR_SLOT1, 'L', false, false, false, 0, false, "", 0 },
  { PIN_IR_SLOT2, 'L', false, false, false, 0, false, "", 0 },
  { PIN_IR_SLOT3, 'R', false, false, false, 0, false, "", 0 },
  { PIN_IR_SLOT4, 'R', false, false, false, 0, false, "", 0 },
};

bool slotVacated = false;

enum GateState { GATE_IDLE, GATE_OPEN_WAIT, GATE_PASS_WAIT, GATE_CLOSING };
GateState gateState = GATE_IDLE;

enum GateMode { GATE_AUTO, GATE_MANUAL };
GateMode gateMode = GATE_AUTO;

// ---- Manual -> Auto auto-revert timer ----
unsigned long manualCommandAt = 0;
const unsigned long MANUAL_AUTO_REVERT_MS = 10000;  // 10 sec

enum SequenceStage { SEQ_NONE, SEQ_ENTRY, SEQ_EXIT };
SequenceStage seqStage = SEQ_NONE;

unsigned long gateOpenedAt = 0;
unsigned long passTimeAt = 0;

const unsigned long GATE_TIMEOUT_MS   = 12000;
const unsigned long GATE_PASS_HOLD_MS = 1500; 

int assignedSlotForActivity = -1;
int leavingSlotIndex = -1; 
int illegalSlotIndex = -1; 
String webStatusMessage = "SYSTEM READY / IDLE";

enum OledPage { PAGE_HOME, PAGE_SYSTEM, PAGE_WELCOME, PAGE_EXITING, PAGE_THANKS_SLOT, PAGE_THANKS_EXIT, PAGE_FULL, PAGE_ILLEGAL };
OledPage currentPage = PAGE_HOME;
unsigned long lastPageSwitch = 0;
const unsigned long IDLE_ROTATE_MS = 6000; 

unsigned long thanksShownAt = 0;
bool showThanksSlotPage = false;
const unsigned long THANKS_SHOW_MS = 2500;

unsigned long exitingShownAt = 0;
bool showExitingPage = false;
const unsigned long EXITING_SHOW_MS = 2500;

bool showFullAlert = false;

enum BuzzerPattern { BUZ_NONE, BUZ_SHORT, BUZ_DOUBLE, BUZ_ILLEGAL_FAST, BUZ_CONTINUOUS };
BuzzerPattern buzzerPattern = BUZ_NONE;
int buzzerStep = 0;
unsigned long buzzerStepAt = 0;

// ---- Illegal-entry queue (handles multiple simultaneous illegal entries) ----
#define ILLEGAL_QUEUE_SIZE 4
int illegalQueue[ILLEGAL_QUEUE_SIZE];
int illegalQueueHead = 0;
int illegalQueueTail = 0;
int illegalQueueCount = 0;

// ---- Recent activity / event log (in-memory, no dashboard layout change —
//      purely additive "Recent Activity" panel appended below existing ones) ----
#define EVENT_LOG_SIZE 20
struct EventLogEntry {
  unsigned long timestampMs;
  String description;
};
EventLogEntry eventLog[EVENT_LOG_SIZE];
int eventLogHead = 0;
int eventLogCount = 0;

void showBootSplash();
void setupHotspot();
void readSlotSensors();
void handleGateLogic();
void updateIndicatorLEDs();
void updateBuzzer();
void updateOled();
void triggerBuzzer(BuzzerPattern pattern);
void startContinuousBuzzer();
void stopContinuousBuzzer();
void drawPage(OledPage page);
void setServoAngle(int targetAngle);
void handleServoDetach();
void checkManualAutoRevert();
void updateUltrasonicNonBlocking();
void logEvent(String desc);
bool illegalQueuePush(int slotIdx);
bool illegalQueuePop(int &slotIdx);
void handleIllegalDetection(int slotIdx, String msg);
bool isParkingFullyBlocked();
int getPhysicalCarCount();
int getAvailableSpaceCount();

int getPhysicalCarCount() {
  return carsInside; 
}

// ---- Event log helper (used for the additive "Recent Activity" panel) ----
void logEvent(String desc) {
  eventLog[eventLogHead].timestampMs = millis();
  eventLog[eventLogHead].description = desc;
  eventLogHead = (eventLogHead + 1) % EVENT_LOG_SIZE;
  if (eventLogCount < EVENT_LOG_SIZE) eventLogCount++;
}

// ---- Illegal-entry queue helpers ----
bool illegalQueuePush(int slotIdx) {
  if (illegalQueueCount >= ILLEGAL_QUEUE_SIZE) return false;
  illegalQueue[illegalQueueTail] = slotIdx;
  illegalQueueTail = (illegalQueueTail + 1) % ILLEGAL_QUEUE_SIZE;
  illegalQueueCount++;
  return true;
}

bool illegalQueuePop(int &slotIdx) {
  if (illegalQueueCount == 0) return false;
  slotIdx = illegalQueue[illegalQueueHead];
  illegalQueueHead = (illegalQueueHead + 1) % ILLEGAL_QUEUE_SIZE;
  illegalQueueCount--;
  return true;
}

// Centralized illegal-alert trigger. If a buzzer alert is already running
// for another slot, this one queues instead of overriding it, so multiple
// simultaneous illegal entries each get their own full alert in turn.
void handleIllegalDetection(int slotIdx, String msg) {
  slots[slotIdx].isIllegal = true;
  webStatusMessage = msg;
  logEvent(msg + " (SLOT " + String(slotIdx + 1) + ")");

  if (buzzerPattern == BUZ_ILLEGAL_FAST) {
    illegalQueuePush(slotIdx);
  } else {
    illegalSlotIndex = slotIdx;
    triggerBuzzer(BUZ_ILLEGAL_FAST);
  }
}

// Availability is driven by the VALIDATED carsInside counter, not raw
// per-slot IR state. This prevents hand-removed cars or illegal entries
// from falsely freeing up / consuming space in the gate's decision logic.
int getAvailableSpaceCount() {
  int reserved = 0;
  for (int i = 0; i < 4; i++) {
    if (slots[i].overrideState != "") {   // RESERVED or MAINTENANCE
      reserved++;
    }
  }
  int avail = TOTAL_CAPACITY - carsInside - reserved;
  if (avail < 0) avail = 0;
  return avail;
}

bool isParkingFullyBlocked() {
  return (getAvailableSpaceCount() <= 0);
}

// Direct-jump write (same as original working version) + DYNAMIC
// non-blocking detach timing, scaled to the actual travel distance,
// so the PWM signal isn't cut before the servo finishes moving.
void setServoAngle(int targetAngle) {
  if (currentServoAngle == targetAngle && !servoNeedsDetaching) return;

  int prevAngle = (currentServoAngle == -1) ? targetAngle : currentServoAngle;
  int travelDegrees = abs(targetAngle - prevAngle);

  gateServo.attach(PIN_GATE_SERVO, 500, 2400);
  gateServo.write(targetAngle);
  currentServoAngle = targetAngle;
  servoMoveStartAt = millis();

  servoSettleDuration = (unsigned long)(travelDegrees * SERVO_MS_PER_DEGREE) + SERVO_SETTLE_MARGIN;
  servoNeedsDetaching = true;
}

void handleServoDetach() {
  if (servoNeedsDetaching && (millis() - servoMoveStartAt >= servoSettleDuration)) {
    gateServo.detach();
    servoNeedsDetaching = false;
  }
}

// Manual Open/Close commands auto-revert to AUTO after MANUAL_AUTO_REVERT_MS.
// Clicking "Return to Auto" clears manualCommandAt immediately, so this
// function no longer fires for that session (instant switch, no wait).
void checkManualAutoRevert() {
  if (gateMode == GATE_MANUAL && manualCommandAt != 0) {
    if (millis() - manualCommandAt >= MANUAL_AUTO_REVERT_MS) {
      gateMode = GATE_AUTO;
      manualCommandAt = 0;
      if (gateState == GATE_IDLE && currentServoAngle != GATE_CLOSED_ANGLE) {
        setServoAngle(GATE_CLOSED_ANGLE);
      }
      webStatusMessage = "AUTO MODE ENABLED (TIMEOUT)";
      logEvent("Auto mode restored (10s timeout)");
    }
  }
}

void handleRoot() {
  int avail = getAvailableSpaceCount();

  bool leftAvail = (!slots[0].occupied && slots[0].overrideState == "") || (!slots[1].occupied && slots[1].overrideState == "");
  bool rightAvail = (!slots[2].occupied && slots[2].overrideState == "") || (!slots[3].occupied && slots[3].overrideState == "");

  bool isFullNow = isParkingFullyBlocked();
  String displayStatus = webStatusMessage;
  if (isFullNow) displayStatus = "PARKING FULL!";

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Smart Car Parking Dashboard</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500;600&family=IBM+Plex+Sans:wght@400;500;600;700&display=swap');
    body { background: #0b0f13; color: #dfe6ea; font-family: 'IBM Plex Sans', sans-serif; margin: 0; padding: 20px 14px 50px; }
    .mono { font-family: 'IBM Plex Mono', monospace; }
    .wrap { max-width: 1000px; margin: 0 auto; }
    .panel { background: #10161b; border: 1px solid #1e2830; border-radius: 10px; padding: 18px 22px; margin-bottom: 20px; }
    .eyebrow { font-family: 'IBM Plex Mono', monospace; font-size: 11px; letter-spacing: 0.12em; text-transform: uppercase; color: #5b7280; }
    .header-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px; flex-wrap: wrap; gap: 14px; }
    .status-group { display: flex; align-items: center; gap: 12px; }
    .local-link-box { background: #151d24; border: 1px solid #2a3742; border-radius: 6px; padding: 10px 16px; font-family: 'IBM Plex Mono', monospace; font-size: 13px; color: #33d69f; font-weight: 600; }
    .live-display-box { background: #000; border: 2px solid #2a3742; border-radius: 6px; padding: 10px 18px; color: #33d69f; font-family: 'IBM Plex Mono', monospace; font-size: 13px; font-weight: 600; min-width: 180px; text-align: center; }
    .summary-bar { display: grid; grid-template-columns: repeat(4, 1fr); gap: 16px; }
    .summary-bar > div { border-right: 1px solid #1a232b; padding: 0 16px; }
    .summary-bar > div:first-child { padding-left: 0; }
    .summary-bar > div:last-child { border-right: none; padding-right: 0; }
    .main-grid { display: grid; grid-template-columns: 1.3fr 1fr; gap: 20px; }
    .slot-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 14px; }
    .slotcard { border-radius: 8px; padding: 16px; background: #0e1418; display: flex; flex-direction: column; justify-content: space-between; min-height: 145px; border: 1px solid #1e2830; box-sizing: border-box; }
    .dot { width: 10px; height: 10px; border-radius: 50%; display: inline-block; flex-shrink: 0; }
    .dot-green { background: #33d69f; box-shadow: 0 0 8px #33d69f; }
    .dot-red { background: #f4415c; box-shadow: 0 0 8px #f4415c; }
    .dot-yellow { background: #f5a623; box-shadow: 0 0 8px #f5a623; }
    .dot-grey { background: #8fa3ad; box-shadow: 0 0 8px #8fa3ad; }
    .dot-blue { background: #4a9eff; box-shadow: 0 0 8px #4a9eff; }
    button.ctl { font-family: 'IBM Plex Mono', monospace; font-size: 12px; padding: 10px 14px; border-radius: 6px; border: 1px solid #2a3742; background: #151d24; color: #cfe0e8; cursor: pointer; transition: 0.2s; }
    button.ctl:hover { background: #1a232b; }
    button.ctl.primary { border-color: #2a6f5c; color: #6fe6c4; }
    button.ctl.danger { border-color: #6f2a3a; color: #ff8fa3; }
    .mini-btn { font-family: 'IBM Plex Mono', monospace; font-size: 10px; padding: 5px 9px; border-radius: 4px; border: 1px solid #2a3742; background: #151d24; color: #9bb0bd; cursor: pointer; text-decoration: none; display: inline-block; text-align: center; flex: 1; }
    .mini-btn:hover { background: #25333e; color: #fff; }
    @media (max-width: 760px) { .main-grid { grid-template-columns: 1fr; } }
    @media (max-width: 520px) { .summary-bar { grid-template-columns: repeat(2, 1fr); row-gap: 18px; } .summary-bar > div { border-right: none; padding: 0; } }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="header-row">
      <div>
        <div class="eyebrow">DIU &middot; EEE &middot; Industrial Automation & Robotics</div>
        <h1 style="margin: 4px 0 0; font-size: 24px; font-weight: 700;">Smart Car Parking <span style="color: #5b7280; font-weight: 500;">/ Control Dashboard</span></h1>
      </div>
      <div class="status-group">
        <div class="local-link-box">ESP32 LOCAL LINK OK</div>
        <div class="live-display-box" style="color: )rawliteral";
  html += (isFullNow ? "#f4415c" : "#33d69f");
  html += R"rawliteral(;">)rawliteral";
  html += displayStatus;
  html += R"rawliteral(</div>
      </div>
    </div>

    <div class="panel summary-bar">
      <div>
        <div class="eyebrow">Available</div>
        <div style="margin-top: 6px;">
          <span class="mono" style="font-size: 26px; font-weight: 600; color: )rawliteral";
  html += (avail > 0 ? "#33d69f" : "#f4415c");
  html += R"rawliteral(;">)rawliteral";
  html += String(avail);
  html += R"rawliteral(</span><span class="mono" style="font-size: 15px; color: #5b7280;"> / 4</span>
        </div>
      </div>
      <div>
        <div class="eyebrow">Gate Status</div>
        <div style="margin-top: 10px; font-size: 14px;" class="mono">
          <b>)rawliteral";
  html += (currentServoAngle == GATE_OPEN_ANGLE ? "OPEN" : "CLOSED");
  html += R"rawliteral(</b> <span style="font-size: 11px; color: #5b7280;">()rawliteral";
  html += (gateMode == GATE_AUTO ? "AUTO" : "MANUAL");
  html += R"rawliteral()</span>
        </div>
      </div>
      <div>
        <div class="eyebrow">Left Side</div>
        <div style="margin-top: 10px; display: flex; align-items: center; gap: 8px;" class="mono">
          <span class="dot )rawliteral";
  html += (leftAvail ? "dot-green" : "dot-red");
  html += R"rawliteral("></span>
          <span>)rawliteral";
  html += (leftAvail ? "Space Free" : "Full / Blocked");
  html += R"rawliteral(</span>
        </div>
      </div>
      <div>
        <div class="eyebrow">Right Side</div>
        <div style="margin-top: 10px; display: flex; align-items: center; gap: 8px;" class="mono">
          <span class="dot )rawliteral";
  html += (rightAvail ? "dot-green" : "dot-red");
  html += R"rawliteral("></span>
          <span>)rawliteral";
  html += (rightAvail ? "Space Free" : "Full / Blocked");
  html += R"rawliteral(</span>
        </div>
      </div>
    </div>

    <div class="main-grid">
      <div>
        <div class="panel">
          <div class="eyebrow">Slot Status & Management</div>
          <div class="slot-grid">
)rawliteral";

  for (int i = 0; i < 4; i++) {
    bool occ = slots[i].occupied;
    String ovr = slots[i].overrideState;

    String cardBorder = "#33d69f33";
    String dotClass = "dot-green";
    String statusText = "Empty";
    String textColor = "#33d69f";

    if (occ) {
      if (slots[i].isIllegal) {
        cardBorder = "#4a9eff33";
        dotClass = "dot-blue";
        statusText = "ILLEGAL ENTRY";
        textColor = "#4a9eff";
      } else {
        cardBorder = "#f4415c33";
        dotClass = "dot-red";
        statusText = "Occupied";
        textColor = "#f4415c";
      }
    } else if (ovr == "RESERVED") {
      cardBorder = "#f5a62333";
      dotClass = "dot-yellow";
      statusText = "RESERVED";
      textColor = "#f5a623";
    } else if (ovr == "MAINTENANCE") {
      cardBorder = "#8fa3ad33";
      dotClass = "dot-grey";
      statusText = "MAINTENANCE";
      textColor = "#8fa3ad";
    }

    html += "<div class='slotcard' style='border-color: " + cardBorder + "'>";
    html += "  <div>";
    html += "    <div style='display:flex; justify-content:space-between; align-items:center;'>";
    html += "      <span class='mono' style='font-size: 12px; color: #8fa3ad;'>SLOT " + String(i + 1) + " &middot; " + String(slots[i].side == 'L' ? "LEFT" : "RIGHT") + "</span>";
    html += "      <span class='dot " + dotClass + "'></span>";
    html += "    </div>";
    html += "    <div class='mono' style='font-size: 15px; margin-top: 10px; color: " + textColor + "; font-weight: 600;'>" + statusText + "</div>";
    html += "  </div>";
    
    html += "  <div style='margin-top: 14px; display:flex; gap:6px;'>";
    if (ovr == "") {
      html += "    <a class='mini-btn' href='/slot?id=" + String(i) + "&cmd=reserve'>RESERVE</a>";
      html += "    <a class='mini-btn' href='/slot?id=" + String(i) + "&cmd=maint'>MAINTAIN</a>";
    } else {
      html += "    <a class='mini-btn' href='/slot?id=" + String(i) + "&cmd=clear' style='color:#33d69f; border-color:#2a6f5c;'>CLEAR OVERRIDE</a>";
    }
    html += "  </div>";
    html += "</div>";
  }

  html += R"rawliteral(
          </div>
        </div>
      </div>

      <div>
        <div class="panel">
          <div class="eyebrow" style="margin-bottom: 14px;">Manual Gate Controls</div>
          <div style="display: flex; gap: 10px; flex-direction: column;">
            <a href="/gate?cmd=open"><button class="ctl primary" style="width: 100%;">OPEN GATE</button></a>
            <a href="/gate?cmd=close"><button class="ctl danger" style="width: 100%;">CLOSE GATE</button></a>
            <a href="/gate?cmd=auto"><button class="ctl" style="width: 100%;">ENABLE AUTO MODE</button></a>
          </div>
        </div>
      </div>
    </div>
  </div>
)rawliteral";

  // Additive-only "Recent Activity" panel — does not alter any existing
  // panel above. Falls back to a placeholder line if nothing logged yet.
  html += R"rawliteral(
  <div class="wrap">
    <div class="panel">
      <div class="eyebrow" style="margin-bottom: 14px;">Recent Activity</div>
      <div class="mono" style="font-size: 12px; line-height: 1.8; color: #9bb0bd;">
)rawliteral";

  if (eventLogCount == 0) {
    html += "<div style='color:#5b7280;'>No recent activity.</div>";
  } else {
    int shown = 0;
    int maxShow = 10;
    for (int k = 0; k < eventLogCount && shown < maxShow; k++) {
      int idx = (eventLogHead - 1 - k + EVENT_LOG_SIZE) % EVENT_LOG_SIZE;
      unsigned long agoSec = (millis() - eventLog[idx].timestampMs) / 1000;
      html += "<div>[" + String(agoSec) + "s ago] " + eventLog[idx].description + "</div>";
      shown++;
    }
  }

  html += R"rawliteral(
      </div>
    </div>
  </div>
  <script>
    setInterval(() => { fetch('/api/status').then(r => r.json()).then(data => { if(data.update) location.reload(); }); }, 2500);
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleGateCommand() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    if (cmd == "open") {
      gateMode = GATE_MANUAL;
      manualCommandAt = millis();
      setServoAngle(GATE_OPEN_ANGLE);
      webStatusMessage = "GATE OPEN (MANUAL)";
      logEvent("Manual GATE OPEN command");
    } else if (cmd == "close") {
      gateMode = GATE_MANUAL;
      manualCommandAt = millis();
      setServoAngle(GATE_CLOSED_ANGLE);
      webStatusMessage = "GATE CLOSED (MANUAL)";
      logEvent("Manual GATE CLOSE command");
    } else if (cmd == "auto") {
      gateMode = GATE_AUTO;
      manualCommandAt = 0;
      if (gateState == GATE_IDLE && currentServoAngle != GATE_CLOSED_ANGLE) {
        setServoAngle(GATE_CLOSED_ANGLE);
      }
      webStatusMessage = "AUTO MODE ENABLED";
      logEvent("Returned to AUTO mode (manual)");
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSlotCommand() {
  if (server.hasArg("id") && server.hasArg("cmd")) {
    int slotId = server.arg("id").toInt();
    String cmd = server.arg("cmd");

    if (slotId >= 0 && slotId < 4) {
      if (cmd == "reserve") {
        slots[slotId].overrideState = "RESERVED";
      } else if (cmd == "maint") {
        slots[slotId].overrideState = "MAINTENANCE";
      } else if (cmd == "clear") {
        slots[slotId].overrideState = "";
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleApiStatus() {
  String json = "{\"update\":true}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);

  esp_task_wdt_config_t wdt_config = {
      .timeout_ms = WDT_TIMEOUT_SEC * 1000,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);

  pinMode(PIN_ULTRA_TRIG, OUTPUT);
  pinMode(PIN_ULTRA_ECHO, INPUT);
  pinMode(PIN_IR_INNER, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_LEFT, OUTPUT);
  pinMode(PIN_LED_RIGHT, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(PIN_ULTRA_ECHO), ultrasonicEchoISR, CHANGE);

  for (int i = 0; i < 4; i++) {
    pinMode(slots[i].irPin, INPUT);
  }

  gateServo.setPeriodHertz(50);
  setServoAngle(GATE_CLOSED_ANGLE);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  
  showBootSplash();
  bootSplashStartTime = millis();

  setupHotspot();
  lastPageSwitch = millis();
}

void loop() {
  esp_task_wdt_reset();
  handleServoDetach();

  if (!bootSplashDone) {
    if (millis() - bootSplashStartTime >= BOOT_SPLASH_DURATION) {
      bootSplashDone = true;
    } else {
      return;
    }
  }

  checkManualAutoRevert();
  updateUltrasonicNonBlocking();

  server.handleClient();

  readSlotSensors();
  handleGateLogic();
  updateIndicatorLEDs();
  updateBuzzer();
  updateOled();
}

void setupHotspot() {
  WiFi.softAP(AP_SSID, AP_PASS);
  server.on("/", handleRoot);
  server.on("/gate", handleGateCommand);
  server.on("/slot", handleSlotCommand);
  server.on("/api/status", handleApiStatus);
  server.begin();
}

// Non-blocking ultrasonic state machine — call every loop() iteration.
// Fires a trigger pulse on a timed interval and lets the ISR
// (ultrasonicEchoISR) capture the echo width in the background.
// outerCarPresentFlag is updated once a fresh reading completes.
void updateUltrasonicNonBlocking() {
  unsigned long nowUs = micros();
  unsigned long nowMs = millis();

  switch (ultraState) {
    case ULTRA_IDLE:
      if (nowMs - ultraLastTriggerAtMs >= ULTRA_TRIGGER_INTERVAL_MS) {
        digitalWrite(PIN_ULTRA_TRIG, LOW);
        ultraStateChangedAtUs = nowUs;
        ultraState = ULTRA_TRIG_LOW_SETTLE;
      }
      break;

    case ULTRA_TRIG_LOW_SETTLE:
      if (nowUs - ultraStateChangedAtUs >= 3) {
        digitalWrite(PIN_ULTRA_TRIG, HIGH);
        ultraStateChangedAtUs = nowUs;
        ultraEchoCaptured = false;
        ultraState = ULTRA_TRIG_PULSE;
      }
      break;

    case ULTRA_TRIG_PULSE:
      if (nowUs - ultraStateChangedAtUs >= 10) {
        digitalWrite(PIN_ULTRA_TRIG, LOW);
        ultraStateChangedAtUs = nowUs;
        ultraLastTriggerAtMs = nowMs;
        ultraState = ULTRA_WAIT_ECHO;
      }
      break;

    case ULTRA_WAIT_ECHO:
      if (ultraEchoCaptured) {
        long distCm = (long)(ultraEchoDurationUs * 0.0343 / 2);
        if (distCm < 400) {
          ultraDistanceSamples[ultraSampleTotalCount % ULTRA_SAMPLES] = distCm;
          ultraSampleTotalCount++;
          long sum = 0;
          int cnt = min(ultraSampleTotalCount, ULTRA_SAMPLES);
          for (int k = 0; k < cnt; k++) sum += ultraDistanceSamples[k];
          ultraAveragedDistanceCm = sum / cnt;
          outerCarPresentFlag = (ultraAveragedDistanceCm <= ULTRA_THRESHOLD_CM);
        }
        ultraState = ULTRA_IDLE;
      } else if (nowUs - ultraStateChangedAtUs >= ULTRA_ECHO_TIMEOUT_US) {
        // No echo returned in time — treat as "nothing detected"
        outerCarPresentFlag = false;
        ultraState = ULTRA_IDLE;
      }
      break;
  }
}

bool outerCarPresent() {
  return outerCarPresentFlag;
}

bool innerCarPresent() {
  return digitalRead(PIN_IR_INNER) == LOW;
}

void readSlotSensors() {
  for (int i = 0; i < 4; i++) {
    bool rawReading = (digitalRead(slots[i].irPin) == LOW);

    if (rawReading != slots[i].rawState) {
      slots[i].rawState = rawReading;
      slots[i].lastStateChange = millis();
    }

    // 1. Slot Occupied Logic
    if (slots[i].rawState && !slots[i].occupied) {
      if ((millis() - slots[i].lastStateChange) > 200) {
        slots[i].prevOccupied = slots[i].occupied;
        slots[i].occupied = true;

        unsigned long sinceVacated = millis() - slots[i].lastVacatedAt;
        bool isRepark = (slots[i].lastVacatedAt != 0) &&
                         (sinceVacated >= MIN_REPARK_GAP_MS) &&
                         (sinceVacated < REPARK_GRACE_MS);

        unsigned long sinceIllegalVacated = millis() - lastIllegalVacatedAt;
        bool isIllegalCorrection = (lastIllegalVacatedAt != 0) &&
                         (sinceIllegalVacated >= MIN_REPARK_GAP_MS) &&
                         (sinceIllegalVacated < ILLEGAL_CORRECTION_GRACE_MS) &&
                         (slots[i].overrideState == "");

        if (pendingValidEntries > 0) {
          // Only a confirmed inner-sensor crossing (i.e. the car actually
          // passed through the gate) counts as a legal entry. Relying on
          // "gate happens to be open" alone (the old seqStage/gateState
          // check) let anything placed in a free slot during that window
          // slip through as legal, even without crossing the inner sensor.
          pendingValidEntries--;
          
          carsInside++;
          if (carsInside > TOTAL_CAPACITY) carsInside = TOTAL_CAPACITY;

          if (slots[i].overrideState != "") {
            // Car entered legitimately through the gate, but parked in a
            // RESERVED/MAINTENANCE slot instead of the suggested one.
            // It still counts toward capacity (it's physically inside),
            // but the slot violation is flagged and alerted.
            handleIllegalDetection(i, "PARKED IN " + slots[i].overrideState + " SLOT!");
          } else {
            slots[i].isIllegal = false; 
            showThanksSlotPage = true;
            thanksShownAt = millis();
            logEvent("Car parked in SLOT " + String(i + 1));
          }
        } 
        else if (isRepark) {
          // Same car repositioned within its own slot — not illegal,
          // carsInside is untouched because it never actually left.
          slots[i].isIllegal = false;
          slots[i].lastVacatedAt = 0;
        }
        else if (isIllegalCorrection) {
          // A previously-flagged illegal car moved to a different, valid
          // slot shortly after its alert. Treated as a self-correction:
          // no new buzzer/OLED alert, carsInside untouched (it was either
          // already counted, or never counted — either way it stays as-is).
          // The original violation remains permanently in the event log.
          slots[i].isIllegal = false;
          lastIllegalVacatedAt = 0;   // consume the grace window
          logEvent("Corrected — re-parked in SLOT " + String(i + 1));
        }
        else {
          handleIllegalDetection(i, "ILLEGAL ENTRY DETECTED!");
        }
      }
    }

    // 2. Slot Vacated Logic (Instant detection without lag)
    if (!slots[i].rawState && slots[i].occupied) {
      if ((millis() - slots[i].lastStateChange) > 200) {
        slots[i].prevOccupied = slots[i].occupied;
        slots[i].occupied = false; 
        slots[i].lastVacatedAt = millis();

        bool wasIllegal = slots[i].isIllegal;   // capture BEFORE resetting
        slots[i].isIllegal = false; 

        if (!wasIllegal) {
          // Legitimate parked car leaving — normal exit flow
          leavingSlotIndex = i; 

          if (carsInside > 0) {
            slotVacated = true; 
          }
          
          showExitingPage = true;
          exitingShownAt = millis();
        } else {
          // Illegally-parked car leaving its slot — no "Exiting" page,
          // no slotVacated/gate trigger. Mark the correction-grace window
          // so if it re-parks in a valid slot shortly after, it's treated
          // as a self-correction, not a fresh illegal entry.
          lastIllegalVacatedAt = millis();
        }
      }
    }
  }
}

int findFreeSlot() {
  for (int i = 0; i < 4; i++) {
    if (!slots[i].occupied && slots[i].overrideState == "") return i;
  }
  return -1;
}

void handleGateLogic() {
  bool full = isParkingFullyBlocked(); 

  switch (gateState) {
    case GATE_IDLE: {
      if (gateMode == GATE_MANUAL) return;

      bool outer = outerCarPresent();
      bool inner = innerCarPresent();

      if (outer && !full) {
        assignedSlotForActivity = findFreeSlot();
        seqStage = SEQ_ENTRY;
        showFullAlert = false;
        
        setServoAngle(GATE_OPEN_ANGLE);
        gateOpenedAt = millis();
        gateState = GATE_OPEN_WAIT;
        
        webStatusMessage = "GATE OPENING (ENTRY)";
        triggerBuzzer(BUZ_SHORT);
      }
      else if (inner && slotVacated) {
        seqStage = SEQ_EXIT;
        showFullAlert = false;
        
        setServoAngle(GATE_OPEN_ANGLE);
        gateOpenedAt = millis();
        gateState = GATE_OPEN_WAIT;
        
        webStatusMessage = "GATE OPENING (EXIT)";
        triggerBuzzer(BUZ_DOUBLE);
      }
      else if (outer && full) {
        webStatusMessage = "PARKING FULL!";
        showFullAlert = true; 
        startContinuousBuzzer();
      } else {
        webStatusMessage = "SYSTEM READY / IDLE";
        showFullAlert = false;
        stopContinuousBuzzer();
      }
      break;
    }

    case GATE_OPEN_WAIT: {
      bool targetCrossed = false;

      if (seqStage == SEQ_ENTRY) {
        if (innerCarPresent()) {
          targetCrossed = true;
          pendingValidEntries++;
        }
      } 
      else if (seqStage == SEQ_EXIT) {
        if (outerCarPresent()) targetCrossed = true;
      }

      if (targetCrossed) {
        passTimeAt = millis();
        webStatusMessage = "CAR PASSED. WAITING...";
        gateState = GATE_PASS_WAIT;
      } 
      else if (millis() - gateOpenedAt > GATE_TIMEOUT_MS) {
        webStatusMessage = "TIMEOUT - CLOSING";
        gateState = GATE_CLOSING;
      }
      break;
    }

    case GATE_PASS_WAIT: {
      if (millis() - passTimeAt >= GATE_PASS_HOLD_MS) {
        if (seqStage == SEQ_EXIT) {
          carsInside--;
          if (carsInside < 0) carsInside = 0;
          logEvent("Car exited (SLOT " + String(leavingSlotIndex + 1) + ")");
          slotVacated = false; 
        }

        webStatusMessage = "GATE CLOSING...";
        gateState = GATE_CLOSING;
      }
      break;
    }

    case GATE_CLOSING: {
      setServoAngle(GATE_CLOSED_ANGLE);
      gateState = GATE_IDLE;
      
      seqStage = SEQ_NONE;
      assignedSlotForActivity = -1;
      leavingSlotIndex = -1; 
      
      webStatusMessage = "SYSTEM READY / IDLE";
      break;
    }
  }
}

void updateIndicatorLEDs() {
  bool showLeft = false, showRight = false;
  if (seqStage == SEQ_ENTRY && assignedSlotForActivity >= 0 && !slots[assignedSlotForActivity].occupied) {
    if (slots[assignedSlotForActivity].side == 'L') showLeft = true; else showRight = true;
  }
  digitalWrite(PIN_LED_LEFT,  showLeft ? HIGH : LOW);
  digitalWrite(PIN_LED_RIGHT, showRight ? HIGH : LOW);
}

void triggerBuzzer(BuzzerPattern pattern) {
  buzzerPattern = pattern;
  buzzerStep = 0;
  buzzerStepAt = millis();
}

void startContinuousBuzzer() {
  if (buzzerPattern != BUZ_CONTINUOUS) {
    buzzerPattern = BUZ_CONTINUOUS;
    buzzerStep = 0;
    buzzerStepAt = millis();
  }
}

void stopContinuousBuzzer() {
  if (buzzerPattern == BUZ_CONTINUOUS) {
    buzzerPattern = BUZ_NONE;
    digitalWrite(PIN_BUZZER, LOW);
  }
}

void updateBuzzer() {
  unsigned long now = millis();
  switch (buzzerPattern) {
    case BUZ_NONE:
      digitalWrite(PIN_BUZZER, LOW);
      break;

    case BUZ_SHORT:
      if (buzzerStep == 0) {
        digitalWrite(PIN_BUZZER, HIGH);
        buzzerStep = 1; buzzerStepAt = now;
      } else if (now - buzzerStepAt > 100) {
        digitalWrite(PIN_BUZZER, LOW);
        buzzerPattern = BUZ_NONE;
      }
      break;

    case BUZ_DOUBLE: 
      if (buzzerStep == 0) {
        digitalWrite(PIN_BUZZER, HIGH); buzzerStep = 1; buzzerStepAt = now;
      } else if (buzzerStep == 1 && now - buzzerStepAt > 50) {
        digitalWrite(PIN_BUZZER, LOW); buzzerStep = 2; buzzerStepAt = now;
      } else if (buzzerStep == 2 && now - buzzerStepAt > 50) {
        digitalWrite(PIN_BUZZER, HIGH); buzzerStep = 3; buzzerStepAt = now;
      } else if (buzzerStep == 3 && now - buzzerStepAt > 50) {
        digitalWrite(PIN_BUZZER, LOW); buzzerPattern = BUZ_NONE;
      }
      break;

    case BUZ_ILLEGAL_FAST: 
      if (buzzerStep % 2 == 0) {
        digitalWrite(PIN_BUZZER, HIGH);
        if (now - buzzerStepAt > 60) {
          buzzerStep++;
          buzzerStepAt = now;
        }
      } else {
        digitalWrite(PIN_BUZZER, LOW);
        if (now - buzzerStepAt > 60) {
          buzzerStep++;
          buzzerStepAt = now;
          if (buzzerStep >= 10) { 
            buzzerPattern = BUZ_NONE;
            illegalSlotIndex = -1; 

            // If another slot's illegal entry is queued, alert it next
            int nextSlot;
            if (illegalQueuePop(nextSlot)) {
              illegalSlotIndex = nextSlot;
              buzzerPattern = BUZ_ILLEGAL_FAST;
              buzzerStep = 0;
              buzzerStepAt = now;
            }
          }
        }
      }
      break;

    case BUZ_CONTINUOUS:
      if (now - buzzerStepAt > 150) {
        buzzerStep = !buzzerStep;
        buzzerStepAt = now;
        digitalWrite(PIN_BUZZER, buzzerStep ? HIGH : LOW);
      }
      break;
  }
}

void showBootSplash() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 10); display.println("IoT Based Automated");
  display.setCursor(10, 22); display.println("Car Parking System");
  display.setCursor(4, 40);  display.println("with Gate Contrlled");
  display.setCursor(4, 54);  display.println("& Cloud Monitoring");
  display.display();
}

void updateOled() {
  if (buzzerPattern == BUZ_ILLEGAL_FAST && illegalSlotIndex >= 0) {
    currentPage = PAGE_ILLEGAL;
  }
  else if (showThanksSlotPage) {
    currentPage = PAGE_THANKS_SLOT;
    if (millis() - thanksShownAt > THANKS_SHOW_MS) {
      showThanksSlotPage = false;
      currentPage = PAGE_HOME;
      lastPageSwitch = millis();
    }
  }
  else if (showExitingPage) {
    currentPage = PAGE_EXITING;
    if (millis() - exitingShownAt > EXITING_SHOW_MS) {
      showExitingPage = false;
      currentPage = PAGE_HOME;
      lastPageSwitch = millis();
    }
  }
  else if (showFullAlert) {
    currentPage = PAGE_FULL;
  }
  else if (seqStage == SEQ_EXIT && gateState == GATE_PASS_WAIT) {
    currentPage = PAGE_THANKS_EXIT; 
  }
  else if (gateState == GATE_OPEN_WAIT || gateState == GATE_PASS_WAIT || gateState == GATE_CLOSING) {
    if (seqStage == SEQ_ENTRY) {
      currentPage = PAGE_WELCOME;
    }
  }
  else {
    if (millis() - lastPageSwitch > IDLE_ROTATE_MS) {
      lastPageSwitch = millis();
      if (currentPage == PAGE_HOME) currentPage = PAGE_SYSTEM;
      else currentPage = PAGE_HOME;
    }
  }

  drawPage(currentPage);
}

String getSlotOledStatus(int idx) {
  if (slots[idx].occupied) return "OCC";
  if (slots[idx].overrideState == "RESERVED") return "RESV";
  if (slots[idx].overrideState == "MAINTENANCE") return "MAIN";
  return "FREE";
}

void drawPage(OledPage page) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  switch (page) {
    case PAGE_HOME: {
      display.setTextSize(1);
      display.setCursor(0, 0);  display.println("PARKING STATUS");
      display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
      
      display.setCursor(0, 16);
      display.print("AVAIL:"); display.print(getAvailableSpaceCount()); display.print("/4");
      
      display.setCursor(68, 16);
      display.print("CARS:"); display.print(getPhysicalCarCount());

      display.setCursor(0, 36);
      display.print("S1:"); display.print(getSlotOledStatus(0));
      display.setCursor(64, 36);
      display.print("S2:"); display.print(getSlotOledStatus(1));

      display.setCursor(0, 50);
      display.print("S3:"); display.print(getSlotOledStatus(2));
      display.setCursor(64, 50);
      display.print("S4:"); display.print(getSlotOledStatus(3));
      break;
    }

    case PAGE_SYSTEM: {
      display.setTextSize(1);
      display.setCursor(0, 0); display.println("GATE STATUS");
      display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
      display.setCursor(0, 16);
      display.print("MODE : "); display.println(gateMode == GATE_AUTO ? "AUTO" : "MANUAL");
      display.setCursor(0, 28);
      display.print("GATE : "); display.println(currentServoAngle == GATE_OPEN_ANGLE ? "OPEN" : "CLOSED");
      display.setCursor(0, 40);
      display.print("IP   : 192.168.4.1");
      display.setCursor(0, 52);
      display.print("SSID : "); display.println(AP_SSID);
      break;
    }

    case PAGE_WELCOME: {
      display.setTextSize(2);
      display.setCursor(20, 4); display.println("WELCOME");
      display.setTextSize(1);
      display.setCursor(28, 26); display.println("Please enter");

      display.setTextSize(2);
      if (assignedSlotForActivity >= 0) {
        display.setCursor(26, 44);
        display.print("SLOT "); display.println(assignedSlotForActivity + 1);
      } else {
        display.setCursor(18, 44); display.println("PARKING");
      }
      break;
    }

    case PAGE_EXITING: {
      display.setTextSize(1);
      display.setCursor(28, 10); display.println("Exiting from");
      display.setTextSize(2);
      if (leavingSlotIndex >= 0) {
        display.setCursor(26, 36);
        display.print("SLOT "); display.println(leavingSlotIndex + 1);
      } else {
        display.setCursor(22, 36); display.println("PARKING");
      }
      break;
    }

    case PAGE_THANKS_EXIT: {
      display.setTextSize(2);
      display.setCursor(28, 10); display.println("THANKS");
      display.setTextSize(1);
      display.setCursor(8, 38); display.println("Have a safe journey");
      break;
    }

    case PAGE_THANKS_SLOT: {
      display.setTextSize(2);
      display.setCursor(28, 12); display.println("THANKS");
      display.setTextSize(1);
      display.setCursor(28, 38); display.println("for parking");
      break;
    }

    case PAGE_ILLEGAL: {
      display.setTextSize(1);
      display.setCursor(16, 12); display.println("Illegal entry in");
      display.setTextSize(2);
      if (illegalSlotIndex >= 0) {
        display.setCursor(26, 34);
        display.print("SLOT "); display.println(illegalSlotIndex + 1);
      } else {
        display.setCursor(18, 34); display.println("PARKING");
      }
      break;
    }

    case PAGE_FULL: {
      display.invertDisplay(true);
      display.setTextSize(2);
      display.setCursor(18, 18); display.println("PARKING");
      display.setCursor(30, 40); display.println("FULL!");
      break;
    }
  }

  if (page != PAGE_FULL) display.invertDisplay(false);
  display.display();
}
