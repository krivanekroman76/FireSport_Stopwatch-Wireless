/*
 * Base Station v6.12 - ESP32-CAM stopwatch base   (use together with Endpoint v6.12 and the protocol.h that comes with it)
 *
 * RACE LAYOUT: 1..4 lanes. Display 1 = lane 1 (L), display 2 = lane 2 (P). Lanes 3-4 are timed and reported to the PC only.
 *   Segments per lane 1..9 (only with 1 lane for now). The lane timer stops at the LAST segment, earlier segments are splits.
 *
 * BATTERY: ADC2 (IO13) cannot be read while Wi-Fi runs. The base measures its own battery at boot and on every RESTART
 *   (Wi-Fi is switched off for ~0.2 s, never while a run is in progress) and asks the endpoints to do the same
 *   (BATT_REQ with payload[0] = 1). Percentages are shown on the displays while RESTART is held.
 *
 * BUTTONS: GREEN = START (IO4), RED = RESTART (IO2), BLUE = IO0.
 *   START: starts the stopwatches. A second press within 10 s of the start = false start (run aborted, displays show FALS).
 *   RESTART: reset (immediately when the timers are idle, after 0.8 s when a run is in progress). While it is held the
 *            battery of the base and of all endpoints is shown (see COMPACT BATTERY VIEW).
 *   IO0: immediate restart of the ESP32 (hardware interrupt, also works when the code is stuck in a loop). Never hold IO0
 *        while powering up (the ESP32 would enter the download mode).
 *
 * COMPACT BATTERY VIEW (16 digits = left display + right display, one continuous strip):
 *   devices in this order: base, endpoints by lane then segment (a starter goes last, not implemented yet).
 *   up to 5 devices: 3 digits each  [state][tens][ones]  state: blank = OK, L = LOW, C = CRITICAL (blinks); 100 % = "100"
 *   6 to 8 devices:  2 digits each  percent (99 = 99..100 %), dot after the last digit = LOW, blinking = CRITICAL
 *   9 to 16 devices: 1 digit each   tens of percent (9 = 90..100 %), dot = LOW, blinking = CRITICAL
 *   "-" = no value / no answer.
 *
 * SETUP (always the same linear flow; hold START while powering up, or automatically on the first run):
 *   LANES (1-4) -> SEGS (only with 1 lane) -> PAIR (every slot, lane by lane) -> HIT -> LEN -> HORN -> LIGHT -> FLASH -> OUT
 *   GREEN = confirm / next, RED = change the value, BLUE = back (held 2 s = restart). GREEN held = jump ahead
 *   (from PAIR to HIT, from the settings to OUT). Left display = name, right display = value, the last digit of the
 *   left display = number of endpoints heard in the last 6 s (its dot flashes on every received packet).
 *   PAIR shows the slot "lane-segment"; trigger the sensor of the endpoint that belongs there. A paired slot shows the
 *   battery of its endpoint; triggering another endpoint re-pairs the slot, RED frees it, GREEN keeps it / skips an empty slot.
 *   HIT = endpoint buzzer on hit 0/1, LEN = endpoint beep length (ms), HORN = base buzzer 0/1, LIGHT = brightness 1-15,
 *   FLASH = after FINISH the display of the 0 = nobody, F = fastest (first), L = slowest (last) lane blinks.
 *
 * Commands from the PC (UART, one per line, 115200 baud):
 *   BATT? | HEALTH   health check now      LIST   endpoint registry      STATUS   print CONFIG
 *   BUZZER <0|1>     endpoint buzzer       BEEP <ms>   beep length 30..1000
 *   LANES <1-4> | SEGMENTS <1-9> | BLINK <0|1|2> | SETUPDONE
 *   ASSIGN <ep> <lane> <seg> | LEARN <lane> <seg> | FORGET <ep> | FORGET ALL
 *
 * Lines for the PC app (always printed, also with DEBUG 0), all start with "[PYTHON]":
 *   EVENT:START | EVENT:RESET | EVENT:FALSE_START MS:<ms> | STOP:<lane> TIME:<ms> | SPLIT:<lane> SEG:<s> TIME:<ms>
 *   FINISH TIME1:<ms> TIME2:<ms> TIME3:<ms> TIME4:<ms>
 *   BATT EP:<n> LANE:<l> SEG:<s> MV:<mV> V:<x.xx> PCT:<0-100|-1> STATE:<O|L|C|E> STALE:<0|1> RSSI:<dBm>
 *   BATT BASE MV:<mV> V:<x.xx> PCT:<0-100|-1> STATE:<O|L|C|E> STALE:<0|1>
 *   HEALTH:OK COUNT:<n> | HEALTH:MISSING EP:<n> LANE:<l> SEG:<s> | HEALTH:NO_ENDPOINTS
 *   BOOT REASON:<..> FIRST_RUN:<0|1> LANES:<n> SEGMENTS:<n> ASSIGNED:<a> EXPECTED:<e> | SETUP:INCOMPLETE ... | SETUP:ENTER | SETUP:EXIT
 *   CONFIG BUZZER:<0|1> BEEP_MS:<ms> BASE_BUZZER:<0|1> BRIGHTNESS:<1-15> LANES:<n> SEGMENTS:<n> BLINK:<0-2> CONFIGURED:<0|1>
 *   ENDPOINT:NEW EP:<n> MAC:<mac> | HELLO EP:<n> MAC:<mac> LANE:<l> SEG:<s> | EVENT:UNASSIGNED EP:<n>
 *   LIST EP:<n> MAC:<mac> LANE:<l> SEG:<s> MV:<mV> V:<x.xx> PCT:<p> STATE:<c> | LIST:EMPTY
 *   ASSIGNED EP:<n> LANE:<l> SEG:<s> | UNASSIGNED EP:<n> | LEARN:WAITING | LEARN:TIMEOUT | OK | ERROR:<reason>
 *
 * Timing notes: the ESP-NOW receive callback runs in the Wi-Fi task, NOT in loop(). The stop time is taken with
 * millis() inside the callback, so display refresh, serial parsing and button handling cannot delay a measurement.
 * Nothing in the callback touches the displays. The Wi-Fi is only switched off while no run is in progress.
 */
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <Preferences.h>
#include "LedController.hpp"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "config.h"      // SECRET_KEY and broadcastMAC (same on all devices)
#include "protocol.h"    // shared with the endpoints

// ====================================================================
// --- MAIN DEBUG SWITCH (1 = on, 0 = off) ---
// Note: [PYTHON] lines are always printed, also with DEBUG 0.
// ====================================================================
#define DEBUG 1

#if DEBUG
  #define DEBUG_PRINT(...)   Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
  #define DEBUG_PRINTF(...)  Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
  #define DEBUG_PRINTF(...)
#endif

// ====================================================================
// SETTINGS
// ====================================================================
#define MAX_LANES      4
#define MAX_SEGMENTS   9
#define MAX_ENDPOINTS  12
#define DISPLAY_LANES  2     // lanes that have a display on the base

const uint8_t       DEFAULT_BUZZER_ON_HIT = 0;        // endpoint buzzer on hit
const bool          DEFAULT_BASE_BUZZER   = true;     // base buzzer
const uint16_t      DEFAULT_BEEP_MS       = 120;      // endpoint beep length on hit
const uint8_t       DEFAULT_BRIGHTNESS    = 15;       // 1..15
const uint8_t       DEFAULT_LANES         = 2;        // 1..4
const uint8_t       DEFAULT_SEGMENTS      = 1;        // 1..9 (more than 1 only with 1 lane)
const uint8_t       DEFAULT_BLINK_MODE    = 0;        // after FINISH: 0 = none, 1 = fastest lane blinks, 2 = slowest lane blinks

const unsigned long HEALTH_POLL_MS        = 300000;   // periodic health check (0 = off), only while idle, without Wi-Fi pause
const unsigned long HEALTH_WINDOW_MS      = 2500;     // how long endpoints get to answer (they measure with the radio off)
const unsigned long CONFIG_REBROADCAST_MS = 60000;    // re-send settings to all endpoints (lost broadcasts)
const unsigned long LEARN_TIMEOUT_MS      = 30000;    // PC command LEARN

const unsigned long RESET_HOLD_RUNNING_MS = 800;      // RESTART must be held this long to reset WHILE a run is in progress
const unsigned long RESET_HOLD_IDLE_MS    = 0;        // ... and this long when the timers are idle (0 = reset right on press)
const unsigned long FALSE_START_WINDOW_MS = 10000;    // second press of START within this time after the start = false start (0 = off)
const unsigned long START_RELEASE_MS      = 40;       // START must be LOW this long before a second press counts
const unsigned long START_LOCKOUT_MS      = 1500;     // START is locked this long after a false start (and while it is held)
const unsigned long STATUS_REDRAW_MS      = 200;      // battery view: redraw period
const unsigned long MARKER_REFRESH_MS     = 200;      // status markers: refresh period
const unsigned long MARKER_BLINK_MS       = 400;      // status markers / critical battery: blink half-period
const unsigned long BLINK_HALF_PERIOD_MS  = 500;      // best / worst lane blink after FINISH

// Base battery (2S Li-ion pack, divider 100k top + 47k bottom, measured on the raw battery side before the 5 V converter)
const float    BATT_RATIO         = (100.0f + 47.0f) / 47.0f;  // use the measured resistor values
const float    BATT_CAL           = 1.000f;                    // fine-tune: real voltage / displayed voltage (measure with a multimeter)
const uint8_t  BATT_SAMPLES       = 16;
const uint16_t BATT_LOW_MV        = 7400;       // warning
const uint16_t BATT_CRIT_MV       = 7000;       // critical
const uint16_t BATT_HYST_MV       = 200;        // hysteresis when recovering
const uint16_t BATT_MIN_VALID_MV  = 4500;       // outside min..max = measurement fault
const uint16_t BATT_MAX_VALID_MV  = 8800;
const uint8_t  BATT_CONFIRM       = 3;          // consecutive worse readings before the state degrades

// ====================================================================
// GPIO PIN MAPPING ON THE ESP32-CAM
// ====================================================================
#define DIN             12  // MAX7219 data in. Strapping pin: the display module must not pull it HIGH at boot
#define CS              15  // MAX7219 chip select / load
#define CLK             14  // MAX7219 clock
#define RESTART_PIN     2   // RED button (to 3V3). Held at power-up = OTA service mode
#define START_PIN       4   // GREEN button (to 3V3). Also drives the on-board flash LED. Held at power-up = setup
#define SOFT_RESET_PIN  0   // BLUE button (to GND). Interrupt: immediate restart. Never hold it while powering up
#define BUZZER_PIN      16  // NPN driver of the buzzer, active HIGH (needs a pull-down on the NPN base)
#define BATT_PIN        13  // battery voltage divider, ADC2_CH4

// ====================================================================
// STATE
// ====================================================================
// Timers (written from the Wi-Fi task and read in loop())
volatile unsigned long elapsedTime[MAX_LANES] = {0, 0, 0, 0};
volatile unsigned long startTime[MAX_LANES] = {0, 0, 0, 0};
volatile bool displayDirty[DISPLAY_LANES] = {false, false};   // set after a stop: loop() redraws the final time
volatile unsigned long splitMs[MAX_LANES][MAX_SEGMENTS];      // split times of the current run (0 = not recorded)
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

bool anyRunning() {
    for (int i = 0; i < MAX_LANES; i++) {
        if (startTime[i] != 0) return true;
    }
    return false;
}

// RESTART button (hold-to-reset)
bool restartHeld = false;
bool restartDone = false;
unsigned long restartSince = 0;
unsigned long restartLastHigh = 0;
const unsigned long RESTART_RELEASE_DEBOUNCE_MS = 50;

// START button / false start
unsigned long runStartedAt = 0;      // millis() stamp of the current run
bool startArmed = false;             // START was released after the run began -> a new press counts as a false start
unsigned long startLastHigh = 0;
bool startLocked = false;            // locked after a false start until START has been released for START_LOCKOUT_MS
unsigned long startLockUntil = 0;
bool falseStart = false;             // the displays show FALS instead of the time

// Settings (saved in flash)
volatile uint8_t laneCount = DEFAULT_LANES;
volatile uint8_t segCount = DEFAULT_SEGMENTS;
uint8_t blinkMode = DEFAULT_BLINK_MODE;
volatile uint16_t beepMs = DEFAULT_BEEP_MS;
bool baseBuzzerOn = DEFAULT_BASE_BUZZER;
uint8_t displayIntensity = DEFAULT_BRIGHTNESS;
volatile bool buzzerOnHit = false;
bool configured = false;             // the setup has been left through OUT at least once
bool firstRun = false;
volatile int8_t blinkLane = -1;      // display that blinks after FINISH (-1 = none)
bool blinkShown = true;

// MAX7219 displays (2 modules with 8 digits each)
LedController<2, 8> lc;
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 10; // display refresh period (ms)

WebServer server(80);
bool otaMode = false;

const char* serverIndex =
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<h2>Stopwatch ESP32-CAM - OTA update</h2>"
  "<input type='file' name='update'>"
  "<input type='submit' value='Upload new firmware (.bin)'>"
  "</form>";

// Endpoint registry
struct Endpoint {
    uint8_t  mac[6];
    uint8_t  lane;       // 0 = unassigned, 1..4
    uint8_t  segment;    // 0 = unassigned, 1..9
    uint16_t battMv;     // last reported battery voltage (0 = nothing yet)
    uint8_t  battFlags;  // last reported BATT_FLAG_* bits
    bool     responded;  // answered the current health check
    bool     missing;    // did not answer the last finished health check (cleared by any report)
    unsigned long heardMs; // millis() of the last packet from this endpoint (0 = never)
    int8_t   rssi;       // signal strength of the last packet in dBm
};

struct StoredEndpoint {  // what is saved in flash
    uint8_t mac[6];
    uint8_t lane;
    uint8_t segment;
};

Endpoint registry[MAX_ENDPOINTS];
volatile uint8_t endpointCount = 0;
volatile bool registryDirty = false;      // registry or settings changed -> save to flash when idle
portMUX_TYPE regMux = portMUX_INITIALIZER_UNLOCKED;
Preferences prefs;

// LEARN mode (PC command and the PAIR step of the setup)
volatile uint8_t learnLane = 0;
volatile uint8_t learnSeg = 0;
volatile unsigned long learnUntil = 0;
volatile int learnAssignedIdx = -1;      // endpoint index assigned by the last LEARN
volatile unsigned long lastHeardMs = 0;  // millis() of the last valid packet from any endpoint (link indicator)

// Health check
bool healthActive = false;
bool healthManual = false;     // started by the operator (RESTART / PC) -> also beeps when everything is OK
unsigned long healthStart = 0;
unsigned long lastHealthPoll = 0;
unsigned long lastConfigBroadcast = 0;
unsigned long lastBattReqMs = 0;

// Battery view (shown while RESTART is held after the reset)
bool statusView = false;
unsigned long statusLastDraw = 0;
bool markersInvalid = true;

// Setup
#define STEP_LANES  1
#define STEP_SEGS   2
#define STEP_PAIR   3
#define STEP_HIT    4
#define STEP_LEN    5
#define STEP_HORN   6
#define STEP_LIGHT  7
#define STEP_FLASH  8
#define STEP_OUT    9
bool setupMode = false;
bool setupWaitRelease = false;
unsigned long setupIntroUntil = 0;
uint8_t setupStep = STEP_LANES;
uint8_t pairLane = 1, pairSeg = 1;   // slot shown in the PAIR step
unsigned long lastInputMs = 0;
bool setupRedraw = true;

// ====================================================================
// BATTERY PERCENTAGE (2S Li-ion pack, resting voltage -> approximate state of charge)
// ====================================================================
#define PCT_POINTS 12
const uint16_t PCT_TABLE_MV[PCT_POINTS]  = {8400, 8220, 8040, 7900, 7740, 7680, 7600, 7540, 7460, 7380, 7220, 6400};
const uint8_t  PCT_TABLE_PCT[PCT_POINTS] = { 100,   90,   80,   70,   60,   50,   40,   30,   20,   10,    5,    0};

int batteryPercent(uint16_t mv) {
    if (mv >= PCT_TABLE_MV[0]) return 100;
    for (uint8_t i = 1; i < PCT_POINTS; i++) {
        if (mv >= PCT_TABLE_MV[i]) {
            uint32_t span = PCT_TABLE_MV[i - 1] - PCT_TABLE_MV[i];
            uint32_t pos  = mv - PCT_TABLE_MV[i];
            uint32_t rise = PCT_TABLE_PCT[i - 1] - PCT_TABLE_PCT[i];
            return PCT_TABLE_PCT[i] + (int)(rise * pos / span);
        }
    }
    return 0;
}

char battStateChar(uint8_t flags) {
    if (flags & BATT_FLAG_ERROR) return 'E';
    uint8_t s = flags & BATT_FLAG_STATE_MASK;
    return (s == 0) ? 'O' : (s == 1) ? 'L' : (s == 2) ? 'C' : 'E';
}

// ====================================================================
// BUZZER (non-blocking beep sequencer)
// ====================================================================
static uint8_t  bzLeft = 0;        // remaining beeps (including the current one)
static uint16_t bzOnMs = 0, bzOffMs = 0;
static bool     bzOn = false;
static unsigned long bzUntil = 0;

void buzzerStart(uint8_t count, uint16_t onMs, uint16_t offMs) {
    if (!baseBuzzerOn || count == 0) return;
    bzLeft = count;
    bzOnMs = onMs;
    bzOffMs = offMs;
    bzOn = true;
    digitalWrite(BUZZER_PIN, HIGH);
    bzUntil = millis() + onMs;
}

void buzzerUpdate() {
    if (bzLeft == 0) return;
    if ((long)(millis() - bzUntil) < 0) return;
    if (bzOn) {
        digitalWrite(BUZZER_PIN, LOW);
        bzOn = false;
        bzLeft--;
        if (bzLeft > 0) bzUntil = millis() + bzOffMs;
    } else {
        digitalWrite(BUZZER_PIN, HIGH);
        bzOn = true;
        bzUntil = millis() + bzOnMs;
    }
}

// ====================================================================
// BASE BATTERY
// ====================================================================
#define BATT_OK   0
#define BATT_LOW  1
#define BATT_CRIT 2

struct BaseBattery {
    uint16_t  mv;
    uint8_t   state;      // BATT_OK / BATT_LOW / BATT_CRIT
    bool      valid;      // at least one valid measurement exists
    bool      fault;      // last reading was outside the plausible range (divider fault?)
    bool      stale;      // the last measurement failed, the value is older
    uint8_t   worseCnt;   // consecutive worse readings
};

BaseBattery baseBatt = {0, BATT_OK, false, false, true, 0};
bool baseBattReport = false;     // state changed -> print a BATT BASE line (when idle)
bool baseBattAlert = false;      // state got worse -> beep (when idle)

// Maps a voltage to a state with hysteresis (cur = current state).
uint8_t classify(uint16_t mv, uint8_t cur) {
    if (mv < BATT_CRIT_MV) return BATT_CRIT;
    if (mv < BATT_LOW_MV) {
        if (cur == BATT_CRIT && mv < BATT_CRIT_MV + BATT_HYST_MV) return BATT_CRIT;
        return BATT_LOW;
    }
    if (cur != BATT_OK && mv < BATT_LOW_MV + BATT_HYST_MV) {
        return (cur == BATT_CRIT) ? BATT_LOW : cur;
    }
    return BATT_OK;
}

// Converts the summed ADC readings (mV at the pin) into the battery voltage and updates the state.
void battStore(uint32_t sum, uint8_t good) {
    if (good < BATT_SAMPLES / 2) {         // ADC2 was busy -> keep the old value
        baseBatt.stale = true;
        return;
    }
    float mv = (sum / (float)good) * BATT_RATIO * BATT_CAL;
    if (mv < BATT_MIN_VALID_MV || mv > BATT_MAX_VALID_MV) {
        if (!baseBatt.fault) baseBattReport = true;
        baseBatt.fault = true;             // implausible -> report as error, never as "empty battery"
        baseBatt.stale = true;
        return;
    }
    bool first = !baseBatt.valid;
    if (baseBatt.fault) baseBattReport = true;
    baseBatt.fault = false;
    baseBatt.stale = false;
    baseBatt.valid = true;
    baseBatt.mv = (uint16_t)(mv + 0.5f);

    uint8_t want = classify(baseBatt.mv, baseBatt.state);
    if (first) {
        baseBatt.state = want;
        baseBatt.worseCnt = 0;
    } else if (want > baseBatt.state) {    // a worse state must be confirmed several times in a row
        if (++baseBatt.worseCnt >= BATT_CONFIRM) {
            baseBatt.state = want;
            baseBatt.worseCnt = 0;
            baseBattReport = true;
            baseBattAlert = true;
        }
    } else {
        if (want != baseBatt.state) baseBattReport = true;
        baseBatt.state = want;
        baseBatt.worseCnt = 0;
    }
}

// Blocking measurement (~35 ms). Only works while Wi-Fi is off: at boot BEFORE Wi-Fi starts, or via measureBaseBatteryPaused().
void battMeasureBlocking() {
    uint32_t sum = 0;
    uint8_t good = 0;
    for (uint8_t i = 0; i < BATT_SAMPLES; i++) {
        uint32_t v = analogReadMilliVolts(BATT_PIN);
        if (v > 0) { sum += v; good++; }
        delay(2);
    }
    battStore(sum, good);
}

uint8_t baseBattFlags() {
    uint8_t f = baseBatt.state & BATT_FLAG_STATE_MASK;
    if (!baseBatt.valid || baseBatt.fault) f |= BATT_FLAG_ERROR;
    if (baseBatt.stale) f |= BATT_FLAG_STALE;
    return f;
}

void printBaseBatteryLine() {
    uint8_t f = baseBattFlags();
    bool err = (f & BATT_FLAG_ERROR) != 0;
    Serial.printf("[PYTHON] BATT BASE MV:%u V:%.2f PCT:%d STATE:%c STALE:%d\n",
                  (unsigned)baseBatt.mv, baseBatt.mv / 1000.0f, err ? -1 : batteryPercent(baseBatt.mv),
                  battStateChar(f), (f & BATT_FLAG_STALE) ? 1 : 0);
}

// Boot beeps: base battery state
void startupBeep() {
    if (!baseBatt.valid)                  buzzerStart(2, 400, 200);   // measurement failed
    else if (baseBatt.state == BATT_CRIT) buzzerStart(5, 120, 120);
    else if (baseBatt.state == BATT_LOW)  buzzerStart(3, 150, 150);
    else                                  buzzerStart(1, 60, 0);      // all fine
}

// ====================================================================
// RADIO (Wi-Fi + ESP-NOW). Can be switched off for a moment so that ADC2 can be read.
// ====================================================================
void OnDataRecv(const esp_now_recv_info* sender, const uint8_t* incomingData, int len);

bool radioStart() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_peer_info_t bcastPeer = {};
    memcpy(bcastPeer.peer_addr, broadcastMAC, 6);
    bcastPeer.channel = 1;
    bcastPeer.encrypt = false;
    esp_now_add_peer(&bcastPeer);
    return true;
}

void radioStop() {
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
}

// Measures the base battery with the radio off (~0.2-0.3 s). NEVER call it while a run is in progress.
void measureBaseBatteryPaused() {
    radioStop();
    delay(20);
    battMeasureBlocking();
    if (!radioStart()) {
        DEBUG_PRINTLN("[ESP-NOW] Re-init failed -> restart");
        ESP.restart();
    }
}

// ====================================================================
// FORWARD DECLARATIONS
// ====================================================================
void stopTimer(int lane);
void resetTimer();
void displayTime(int displayNumber);
void blankTime(int displayNumber);
void startTimers();
void updateTimers();
void enterStatusView();
void exitStatusView();
void drawStatusView(unsigned long now);
void updateMarkers(unsigned long now);
void abortRun();
void recordSplit(int lane, uint8_t seg);
void showText(int d, const char* s);
void setupLoop();
void enterSetupMode();
void printBootLine();

// ====================================================================
// HELPERS: MAC, registry, flash
// ====================================================================
void macToStr(const uint8_t* mac, char* out) {   // out must hold 18 bytes
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int findEndpoint(const uint8_t* mac) {
    for (uint8_t i = 0; i < endpointCount; i++) {
        if (memcmp(registry[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

int addEndpoint(const uint8_t* mac) {
    int idx = -1;
    portENTER_CRITICAL(&regMux);
    if (endpointCount < MAX_ENDPOINTS) {
        idx = endpointCount;
        memset(&registry[idx], 0, sizeof(Endpoint));
        memcpy(registry[idx].mac, mac, 6);
        endpointCount = endpointCount + 1;
    }
    portEXIT_CRITICAL(&regMux);
    if (idx >= 0) registryDirty = true;
    return idx;
}

int findOrAddEndpoint(const uint8_t* mac) {
    int idx = findEndpoint(mac);
    if (idx < 0) {
        idx = addEndpoint(mac);
        char ms[18];
        macToStr(mac, ms);
        if (idx >= 0) Serial.printf("[PYTHON] ENDPOINT:NEW EP:%d MAC:%s\n", idx + 1, ms);
        else          Serial.printf("[PYTHON] ERROR:REGISTRY_FULL MAC:%s\n", ms);
    }
    return idx;
}

// Index of the endpoint paired to a slot, or -1
int slotEndpoint(uint8_t lane, uint8_t seg) {
    for (uint8_t i = 0; i < endpointCount; i++) {
        if (registry[i].lane == lane && registry[i].segment == seg) return i;
    }
    return -1;
}

// Battery percentage of an endpoint, -1 = no value (nothing reported, fault, or no answer)
int endpointPct(int i) {
    const Endpoint &e = registry[i];
    if (e.missing || e.battMv == 0 || (e.battFlags & BATT_FLAG_ERROR)) return -1;
    return batteryPercent(e.battMv);
}

void saveRegistry() {
    StoredEndpoint s[MAX_ENDPOINTS];
    uint8_t c = endpointCount;
    for (uint8_t i = 0; i < c; i++) {
        memcpy(s[i].mac, registry[i].mac, 6);
        s[i].lane = registry[i].lane;
        s[i].segment = registry[i].segment;
    }
    prefs.begin("stopwatch", false);
    prefs.putUChar("count", c);
    if (c > 0) prefs.putBytes("eps", s, sizeof(StoredEndpoint) * c);
    prefs.putUChar("buzzer", buzzerOnHit ? 1 : 0);
    prefs.putUShort("beepms", beepMs);
    prefs.putUChar("bbuz", baseBuzzerOn ? 1 : 0);
    prefs.putUChar("bright", displayIntensity);
    prefs.putUChar("lanes", laneCount);
    prefs.putUChar("segs", segCount);
    prefs.putUChar("blink", blinkMode);
    prefs.putUChar("cfgd", configured ? 1 : 0);
    prefs.end();
    DEBUG_PRINTLN("[FLASH] Registry and settings saved.");
}

void loadRegistry() {
    StoredEndpoint s[MAX_ENDPOINTS];
    prefs.begin("stopwatch", false);
    uint8_t c = prefs.getUChar("count", 0);
    if (c > MAX_ENDPOINTS) c = 0;
    if (c > 0 && prefs.getBytes("eps", s, sizeof(StoredEndpoint) * c) != sizeof(StoredEndpoint) * c) c = 0;
    buzzerOnHit = prefs.getUChar("buzzer", DEFAULT_BUZZER_ON_HIT) != 0;
    beepMs = prefs.getUShort("beepms", DEFAULT_BEEP_MS);
    if (beepMs < 30 || beepMs > 1000) beepMs = DEFAULT_BEEP_MS;
    baseBuzzerOn = prefs.getUChar("bbuz", DEFAULT_BASE_BUZZER ? 1 : 0) != 0;
    displayIntensity = prefs.getUChar("bright", DEFAULT_BRIGHTNESS);
    if (displayIntensity < 1 || displayIntensity > 15) displayIntensity = DEFAULT_BRIGHTNESS;
    laneCount = prefs.getUChar("lanes", DEFAULT_LANES);
    if (laneCount < 1 || laneCount > MAX_LANES) laneCount = DEFAULT_LANES;
    segCount = prefs.getUChar("segs", DEFAULT_SEGMENTS);
    if (segCount < 1 || segCount > MAX_SEGMENTS) segCount = DEFAULT_SEGMENTS;
    if (laneCount > 1) segCount = 1;
    blinkMode = prefs.getUChar("blink", DEFAULT_BLINK_MODE);
    if (blinkMode > 2) blinkMode = DEFAULT_BLINK_MODE;
    configured = prefs.getUChar("cfgd", 0) != 0;
    prefs.end();

    memset(registry, 0, sizeof(registry));
    for (uint8_t i = 0; i < c; i++) {
        memcpy(registry[i].mac, s[i].mac, 6);
        registry[i].lane = s[i].lane;
        registry[i].segment = s[i].segment;
    }
    endpointCount = c;
}

// ====================================================================
// PYTHON OUTPUT
// ====================================================================
void printBatteryLine(int idx) {
    const Endpoint &e = registry[idx];
    bool err = (e.battFlags & BATT_FLAG_ERROR) != 0 || e.battMv == 0;
    int pct = err ? -1 : batteryPercent(e.battMv);
    Serial.printf("[PYTHON] BATT EP:%d LANE:%d SEG:%d MV:%u V:%.2f PCT:%d STATE:%c STALE:%d RSSI:%d\n",
                  idx + 1, e.lane, e.segment, (unsigned)e.battMv, e.battMv / 1000.0f, pct,
                  battStateChar(e.battFlags), (e.battFlags & BATT_FLAG_STALE) ? 1 : 0, (int)e.rssi);
}

void printList() {
    if (endpointCount == 0) {
        Serial.println("[PYTHON] LIST:EMPTY");
        return;
    }
    for (uint8_t i = 0; i < endpointCount; i++) {
        const Endpoint &e = registry[i];
        char ms[18];
        macToStr(e.mac, ms);
        bool err = (e.battFlags & BATT_FLAG_ERROR) != 0 || e.battMv == 0;
        Serial.printf("[PYTHON] LIST EP:%d MAC:%s LANE:%d SEG:%d MV:%u V:%.2f PCT:%d STATE:%c\n",
                      i + 1, ms, e.lane, e.segment, (unsigned)e.battMv, e.battMv / 1000.0f,
                      err ? -1 : batteryPercent(e.battMv), battStateChar(e.battFlags));
    }
}

void printConfig() {
    Serial.printf("[PYTHON] CONFIG BUZZER:%d BEEP_MS:%u BASE_BUZZER:%d BRIGHTNESS:%u LANES:%u SEGMENTS:%u BLINK:%u CONFIGURED:%d\n",
                  buzzerOnHit ? 1 : 0, (unsigned)beepMs, baseBuzzerOn ? 1 : 0, (unsigned)displayIntensity,
                  (unsigned)laneCount, (unsigned)segCount, (unsigned)blinkMode, configured ? 1 : 0);
}

void printBootLine() {
    esp_reset_reason_t r = esp_reset_reason();
    const char* name = "OTHER";
    if (r == ESP_RST_POWERON)        name = "POWERON";
    else if (r == ESP_RST_SW)        name = "SOFTWARE";
    else if (r == ESP_RST_BROWNOUT)  name = "BROWNOUT";
    else if (r == ESP_RST_PANIC)     name = "PANIC";
    else if (r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT) name = "WATCHDOG";
    int assigned = 0;
    for (uint8_t i = 0; i < endpointCount; i++) {
        if (registry[i].lane != 0) assigned++;
    }
    int expected = (int)laneCount * (int)segCount;
    Serial.printf("[PYTHON] BOOT REASON:%s FIRST_RUN:%d LANES:%u SEGMENTS:%u ASSIGNED:%d EXPECTED:%d\n",
                  name, firstRun ? 1 : 0, (unsigned)laneCount, (unsigned)segCount, assigned, expected);
    if (configured && assigned < expected) {
        Serial.printf("[PYTHON] SETUP:INCOMPLETE ASSIGNED:%d EXPECTED:%d\n", assigned, expected);
        buzzerStart(5, 120, 120);
    }
}

// ====================================================================
// ESP-NOW SENDING
// ====================================================================
bool ensurePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 1;
    p.encrypt = false;
    return esp_now_add_peer(&p) == ESP_OK;
}

void sendAck(const uint8_t* mac, uint8_t ackedType, uint8_t seq) {
    if (!ensurePeer(mac)) {
        DEBUG_PRINTLN("[ESP-NOW] Failed to register the peer for the ACK!");
        return;
    }
    struct_message a;
    memset(&a, 0, sizeof(a));
    a.key = SECRET_KEY;
    a.id = MSG_ACK;
    a.payload[0] = ackedType;
    a.payload[1] = seq;
    esp_now_send(mac, (uint8_t *)&a, sizeof(a));
}

// Sends the settings to one endpoint (unicast) or to all (pass broadcastMAC)
void sendConfig(const uint8_t* mac) {
    if (!ensurePeer(mac)) return;
    struct_message c;
    memset(&c, 0, sizeof(c));
    c.key = SECRET_KEY;
    c.id = MSG_CONFIG;
    c.payload[0] = buzzerOnHit ? CFG_FLAG_BUZZER_ON_HIT : 0;
    c.payload[1] = (uint8_t)(beepMs / 10);          // beep length in 10 ms units
    esp_now_send(mac, (uint8_t *)&c, sizeof(c));
}

// Broadcasts a battery request. measure = true: the endpoints first measure with their radio off (only when no run is in progress!)
void sendBattRequest(bool measure) {
    struct_message req;
    memset(&req, 0, sizeof(req));
    req.key = SECRET_KEY;
    req.id = MSG_BATT_REQ;
    req.payload[0] = measure ? BATT_REQ_MEASURE : 0;
    esp_now_send(broadcastMAC, (uint8_t *)&req, sizeof(req));
}

// Health check: (manual) measure the base battery with Wi-Fi off, report it, then ask all endpoints for theirs.
void startHealthCheck(bool manual) {
    bool measure = manual && !anyRunning();         // the radio is never switched off during a run
    if (measure) measureBaseBatteryPaused();
    printBaseBatteryLine();
    for (uint8_t i = 0; i < endpointCount; i++) registry[i].responded = false;
    healthActive = true;
    healthManual = manual;
    healthStart = millis();
    sendBattRequest(measure);
    DEBUG_PRINTLN("[HEALTH] Battery request sent to all endpoints.");
}

void healthTask() {
    if (!healthActive || (millis() - healthStart) < HEALTH_WINDOW_MS) return;
    healthActive = false;

    int assigned = 0, answered = 0;
    uint8_t worst = 0;   // 0 = all OK, 1 = something LOW, 2 = CRITICAL or an endpoint did not answer
    for (uint8_t i = 0; i < endpointCount; i++) {
        if (registry[i].lane == 0) continue;
        assigned++;
        registry[i].missing = !registry[i].responded;
        if (registry[i].responded) {
            answered++;
            uint8_t s = registry[i].battFlags & BATT_FLAG_STATE_MASK;
            if (s > 2) s = 2;
            if (s > worst) worst = s;
            if ((registry[i].battFlags & BATT_FLAG_ERROR) && worst < 1) worst = 1;
        } else {
            worst = 2;
        }
    }
    if (baseBatt.valid && !baseBatt.fault && baseBatt.state > worst) worst = baseBatt.state;

    if (assigned == 0) {
        Serial.println("[PYTHON] HEALTH:NO_ENDPOINTS");
    } else if (answered == assigned) {
        Serial.printf("[PYTHON] HEALTH:OK COUNT:%d\n", answered);
    } else {
        for (uint8_t i = 0; i < endpointCount; i++) {
            if (registry[i].lane != 0 && !registry[i].responded) {
                Serial.printf("[PYTHON] HEALTH:MISSING EP:%d LANE:%d SEG:%d\n", i + 1, registry[i].lane, registry[i].segment);
            }
        }
    }

    // Acoustic result, never while the stopwatches run
    if (!anyRunning()) {
        if (worst == 2)                          buzzerStart(5, 120, 120);
        else if (worst == 1)                     buzzerStart(3, 150, 150);
        else if (healthManual && assigned > 0)   buzzerStart(1, 60, 0);
    }
}

// ====================================================================
// MESSAGE HANDLERS (called from the ESP-NOW receive callback)
// ====================================================================
void onEvent(const uint8_t* mac, uint8_t seq) {
    int idx = findOrAddEndpoint(mac);

    // LEARN mode: this endpoint gets the requested lane/segment; the event itself is not used for timing
    if (learnLane != 0 && (long)(millis() - learnUntil) < 0 && idx >= 0) {
        uint8_t ln = learnLane, sg = learnSeg;
        for (uint8_t i = 0; i < endpointCount; i++) {          // one endpoint per lane + segment
            if ((int)i != idx && registry[i].lane == ln && registry[i].segment == sg) {
                registry[i].lane = 0;
                registry[i].segment = 0;
                registry[i].missing = false;
                Serial.printf("[PYTHON] UNASSIGNED EP:%d\n", i + 1);
            }
        }
        registry[idx].lane = ln;
        registry[idx].segment = sg;
        registry[idx].missing = false;
        registryDirty = true;
        Serial.printf("[PYTHON] ASSIGNED EP:%d LANE:%d SEG:%d\n", idx + 1, registry[idx].lane, registry[idx].segment);
        learnAssignedIdx = idx;
        learnLane = 0;
        sendAck(mac, MSG_EVENT, seq);
        return;
    }

    if (idx < 0 || registry[idx].lane == 0) {
        static unsigned long lastWarn = 0;
        if (millis() - lastWarn > 1000) {
            lastWarn = millis();
            Serial.printf("[PYTHON] EVENT:UNASSIGNED EP:%d\n", idx + 1);
        }
        sendAck(mac, MSG_EVENT, seq);
        return;
    }

    int d = registry[idx].lane - 1;
    DEBUG_PRINTF("[ESP-NOW] Event from EP %d (lane %d, segment %d)\n", idx + 1, registry[idx].lane, registry[idx].segment);
    if (d >= 0 && d < laneCount && startTime[d] != 0) {
        if (registry[idx].segment < segCount) recordSplit(d, registry[idx].segment);   // intermediate segment = split time
        else                                  stopTimer(d);                            // the last segment stops the lane timer
    }
    sendAck(mac, MSG_EVENT, seq);   // ACK only after the time has been taken
}

void onHello(const uint8_t* mac, const struct_message &m) {
    int idx = findOrAddEndpoint(mac);
    if (idx >= 0) {
        registry[idx].battMv = unpackBatteryMv(m);
        registry[idx].battFlags = unpackBatteryFlags(m);
        registry[idx].missing = false;
        char ms[18];
        macToStr(mac, ms);
        Serial.printf("[PYTHON] HELLO EP:%d MAC:%s LANE:%d SEG:%d\n", idx + 1, ms, registry[idx].lane, registry[idx].segment);
        printBatteryLine(idx);
    }
    sendConfig(mac);   // answers HELLO; this is also what makes the endpoint stop saying HELLO
}

void onBatteryReport(const uint8_t* mac, const struct_message &m) {
    int idx = findOrAddEndpoint(mac);
    if (idx >= 0) {
        registry[idx].battMv = unpackBatteryMv(m);
        registry[idx].battFlags = unpackBatteryFlags(m);
        registry[idx].responded = true;
        registry[idx].missing = false;
        printBatteryLine(idx);
    }
    sendAck(mac, MSG_BATT_REPORT, 0);
}

// Remembers when (and how strongly) an endpoint was last heard
void noteHeard(const uint8_t* mac, int rssi) {
    lastHeardMs = millis();
    int i = findEndpoint(mac);
    if (i >= 0) {
        registry[i].heardMs = lastHeardMs;
        registry[i].rssi = (int8_t)rssi;
    }
}

// ESP-NOW receive callback (runs in the Wi-Fi task)
void OnDataRecv(const esp_now_recv_info* sender, const uint8_t* incomingData, int len) {
    if (len != sizeof(struct_message)) {
        DEBUG_PRINTF("[ESP-NOW] Wrong packet length %d (expected %u): different protocol.h?\n", len, (unsigned)sizeof(struct_message));
        return;
    }

    struct_message m;
    memcpy(&m, incomingData, sizeof(m));

    if (m.key != SECRET_KEY) {
        DEBUG_PRINTLN("[SECURITY] Rejected unauthorized packet!");
        return;
    }

    const uint8_t* mac = sender->src_addr;
    int rssi = sender->rx_ctrl ? sender->rx_ctrl->rssi : 0;
    noteHeard(mac, rssi);
    switch (m.id) {
        case MSG_EVENT:       onEvent(mac, m.payload[0]); break;
        case MSG_HELLO:       onHello(mac, m); break;
        case MSG_BATT_REPORT: onBatteryReport(mac, m); break;
        default: break;   // other types (CONFIG, ACK, ...) are for endpoints
    }
    noteHeard(mac, rssi);     // again: a new endpoint has just been added to the registry
}

// ====================================================================
// COMMANDS FROM THE PC
// ====================================================================
void setBuzzer(bool on) {
    buzzerOnHit = on;
    registryDirty = true;
    sendConfig(broadcastMAC);
    lastConfigBroadcast = millis();
    printConfig();
}

void setBeep(uint16_t ms) {
    beepMs = ms;
    registryDirty = true;
    sendConfig(broadcastMAC);
    lastConfigBroadcast = millis();
    printConfig();
}

void processCommand(const char* cmd) {
    int a, b, c;
    if (strcmp(cmd, "BATT?") == 0 || strcmp(cmd, "HEALTH") == 0) {
        startHealthCheck(true);
    } else if (strcmp(cmd, "LIST") == 0) {
        printList();
    } else if (strcmp(cmd, "STATUS") == 0) {
        printConfig();
    } else if (sscanf(cmd, "BUZZER %d", &a) == 1) {
        setBuzzer(a != 0);
    } else if (sscanf(cmd, "BEEP %d", &a) == 1) {
        if (a < 30 || a > 1000) { Serial.println("[PYTHON] ERROR:BAD_VALUE"); return; }
        setBeep((uint16_t)a);
    } else if (sscanf(cmd, "LANES %d", &a) == 1) {
        if (a < 1 || a > MAX_LANES) { Serial.println("[PYTHON] ERROR:BAD_VALUE"); return; }
        if (anyRunning())           { Serial.println("[PYTHON] ERROR:BUSY"); return; }
        laneCount = a;
        if (laneCount > 1) segCount = 1;
        registryDirty = true;
        resetTimer();
        printConfig();
    } else if (sscanf(cmd, "SEGMENTS %d", &a) == 1) {
        if (a < 1 || a > MAX_SEGMENTS)       { Serial.println("[PYTHON] ERROR:BAD_VALUE"); return; }
        if (a > 1 && laneCount != 1)         { Serial.println("[PYTHON] ERROR:ONE_LANE_ONLY"); return; }
        if (anyRunning())                    { Serial.println("[PYTHON] ERROR:BUSY"); return; }
        segCount = a;
        registryDirty = true;
        printConfig();
    } else if (sscanf(cmd, "BLINK %d", &a) == 1) {
        if (a < 0 || a > 2) { Serial.println("[PYTHON] ERROR:BAD_VALUE"); return; }
        blinkMode = a;
        registryDirty = true;
        printConfig();
    } else if (strcmp(cmd, "SETUPDONE") == 0) {
        configured = true;
        registryDirty = true;
        printConfig();
    } else if (sscanf(cmd, "ASSIGN %d %d %d", &a, &b, &c) == 3) {
        if (a < 1 || a > endpointCount)   { Serial.println("[PYTHON] ERROR:BAD_EP"); return; }
        if (b < 1 || b > MAX_LANES)       { Serial.println("[PYTHON] ERROR:BAD_LANE"); return; }
        if (c < 1 || c > MAX_SEGMENTS)    { Serial.println("[PYTHON] ERROR:BAD_SEGMENT"); return; }
        registry[a - 1].lane = b;
        registry[a - 1].segment = c;
        registryDirty = true;
        Serial.printf("[PYTHON] ASSIGNED EP:%d LANE:%d SEG:%d\n", a, b, c);
    } else if (sscanf(cmd, "LEARN %d %d", &a, &b) == 2) {
        if (a < 1 || a > MAX_LANES)       { Serial.println("[PYTHON] ERROR:BAD_LANE"); return; }
        if (b < 1 || b > MAX_SEGMENTS)    { Serial.println("[PYTHON] ERROR:BAD_SEGMENT"); return; }
        learnSeg = b;
        learnUntil = millis() + LEARN_TIMEOUT_MS;
        learnLane = a;
        Serial.println("[PYTHON] LEARN:WAITING");
    } else if (strcmp(cmd, "FORGET ALL") == 0) {
        portENTER_CRITICAL(&regMux);
        endpointCount = 0;
        portEXIT_CRITICAL(&regMux);
        registryDirty = true;
        Serial.println("[PYTHON] OK");
    } else if (sscanf(cmd, "FORGET %d", &a) == 1) {
        if (a < 1 || a > endpointCount)   { Serial.println("[PYTHON] ERROR:BAD_EP"); return; }
        portENTER_CRITICAL(&regMux);
        for (uint8_t i = a - 1; i + 1 < endpointCount; i++) registry[i] = registry[i + 1];
        endpointCount = endpointCount - 1;
        portEXIT_CRITICAL(&regMux);
        registryDirty = true;
        Serial.println("[PYTHON] OK");
    } else {
        Serial.println("[PYTHON] ERROR:UNKNOWN_COMMAND");
    }
}

// Collects a line from the UART without blocking
void handleSerialCommands() {
    static char buf[40];
    static uint8_t n = 0;
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\n' || ch == '\r') {
            buf[n] = 0;
            if (n > 0) processCommand(buf);
            n = 0;
        } else if (n < sizeof(buf) - 1) {
            buf[n++] = ch;
        }
    }
}

// ====================================================================
// OTA SERVICE MODE
// ====================================================================
void setupOTA() {
    WiFi.softAP("STOPKY-UPDATE", "12345678");

    server.on("/", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", serverIndex);
    });

    server.on("/update", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "UPDATE FAILED" : "UPDATE OK! ESP32 is restarting...");
        delay(1000);
        ESP.restart();
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            DEBUG_PRINTF("OTA upload started: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                DEBUG_PRINTF("OTA finished successfully! Size: %u B\n", (unsigned)upload.totalSize);
            } else {
                Update.printError(Serial);
            }
        }
    });

    server.begin();
    DEBUG_PRINTLN("[OTA] Service mode active! Connect to Wi-Fi 'STOPKY-UPDATE' and open 192.168.4.1");
}

// ====================================================================
// SETUP
// ====================================================================
// BLUE button (IO0 to GND): immediate restart, also when the code is stuck in a loop.
// Not used in the setup mode (there BLUE = back) and never hold it while powering up.
void IRAM_ATTR onSoftResetISR() {
    esp_restart();
}

void setup() {
    // Disable the brownout detector to avoid resets on sudden voltage drops
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    delay(500);
    DEBUG_PRINTLN("\n--- ESP32-CAM Stopwatch Base v6.12 starting ---");

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    pinMode(SOFT_RESET_PIN, INPUT_PULLUP);
    pinMode(START_PIN, INPUT_PULLDOWN);
    pinMode(RESTART_PIN, INPUT_PULLDOWN);
    attachInterrupt(SOFT_RESET_PIN, onSoftResetISR, FALLING);

    // SERVICE OTA MODE: RESTART button held at power-up
    if (digitalRead(RESTART_PIN) == HIGH) {
        otaMode = true;
        setupOTA();
        return;
    }

    // Displays (bit-banged SPI)
    lc = LedController<2, 8>(DIN, CLK, CS);
    lc.setIntensity(15);
    lc.clearMatrix();

    // Endpoint registry and settings from flash
    loadRegistry();
    lc.setIntensity(displayIntensity);
    DEBUG_PRINTF("[FLASH] %u endpoint(s) loaded, lanes %u, segments %u\n", (unsigned)endpointCount, (unsigned)laneCount, (unsigned)segCount);

    // Base battery: first measurement BEFORE Wi-Fi starts (ADC2 is reliable then)
    analogSetPinAttenuation(BATT_PIN, ADC_11db);
    battMeasureBlocking();
    baseBattReport = false;
    baseBattAlert = false;
    DEBUG_PRINTF("[BATT] base boot: %u mV, state %d, valid=%d\n", (unsigned)baseBatt.mv, (int)baseBatt.state, (int)baseBatt.valid);
    printBaseBatteryLine();
    startupBeep();

    // Wi-Fi and ESP-NOW
    if (!radioStart()) {
        DEBUG_PRINTLN("[ESP-NOW] Init error!");
        return;
    }

    // First settings broadcast after ~2 s, first health check after ~4 s
    lastConfigBroadcast = millis() + 2000UL - CONFIG_REBROADCAST_MS;
    lastHealthPoll = millis() + 4000UL - HEALTH_POLL_MS;

    resetTimer();
    firstRun = !configured;
    printBootLine();
    if (firstRun || digitalRead(START_PIN) == HIGH) enterSetupMode();   // first run, or START held at power-up
    DEBUG_PRINTLN("[SYS] Setup finished.");
}

// ====================================================================
// LOOP
// ====================================================================
void loop() {
    if (otaMode) {
        server.handleClient();
        delay(1);
        return;
    }
    if (setupMode) {
        setupLoop();
        return;
    }

    unsigned long now = millis();
    buzzerUpdate();

    // 3. START button. Timers idle: starts them. Run in progress: a second press (after releasing the button)
    //    within FALSE_START_WINDOW_MS of the start = false start, the run is aborted.
    bool startHigh = (digitalRead(START_PIN) == HIGH);
    bool running = anyRunning();
    if (startHigh) startLastHigh = now;
    if (startLocked) {
        if (startHigh) startLockUntil = now + START_LOCKOUT_MS;       // still held: keep the lock
        else if ((long)(now - startLockUntil) >= 0) startLocked = false;
    }
    if (startHigh) {
        if (!running) {
            if (!startLocked) startTimers();
        } else if (FALSE_START_WINDOW_MS > 0 && startArmed && (now - runStartedAt) <= FALSE_START_WINDOW_MS) {
            startArmed = false;
            abortRun();
        }
    } else if (running && !startArmed && (now - startLastHigh) >= START_RELEASE_MS) {
        startArmed = true;
    }

    // 4. RESTART button: hold to reset the times, measure the batteries and show them while the button is held
    if (digitalRead(RESTART_PIN) == HIGH) {
        restartLastHigh = now;
        if (!restartHeld) {
            restartHeld = true;
            restartDone = false;
            restartSince = now;
        }
        unsigned long holdNeeded = running ? RESET_HOLD_RUNNING_MS : RESET_HOLD_IDLE_MS;
        if (!restartDone && (now - restartSince) >= holdNeeded) {
            restartDone = true;
            resetTimer();
            enterStatusView();
            startHealthCheck(true);     // base battery with Wi-Fi off, then the endpoints are asked to do the same
        }
    } else if (restartHeld && (now - restartLastHigh) > RESTART_RELEASE_DEBOUNCE_MS) {
        restartHeld = false;
        if (statusView) exitStatusView();
    }

    // 5. Display refresh
    if (statusView) {
        if (now - statusLastDraw >= STATUS_REDRAW_MS) {
            statusLastDraw = now;
            drawStatusView(now);
        }
    } else {
        if (now - lastUpdateTime >= updateInterval) {
            lastUpdateTime = now;
            updateTimers();
        }
        updateMarkers(now);
        if (blinkLane >= 0) {                                      // best / worst lane blinks after FINISH
            bool on = ((now / BLINK_HALF_PERIOD_MS) % 2) == 0;
            if (on != blinkShown) {
                blinkShown = on;
                if (on) displayTime(blinkLane);
                else    blankTime(blinkLane);
            }
        }
    }

    // 6. PC commands (non-blocking)
    handleSerialCommands();

    // 7. Housekeeping, only while the stopwatches are not running
    if (!anyRunning()) {
        if (baseBattReport) {
            baseBattReport = false;
            printBaseBatteryLine();
        }
        if (baseBattAlert) {
            baseBattAlert = false;
            if (baseBatt.state == BATT_CRIT)     buzzerStart(5, 120, 120);
            else if (baseBatt.state == BATT_LOW) buzzerStart(3, 150, 150);
        }
        if (HEALTH_POLL_MS > 0 && millis() - lastHealthPoll >= HEALTH_POLL_MS) {
            lastHealthPoll = millis();
            startHealthCheck(false);                              // periodic: no Wi-Fi pause, cached values
        }
        if (millis() - lastConfigBroadcast >= CONFIG_REBROADCAST_MS) {
            lastConfigBroadcast = millis();
            sendConfig(broadcastMAC);
        }
        if (registryDirty) {                                      // flash write blocks for a few ms -> only when idle
            registryDirty = false;
            saveRegistry();
        }
    }
    healthTask();

    // 8. LEARN mode timeout (PC command)
    if (learnLane != 0 && (long)(millis() - learnUntil) >= 0) {
        learnLane = 0;
        Serial.println("[PYTHON] LEARN:TIMEOUT");
    }
}

// ====================================================================
// TIMERS
// ====================================================================
void startTimers() {
    unsigned long t0 = millis();          // ONE stamp for all lanes, taken before any display work
    portENTER_CRITICAL(&timerMux);
    for (int i = 0; i < MAX_LANES; i++) {
        elapsedTime[i] = 0;
        startTime[i] = (i < laneCount) ? t0 : 0;         // an unused lane never starts
        for (int k = 0; k < MAX_SEGMENTS; k++) splitMs[i][k] = 0;
    }
    blinkLane = -1;
    blinkShown = true;
    portEXIT_CRITICAL(&timerMux);

    runStartedAt = t0;
    startArmed = false;                   // START has to be released before a second press counts
    startLastHigh = t0;
    falseStart = false;
    if (statusView) {                     // START always wins over the battery view
        statusView = false;
        markersInvalid = true;
    }
    DEBUG_PRINTLN("Timer started");
    Serial.println("[PYTHON] EVENT:START");
    for (int i = 0; i < DISPLAY_LANES; i++) displayTime(i);
}

// Runs in loop(): refreshes the running times and redraws the final value after a stop
void updateTimers() {
    for (int i = 0; i < DISPLAY_LANES; i++) {
        bool redraw = false;
        if (displayDirty[i]) {
            displayDirty[i] = false;
            redraw = true;
        }
        portENTER_CRITICAL(&timerMux);
        if (startTime[i] != 0) {
            unsigned long newElapsedTime = millis() - startTime[i];
            if (newElapsedTime != elapsedTime[i]) {
                elapsedTime[i] = newElapsedTime;
                redraw = true;
            }
        }
        portEXIT_CRITICAL(&timerMux);
        if (redraw) displayTime(i);
    }
}

// Runs in the ESP-NOW callback (Wi-Fi task): takes the time, never touches the displays
void stopTimer(int lane) {
    unsigned long now = millis();
    unsigned long el;
    bool allStopped;

    portENTER_CRITICAL(&timerMux);
    if (startTime[lane] == 0) {                   // already stopped
        portEXIT_CRITICAL(&timerMux);
        return;
    }
    el = ((long)(now - startTime[lane]) < 0) ? 0 : (now - startTime[lane]);
    elapsedTime[lane] = el;
    startTime[lane] = 0;
    allStopped = !anyRunning();
    portEXIT_CRITICAL(&timerMux);

    if (lane < DISPLAY_LANES) displayDirty[lane] = true;      // loop() redraws the final time
    DEBUG_PRINTF("Lane %d stopped\n", lane + 1);
    Serial.printf("[PYTHON] STOP:%d TIME:%lu\n", lane + 1, el);

    if (allStopped) {
        Serial.printf("[PYTHON] FINISH TIME1:%lu TIME2:%lu TIME3:%lu TIME4:%lu\n", (unsigned long)elapsedTime[0],
                      (unsigned long)elapsedTime[1], (unsigned long)elapsedTime[2], (unsigned long)elapsedTime[3]);
        if (blinkMode != 0 && laneCount >= 2) {                // F = fastest / first, L = slowest / last lane blinks
            int best = -1;
            for (int i = 0; i < laneCount; i++) {
                if (elapsedTime[i] == 0) continue;
                if (best < 0 || (blinkMode == 1 ? elapsedTime[i] < elapsedTime[best] : elapsedTime[i] > elapsedTime[best])) best = i;
            }
            int ties = 0;
            for (int i = 0; best >= 0 && i < laneCount; i++) {
                if (elapsedTime[i] == elapsedTime[best]) ties++;
            }
            if (best >= 0 && best < DISPLAY_LANES && ties == 1) {   // unique, and the lane has a display
                blinkShown = true;
                blinkLane = best;
            }
        }
    }
}

// Runs in the ESP-NOW callback: records the time of an intermediate segment
void recordSplit(int lane, uint8_t seg) {
    unsigned long now = millis();
    unsigned long t = 0;
    bool fresh = false;
    portENTER_CRITICAL(&timerMux);
    if (startTime[lane] != 0 && seg >= 1 && seg <= MAX_SEGMENTS && splitMs[lane][seg - 1] == 0) {   // a resent event counts once
        t = ((long)(now - startTime[lane]) < 0) ? 0 : (now - startTime[lane]);
        splitMs[lane][seg - 1] = (t == 0) ? 1 : t;
        fresh = true;
    }
    portEXIT_CRITICAL(&timerMux);
    if (fresh) Serial.printf("[PYTHON] SPLIT:%d SEG:%d TIME:%lu\n", lane + 1, (int)seg, t);
}

void resetTimer() {
    falseStart = false;
    portENTER_CRITICAL(&timerMux);
    for (int i = 0; i < MAX_LANES; i++) {
        startTime[i] = 0;
        elapsedTime[i] = 0;
        for (int k = 0; k < MAX_SEGMENTS; k++) splitMs[i][k] = 0;
    }
    blinkLane = -1;
    blinkShown = true;
    portEXIT_CRITICAL(&timerMux);
    for (int i = 0; i < DISPLAY_LANES; i++) {
        displayDirty[i] = false;
        displayTime(i);
    }
    DEBUG_PRINTLN("Reset");
    Serial.println("[PYTHON] EVENT:RESET");
}

// Second press of START shortly after the start
void abortRun() {
    unsigned long now = millis();
    unsigned long sinceStart = now - runStartedAt;
    portENTER_CRITICAL(&timerMux);
    for (int i = 0; i < MAX_LANES; i++) {
        startTime[i] = 0;
        elapsedTime[i] = 0;
        for (int k = 0; k < MAX_SEGMENTS; k++) splitMs[i][k] = 0;
    }
    blinkLane = -1;
    blinkShown = true;
    portEXIT_CRITICAL(&timerMux);
    falseStart = true;
    startLocked = true;
    startLockUntil = now + START_LOCKOUT_MS;
    for (int i = 0; i < DISPLAY_LANES; i++) {
        displayDirty[i] = false;
        displayTime(i);
    }
    Serial.printf("[PYTHON] EVENT:FALSE_START MS:%lu\n", sinceStart);
    buzzerStart(1, 500, 0);
}

// ====================================================================
// DISPLAY: time (digits 7..2; digits 1 and 0 belong to the status markers)
// ====================================================================
void blankTime(int d) {
    for (int p = 7; p >= 2; p--) lc.setChar(d, p, ' ', false);
}

void displayTime(int d) {
    if (d >= DISPLAY_LANES) return;
    if (d >= laneCount) {                   // unused lane: blank display
        blankTime(d);
        return;
    }
    if (falseStart) {                       // run aborted by a second press of START
        lc.setChar(d, 7, d == 0 ? 'L' : 'P', false);
        lc.setChar(d, 6, ' ', false);
        lc.setChar(d, 5, 'F', false);
        lc.setChar(d, 4, 'A', false);
        lc.setChar(d, 3, 'L', false);
        lc.setChar(d, 2, 'S', false);
        return;
    }
    unsigned long t = elapsedTime[d];
    lc.setChar(d, 7, d == 0 ? 'L' : 'P', true);
    lc.setDigit(d, 6, (t / 60000) % 10, true);
    lc.setDigit(d, 5, (t / 10000) % 6, false);
    lc.setDigit(d, 4, (t / 1000) % 10, true);
    lc.setDigit(d, 3, (t / 100) % 10, false);   // tenths of a second
    lc.setDigit(d, 2, (t / 10) % 10, false);    // hundredths of a second
}

// ====================================================================
// DISPLAY: status markers (digit 1 = worst endpoint on the lane, digit 0 = base battery)
// rank: 0 = OK, 1 = LOW (steady L), 2 = CRITICAL (blinking C), 3 = no answer / fault (blinking E)
// ====================================================================
char markerChar(uint8_t rank, bool blinkOn) {
    switch (rank) {
        case 1:  return 'L';
        case 2:  return blinkOn ? 'C' : ' ';
        case 3:  return blinkOn ? 'E' : ' ';
        default: return ' ';
    }
}

uint8_t laneRank(uint8_t lane) {
    uint8_t rank = 0;
    for (uint8_t i = 0; i < endpointCount; i++) {
        const Endpoint &e = registry[i];
        if (e.lane != lane) continue;
        uint8_t r = 0;
        if (e.missing || (e.battMv != 0 && (e.battFlags & BATT_FLAG_ERROR))) {
            r = 3;
        } else if (e.battMv != 0) {                 // battMv == 0 = nothing reported yet: no marker
            uint8_t s = e.battFlags & BATT_FLAG_STATE_MASK;
            r = (s >= 2) ? 2 : s;
        }
        if (r > rank) rank = r;
    }
    return rank;
}

void updateMarkers(unsigned long now) {
    static unsigned long last = 0;
    static char shown[2][2] = {{'?', '?'}, {'?', '?'}};

    if (markersInvalid) {                           // the digits were overwritten: redraw all
        markersInvalid = false;
        for (int d = 0; d < 2; d++) { shown[d][0] = '?'; shown[d][1] = '?'; }
        last = 0;
    }
    if (now - last < MARKER_REFRESH_MS) return;
    last = now;

    bool blinkOn = ((now / MARKER_BLINK_MS) % 2) == 0;
    uint8_t baseRank = (!baseBatt.valid || baseBatt.fault) ? 3 : baseBatt.state;
    char baseCh = markerChar(baseRank, blinkOn);

    for (int d = 0; d < DISPLAY_LANES; d++) {
        char laneCh = (d < laneCount) ? markerChar(laneRank(d + 1), blinkOn) : ' ';
        char bCh = (d < laneCount) ? baseCh : ' ';
        if (laneCh != shown[d][0]) { lc.setChar(d, 1, laneCh, false); shown[d][0] = laneCh; }
        if (bCh != shown[d][1]) { lc.setChar(d, 0, bCh, false); shown[d][1] = bCh; }
    }
}

// ====================================================================
// DISPLAY: COMPACT BATTERY VIEW (RESTART held after the reset). 16 digits = left display + right display.
// ====================================================================
#define MAX_DEVICES 16

void enterStatusView() {
    statusView = true;
    statusLastDraw = 0;
    lc.clearMatrix();
}

void exitStatusView() {
    statusView = false;
    markersInvalid = true;
    lc.clearMatrix();
    displayTime(0);
    displayTime(1);
}

// One digit of the 16-digit strip (g = 0..15, left to right)
void putDigit(int g, char ch, bool dp) {
    int d = g / 8;
    int digit = 7 - (g % 8);
    if (ch >= '0' && ch <= '9') lc.setDigit(d, digit, ch - '0', dp);
    else                        lc.setChar(d, digit, ch, dp);
}

// Devices in the order base, endpoints by lane then segment (a starter would go last). Returns the count.
int buildDevices(int* pct, uint8_t* st) {
    int n = 0;
    pct[n] = (!baseBatt.valid || baseBatt.fault) ? -1 : batteryPercent(baseBatt.mv);
    st[n] = baseBatt.state;
    n++;

    uint8_t order[MAX_ENDPOINTS];
    int m = 0;
    for (uint8_t i = 0; i < endpointCount; i++) {
        if (registry[i].lane != 0) order[m++] = i;
    }
    for (int a = 1; a < m; a++) {                  // insertion sort by lane, then segment
        uint8_t key = order[a];
        int kk = registry[key].lane * 16 + registry[key].segment;
        int b = a - 1;
        while (b >= 0 && registry[order[b]].lane * 16 + registry[order[b]].segment > kk) {
            order[b + 1] = order[b];
            b--;
        }
        order[b + 1] = key;
    }
    for (int k = 0; k < m && n < MAX_DEVICES; k++) {
        uint8_t s = registry[order[k]].battFlags & BATT_FLAG_STATE_MASK;
        pct[n] = endpointPct(order[k]);
        st[n] = (s > 2) ? 2 : s;
        n++;
    }
    return n;
}

void drawStatusView(unsigned long now) {
    int pct[MAX_DEVICES];
    uint8_t st[MAX_DEVICES];
    int n = buildDevices(pct, st);
    int w = (n <= 5) ? 3 : (n <= 8) ? 2 : 1;          // digits per device
    bool blinkOn = ((now / MARKER_BLINK_MS) % 2) == 0;

    for (int g = 0; g < 16; g++) putDigit(g, ' ', false);
    for (int k = 0; k < n; k++) {
        int g = k * w;
        int p = pct[k];
        uint8_t s = st[k];
        if (p < 0) {                                   // no value / no answer
            for (int j = 0; j < w; j++) putDigit(g + j, '-', false);
            continue;
        }
        if (s == 2 && !blinkOn) continue;              // CRITICAL blinks
        if (w == 3) {                                  // [state or hundreds][tens][ones]
            char c0 = (p >= 100) ? '1' : (s == 1 ? 'L' : (s == 2 ? 'C' : ' '));
            int v = (p >= 100) ? 0 : p;
            putDigit(g, c0, false);
            putDigit(g + 1, '0' + (v / 10) % 10, false);
            putDigit(g + 2, '0' + v % 10, false);
        } else if (w == 2) {                           // percent, dot after the last digit = LOW
            int v = (p > 99) ? 99 : p;
            putDigit(g, '0' + v / 10, false);
            putDigit(g + 1, '0' + v % 10, s == 1);
        } else {                                       // tens of percent, dot = LOW
            int v = p / 10;
            if (v > 9) v = 9;
            putDigit(g, '0' + v, s == 1);
        }
    }
}

// ====================================================================
// SETUP: buttons and text helpers
// ====================================================================
const uint8_t btnPin[3] = {START_PIN, RESTART_PIN, SOFT_RESET_PIN};   // 0 = GREEN, 1 = RED, 2 = BLUE
const bool btnActiveHigh[3] = {true, true, false};                     // GREEN and RED switch to 3V3, BLUE to GND
bool btnRaw[3] = {false, false, false};
bool btnState[3] = {false, false, false};
bool btnLongDone[3] = {false, false, false};
unsigned long btnChangedAt[3] = {0, 0, 0};
unsigned long btnPressedAt[3] = {0, 0, 0};
const uint16_t BEEP_CHOICES[] = {60, 120, 200, 300, 500};
const uint8_t BEEP_CHOICES_N = 5;

// Returns 0 = nothing, 1 = short press (reported on release), 2 = long press (reported once, while still held)
uint8_t pollButton(uint8_t b, unsigned long now, unsigned long longMs) {
    bool raw = (digitalRead(btnPin[b]) == (btnActiveHigh[b] ? HIGH : LOW));
    if (raw != btnRaw[b]) {
        btnRaw[b] = raw;
        btnChangedAt[b] = now;
    }
    if ((now - btnChangedAt[b]) >= 30 && raw != btnState[b]) {
        btnState[b] = raw;
        if (raw) {
            btnPressedAt[b] = now;
            btnLongDone[b] = false;
        } else if (!btnLongDone[b]) {
            return 1;
        }
    }
    if (btnState[b] && !btnLongDone[b] && (now - btnPressedAt[b]) >= longMs) {
        btnLongDone[b] = true;
        return 2;
    }
    return 0;
}

// Draws exactly 8 characters (left to right) on one display
void showText(int d, const char* s) {
    for (int i = 0; i < 8; i++) {
        char ch = s[i];
        int digit = 7 - i;
        if (ch >= '0' && ch <= '9') lc.setDigit(d, digit, ch - '0', false);
        else                        lc.setChar(d, digit, ch, false);
    }
}

// ====================================================================
// SETUP: LANES -> SEGS -> PAIR -> HIT -> LEN -> HORN -> LIGHT -> FLASH -> OUT
// ====================================================================
void enterSetupMode() {
    detachInterrupt(SOFT_RESET_PIN);             // BLUE is "back" in the setup (held 2 s = restart)
    setupMode = true;
    setupWaitRelease = true;
    setupStep = STEP_LANES;
    pairLane = 1;
    pairSeg = 1;
    setupIntroUntil = millis() + 1000;
    setupRedraw = true;
    showText(0, "SETUP   ");
    showText(1, "        ");
    buzzerStart(3, 60, 60);
    Serial.println("[PYTHON] SETUP:ENTER");
}

void exitSetup() {
    Serial.println("[PYTHON] SETUP:EXIT");
    configured = true;
    saveRegistry();
    registryDirty = false;
    lc.clearMatrix();
    unsigned long t = millis();
    while (digitalRead(START_PIN) == HIGH && (millis() - t) < 3000) delay(10);   // do not re-enter the setup after the restart
    delay(200);
    ESP.restart();
}

// Last digit of the LEFT display: number of endpoints heard in the last 6 s, the dot flashes on every received packet
void drawLinkIndicator(unsigned long now) {
    int n = 0;
    for (uint8_t i = 0; i < endpointCount; i++) {
        if (registry[i].heardMs != 0 && (now - registry[i].heardMs) < 6000) n++;
    }
    bool flash = (lastHeardMs != 0) && ((now - lastHeardMs) < 250);
    lc.setDigit(0, 0, n > 9 ? 9 : n, flash);
}

char stateChar(uint8_t flags) {
    uint8_t s = flags & BATT_FLAG_STATE_MASK;
    return (s >= 2) ? 'C' : (s == 1) ? 'L' : ' ';
}

void drawSetup(unsigned long now) {
    char l[9], r[9];
    const char* label = "";
    switch (setupStep) {
        case STEP_LANES: label = "LANES"; break;
        case STEP_SEGS:  label = "SEGS";  break;
        case STEP_PAIR:  label = "PAIR";  break;
        case STEP_HIT:   label = "HIT";   break;
        case STEP_LEN:   label = "LEN";   break;
        case STEP_HORN:  label = "HORN";  break;
        case STEP_LIGHT: label = "LIGHT"; break;
        case STEP_FLASH: label = "FLASH"; break;
        default:         label = "OUT";   break;
    }
    snprintf(l, sizeof(l), "%-8s", label);
    memset(r, ' ', 8);
    r[8] = 0;

    switch (setupStep) {
        case STEP_LANES:                                   // the numbers of the lanes in use are lit: "1 2 3"
            for (int i = 0; i < laneCount && i < 4; i++) r[i * 2] = (char)('1' + i);
            break;
        case STEP_SEGS:  snprintf(r, sizeof(r), "%8u", (unsigned)segCount); break;
        case STEP_PAIR: {                                  // slot "lane-segment", then state + percent of its endpoint
            int idx = slotEndpoint(pairLane, pairSeg);
            bool blinkOn = ((now / 400) % 2) == 0;
            r[0] = (idx < 0 && !blinkOn) ? ' ' : (char)('0' + pairLane);   // an empty slot blinks: trigger its sensor
            r[1] = '-';
            r[2] = (char)('0' + pairSeg);
            if (idx >= 0) {
                int p = endpointPct(idx);
                r[4] = stateChar(registry[idx].battFlags);
                if (p < 0) {
                    r[5] = '-'; r[6] = '-'; r[7] = '-';
                } else {
                    r[5] = (p >= 100) ? '1' : ' ';
                    r[6] = (p >= 10) ? (char)('0' + (p / 10) % 10) : ' ';
                    r[7] = (char)('0' + p % 10);
                }
            }
            break;
        }
        case STEP_HIT:   snprintf(r, sizeof(r), "%8u", buzzerOnHit ? 1u : 0u); break;
        case STEP_LEN:   snprintf(r, sizeof(r), "%8u", (unsigned)beepMs); break;
        case STEP_HORN:  snprintf(r, sizeof(r), "%8u", baseBuzzerOn ? 1u : 0u); break;
        case STEP_LIGHT: snprintf(r, sizeof(r), "%8u", (unsigned)displayIntensity); break;
        case STEP_FLASH: r[7] = (blinkMode == 0) ? '0' : (blinkMode == 1 ? 'F' : 'L'); break;   // 0 = off, F = fastest, L = slowest
        default:         r[6] = 'G'; r[7] = 'O'; break;                                         // GREEN = save and restart
    }
    showText(0, l);
    drawLinkIndicator(now);
    showText(1, r);
}

void setupLoop() {
    unsigned long now = millis();
    static unsigned long lastDraw = 0;

    buzzerUpdate();
    handleSerialCommands();
    if (now - lastConfigBroadcast >= 3000) {                     // endpoints may have missed a broadcast
        lastConfigBroadcast = now;
        sendConfig(broadcastMAC);
    }
    if (now - lastBattReqMs >= 4000) {                           // every endpoint answers (cached value) -> the link indicator counts them
        lastBattReqMs = now;
        sendBattRequest(false);
    }

    uint8_t bg = pollButton(0, now, 800);                        // GREEN: confirm / next, held = jump ahead
    uint8_t br = pollButton(1, now, 800);                        // RED: change the value
    uint8_t bb = pollButton(2, now, 2000);                       // BLUE: back, held 2 s = restart
    if (setupWaitRelease) {                                      // ignore the press that opened the setup
        if (!btnRaw[0] && !btnRaw[1] && !btnRaw[2] && !btnState[0] && !btnState[1] && !btnState[2]) setupWaitRelease = false;
        bg = 0; br = 0; bb = 0;
    }
    if ((long)(now - setupIntroUntil) < 0) { bg = 0; br = 0; bb = 0; }   // "SETUP" intro screen
    if (bg || br || bb) lastInputMs = now;

    if (registryDirty && (now - lastInputMs) > 1200) {           // save a moment after the last button press
        registryDirty = false;
        saveRegistry();
    }
    if (bb == 2) {                                               // BLUE held: restart
        if (registryDirty) {
            registryDirty = false;
            saveRegistry();
        }
        ESP.restart();
    }

    // PAIR: the current slot is always waiting for the sensor of its endpoint
    if (setupStep == STEP_PAIR) {
        learnSeg = pairSeg;
        learnUntil = now + 60000;
        learnLane = pairLane;
        if (learnAssignedIdx >= 0) {                             // an endpoint was (re)paired to this slot
            learnAssignedIdx = -1;
            buzzerStart(1, 80, 0);
            lastInputMs = now;
            setupRedraw = true;
        }
    } else {
        learnLane = 0;
    }

    uint8_t stepAtStart = setupStep;                             // the navigation below must not react twice to one press
    switch (setupStep) {
        case STEP_LANES:
            if (br == 1) {
                laneCount = (laneCount % MAX_LANES) + 1;
                if (laneCount > 1) segCount = 1;                 // more segments only with one lane
                registryDirty = true;
            } else if (bg == 1) {
                registryDirty = true;
                setupStep = (laneCount == 1) ? STEP_SEGS : STEP_PAIR;
                pairLane = 1;
                pairSeg = 1;
            }
            break;
        case STEP_SEGS:
            if (br == 1) {
                segCount = (segCount % MAX_SEGMENTS) + 1;
                registryDirty = true;
            } else if (bg == 1) {
                registryDirty = true;
                setupStep = STEP_PAIR;
                pairLane = 1;
                pairSeg = 1;
            } else if (bb == 1) {
                setupStep = STEP_LANES;
            }
            break;
        case STEP_PAIR:
            if (br == 1) {                                       // RED: free this slot
                int idx = slotEndpoint(pairLane, pairSeg);
                if (idx >= 0) {
                    registry[idx].lane = 0;
                    registry[idx].segment = 0;
                    registry[idx].missing = false;
                    registryDirty = true;
                }
            } else if (bg == 1) {                                // GREEN: keep / skip, next slot
                if (++pairSeg > segCount) {
                    pairSeg = 1;
                    pairLane++;
                }
                if (pairLane > laneCount) setupStep = STEP_HIT;
            } else if (bg == 2) {                                // GREEN held: pairing is done
                setupStep = STEP_HIT;
            } else if (bb == 1) {                                // BLUE: previous slot
                if (pairSeg > 1) {
                    pairSeg--;
                } else if (pairLane > 1) {
                    pairLane--;
                    pairSeg = segCount;
                } else {
                    setupStep = (laneCount == 1) ? STEP_SEGS : STEP_LANES;
                }
            }
            break;
        case STEP_HIT:
            if (br == 1) setBuzzer(!buzzerOnHit);                // endpoints answer with 2 short / 1 long beep
            break;
        case STEP_LEN:
            if (br == 1) {
                uint8_t k = 0;
                for (uint8_t i = 0; i < BEEP_CHOICES_N; i++) {
                    if (BEEP_CHOICES[i] == beepMs) k = i + 1;
                }
                setBeep(BEEP_CHOICES[k % BEEP_CHOICES_N]);
            }
            break;
        case STEP_HORN:
            if (br == 1) {
                baseBuzzerOn = !baseBuzzerOn;
                registryDirty = true;
                printConfig();
                if (baseBuzzerOn) buzzerStart(1, 80, 0);
            }
            break;
        case STEP_LIGHT:
            if (br == 1) {
                displayIntensity = (displayIntensity % 15) + 1;
                lc.setIntensity(displayIntensity);
                registryDirty = true;
                printConfig();
            }
            break;
        case STEP_FLASH:
            if (br == 1) {
                blinkMode = (blinkMode + 1) % 3;
                registryDirty = true;
                printConfig();
            }
            break;
        default:                                                 // OUT
            if (bg == 1) exitSetup();
            break;
    }
    if (stepAtStart >= STEP_HIT && stepAtStart < STEP_OUT) {     // settings: GREEN next, GREEN held = OUT, BLUE back
        if (bg == 1) setupStep = stepAtStart + 1;
        else if (bg == 2) setupStep = STEP_OUT;
        else if (bb == 1) {
            if (stepAtStart == STEP_HIT) {                         // back into the last PAIR slot
                setupStep = STEP_PAIR;
                pairLane = laneCount;
                pairSeg = segCount;
            } else {
                setupStep = stepAtStart - 1;
            }
        }
    } else if (stepAtStart == STEP_OUT && bb == 1) {
        setupStep = STEP_FLASH;
    }
    if (bg || br || bb) setupRedraw = true;

    if ((long)(now - setupIntroUntil) >= 0 && (setupRedraw || now - lastDraw >= 120)) {
        setupRedraw = false;
        lastDraw = now;
        drawSetup(now);
    }
}