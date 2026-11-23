/*
 * Endpoint v6.12 - ESP32-CAM stopwatch remote node (use together with Base v6.12 and the protocol.h that comes with it)
 * One sensor, one 3W LED box and one buzzer per endpoint. The SAME firmware runs on every endpoint:
 * the base recognises endpoints by MAC address and assigns lane/segment.
 *
 * Behaviour:
 *  - Boot: measures the battery (before Wi-Fi starts) and beeps the result:
 *      1 beep = OK, 3 beeps = LOW, 5 beeps = CRITICAL, 2 long beeps = measurement failed.
 *  - Sends HELLO (with battery) until the base answers with CONFIG (buzzer flag, beep length).
 *  - Sensor triggered (active LOW): the 3W LED lights up and (if enabled by the base) the buzzer beeps
 *    at the same moment. The event is re-sent every 20 ms until the base acknowledges it.
 *      LED: fast blink = sending, solid = delivered (ACK received). OTA mode: slow blink.
 *      If no ACK arrives within 1.5 s the buzzer (if enabled) gives 3 short beeps once.
 *  - BATTERY: ADC2 (IO13) cannot be read while Wi-Fi runs. When the base asks for a fresh measurement
 *    (BATT_REQ with payload[0] = 1, sent when the operator presses RESTART and no run is in progress), the endpoint switches
 *    the radio off for ~0.3 s, measures and answers. Otherwise it reports the last measured value.
 *    A low battery is reported to the base by itself (self-report) after such a measurement.
 *  - Double power-cycle (within 2 s) or MSG_OTA from the base starts the OTA update mode.
 *
 * Timing notes: loop() is non-blocking (millis() based) except the battery measurement on request, which only happens
 * in IDLE / DELIVERED state, i.e. never while a detection is being sent.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <Update.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "config.h"      // SECRET_KEY and broadcastMAC (same on all devices)
#include "protocol.h"    // shared with the base

// ============================================================================
// DEBUG SWITCH (1 = on, 0 = off). Serial output can block when the TX buffer is full,
// so use 0 for timing-critical use.
// ============================================================================
#define DEBUG 0

#if DEBUG
  #define DEBUG_BEGIN(speed) Serial.begin(speed)
  #define DEBUG_PRINT(...)   Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
  #define DEBUG_PRINTF(...)  Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_BEGIN(speed)
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
  #define DEBUG_PRINTF(...)
#endif

// GPIO 16 is used for the sensor. It collides with PSRAM, so PSRAM must be disabled.
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
  #error "PSRAM is enabled! GPIO 16 collides with PSRAM. Arduino IDE: Tools -> PSRAM -> Disabled"
#endif

// ============================================================================
// HARDWARE SETTINGS
// ============================================================================
const int SENSOR_PIN = 16;    // Sensor input, active LOW (switches to GND), internal pull-up
const int BATT_PIN   = 13;    // Battery voltage divider (100k top + 47k bottom), ADC2_CH4
const int LED_PIN    = 12;    // NPN driver of the 3W LED, active HIGH
const int BUZZER_PIN = 14;    // NPN driver of the buzzer, active HIGH

// ============================================================================
// BATTERY SETTINGS (2S Li-ion pack)
// ============================================================================
const float    BATT_RATIO         = (100.0f + 47.0f) / 47.0f;  // use the measured resistor values
const float    BATT_CAL           = 1.000f;                    // fine-tune: real voltage / displayed voltage (multimeter)
const uint8_t  BATT_SAMPLES       = 16;
const uint16_t BATT_LOW_MV        = 7400;       // warning (about 10 % on the resting curve)
const uint16_t BATT_CRIT_MV       = 7000;       // critical (a few percent left)
const uint16_t BATT_HYST_MV       = 200;        // hysteresis when recovering
const uint16_t BATT_MIN_VALID_MV  = 4500;       // outside min..max = measurement fault
const uint16_t BATT_MAX_VALID_MV  = 8800;
const uint8_t  BATT_CONFIRM       = 3;          // consecutive worse readings before the state degrades
const unsigned long BATT_REPORT_LOW_MS  = 300000;  // self-report repeat period (LOW)
const unsigned long BATT_REPORT_CRIT_MS = 60000;   // self-report repeat period (CRITICAL)

// ============================================================================
// TIMING SETTINGS
// ============================================================================
const unsigned long SEND_INTERVAL_MS   = 20;    // event re-send period until ACK
const unsigned long BLINK_INTERVAL_MS  = 60;    // LED blink period while sending
const unsigned long NOACK_WARN_MS      = 1500;  // no ACK after this long -> warning beeps (if enabled)
const unsigned long REQUEST_QUIET_MS   = 300;   // min. pause after sensor activity before answering a request
const unsigned long SELF_REPORT_QUIET_MS = 3000;// min. pause after sensor activity before a self-report

// RTC memory variable for double power-cycle detection
RTC_DATA_ATTR uint32_t rtcMagic = 0;
#define RTC_MAGIC 0xFA3A8B01

// ============================================================================
// STATE
// ============================================================================
enum State { IDLE, SENDING, DELIVERED, OTA_MODE };
State currentState = IDLE;

#define BATT_OK   0
#define BATT_LOW  1
#define BATT_CRIT 2

struct_message eventMsg;       // sensor events
struct_message txMsg;          // HELLO / battery reports

unsigned long sendPreviousMillis = 0;
unsigned long blinkPreviousMillis = 0;
unsigned long sendStartMs = 0;
unsigned long lastActivityMs = 0;
bool ledToggleState = false;
bool noAckWarned = false;
uint8_t eventSeq = 0;
volatile bool eventAcked = false;

// Settings received from the base
volatile bool buzzerOnHit = false;   // CFG_FLAG_BUZZER_ON_HIT
volatile uint16_t hitBeepMs = 120;   // beep length on sensor trigger (CONFIG payload[1] * 10)
volatile bool configured = false;    // a CONFIG message has been received
volatile bool cfgChanged = false;    // buzzer flag changed -> give audible feedback
uint16_t helloTries = 0;
unsigned long lastHelloMs = 0;

// Battery state
uint16_t  battMv = 0;
uint8_t   battState = BATT_OK;
bool battValid = false;        // at least one valid measurement exists
bool battFault = false;        // last reading was outside the plausible range
bool battStale = true;         // the last measurement failed, the value is older
uint8_t battWorseCnt = 0;

volatile bool battAcked = true;
volatile bool battReqPending = false;
volatile bool battReqMeasure = false;   // the request asks for a fresh measurement
volatile unsigned long battReqAt = 0;
unsigned long replyStaggerMs = 0;   // derived from the MAC so endpoints do not answer simultaneously
bool selfActive = false;
uint8_t selfTries = 0;
unsigned long selfNextMs = 0;
unsigned long selfLastTryMs = 0;

// Web server for OTA
WebServer httpServer(80);

const char* otaHtml =
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<h2>Stopwatch ESP32 Endpoint - OTA update</h2>"
  "<input type='file' name='update'>"
  "<input type='submit' value='Upload new firmware (.bin)'>"
  "</form>";

// ============================================================================
// BUZZER (non-blocking beep sequencer)
// ============================================================================
static uint8_t  bzLeft = 0;        // remaining beeps (including the current one)
static uint16_t bzOnMs = 0, bzOffMs = 0;
static bool     bzOn = false;
static unsigned long bzUntil = 0;

void buzzerStart(uint8_t count, uint16_t onMs, uint16_t offMs) {
    if (count == 0) return;
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

// ============================================================================
// BATTERY
// ============================================================================
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
        battStale = true;
        return;
    }
    float mv = (sum / (float)good) * BATT_RATIO * BATT_CAL;
    if (mv < BATT_MIN_VALID_MV || mv > BATT_MAX_VALID_MV) {
        battFault = true;                  // implausible -> report as error, never as "empty battery"
        battStale = true;
        return;
    }
    bool first = !battValid;
    battFault = false;
    battStale = false;
    battValid = true;
    battMv = (uint16_t)(mv + 0.5f);

    uint8_t want = classify(battMv, battState);
    if (first) {
        battState = want;
        battWorseCnt = 0;
        selfNextMs = 0;
    } else if (want > battState) {         // a worse state must be confirmed several times in a row
        if (++battWorseCnt >= BATT_CONFIRM) {
            battState = want;
            battWorseCnt = 0;
            selfNextMs = 0;                // report immediately
        }
    } else {
        if (want != battState) selfNextMs = 0;
        battState = want;
        battWorseCnt = 0;
    }
}

// Blocking measurement (~35 ms). Works only while Wi-Fi is off: at boot, or via measureBatteryRadioPaused().
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

uint8_t battFlagsNow() {
    uint8_t f = battState & BATT_FLAG_STATE_MASK;
    if (!battValid || battFault) f |= BATT_FLAG_ERROR;
    if (battStale) f |= BATT_FLAG_STALE;
    return f;
}

// Builds and broadcasts a HELLO or BATT_REPORT message.
void sendBatteryMessage(int type) {
    memset(&txMsg, 0, sizeof(txMsg));
    txMsg.key = SECRET_KEY;
    txMsg.id = type;
    packBattery(txMsg, battMv > 9999 ? 9999 : battMv, battFlagsNow());
    esp_now_send(broadcastMAC, (uint8_t *)&txMsg, sizeof(txMsg));
    DEBUG_PRINTF("[BATT] sent %s: %u mV, flags 0x%02X\n", type == MSG_HELLO ? "HELLO" : "REPORT",
                 (unsigned)battMv, (unsigned)battFlagsNow());
}

// ============================================================================
// RADIO (Wi-Fi + ESP-NOW)
// ============================================================================
void onEspNowRecv(const esp_now_recv_info *info, const uint8_t *data, int len);

bool radioStart() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) return false;

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastMAC, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    esp_now_register_recv_cb(onEspNowRecv);
    return true;
}

void radioStop() {
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
}

// Fresh battery measurement with the radio switched off for ~0.3 s. Only called in IDLE / DELIVERED state.
void measureBatteryRadioPaused() {
    radioStop();
    delay(20);
    analogSetPinAttenuation(BATT_PIN, ADC_11db);
    battMeasureBlocking();
    if (!radioStart()) {
        DEBUG_PRINTLN("[ESP-NOW] re-init failed -> restart");
        ESP.restart();
    }
}

// Receive callback (runs in the Wi-Fi task, not in loop()): ACK, CONFIG, battery request, OTA command
void onEspNowRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
    if (len != sizeof(struct_message)) return;
    struct_message msg;
    memcpy(&msg, data, sizeof(msg));
    if (msg.key != SECRET_KEY) return;

    switch (msg.id) {
        case MSG_OTA:
            DEBUG_PRINTLN("[ESP-NOW] Remote OTA command received! Restarting...");
            rtcMagic = RTC_MAGIC;
            ESP.restart();
            break;
        case MSG_BATT_REQ:
            battReqMeasure = (msg.payload[0] == BATT_REQ_MEASURE);
            battReqAt = millis() + replyStaggerMs;
            battReqPending = true;
            break;
        case MSG_CONFIG: {
            bool nb = (msg.payload[0] & CFG_FLAG_BUZZER_ON_HIT) != 0;
            if (nb != buzzerOnHit) cfgChanged = true;
            buzzerOnHit = nb;
            uint8_t b = msg.payload[1];                     // beep length in 10 ms units, 0 = not sent by an older base
            if (b >= 3 && b <= 100) hitBeepMs = (uint16_t)b * 10;
            configured = true;
            break;
        }
        case MSG_ACK:
            if (msg.payload[0] == MSG_EVENT && msg.payload[1] == eventSeq) eventAcked = true;
            else if (msg.payload[0] == MSG_BATT_REPORT) battAcked = true;
            break;
        default:
            break;
    }
}

// Answers a battery request from the base (a fresh measurement when asked for, otherwise the cached value).
// Allowed in IDLE and DELIVERED, never while an event is being sent.
void answerBatteryRequest(unsigned long now) {
    if (now - lastActivityMs < REQUEST_QUIET_MS) return;
    if (battReqPending && (long)(now - battReqAt) >= 0) {
        battReqPending = false;
        if (battReqMeasure) {
            battReqMeasure = false;
            measureBatteryRadioPaused();
        }
        sendBatteryMessage(MSG_BATT_REPORT);
    }
}

// Housekeeping that must never disturb a detection: runs in IDLE only.
void idleHousekeeping(unsigned long now) {
    answerBatteryRequest(now);

    // HELLO until the base answers with CONFIG (every 2 s at first, then every 15 s)
    if (!configured) {
        unsigned long interval = (helloTries < 10) ? 2000UL : 15000UL;
        if (now - lastHelloMs >= interval) {
            lastHelloMs = now;
            helloTries++;
            sendBatteryMessage(MSG_HELLO);
        }
    }

    if (now - lastActivityMs < SELF_REPORT_QUIET_MS) return;

    // Low-battery self-report: repeated until the base acknowledges (max. 5 tries per round)
    if (!selfActive) {
        if (battValid && !battFault && battState != BATT_OK && (long)(now - selfNextMs) >= 0) {
            selfActive = true;
            selfTries = 0;
            selfLastTryMs = 0;
            battAcked = false;
        }
    }
    if (selfActive && (now - selfLastTryMs >= 1000)) {
        if (battAcked || selfTries >= 5) {
            selfActive = false;
            selfNextMs = now + ((battState == BATT_CRIT) ? BATT_REPORT_CRIT_MS : BATT_REPORT_LOW_MS);
        } else {
            sendBatteryMessage(MSG_BATT_REPORT);
            selfTries++;
            selfLastTryMs = now;
        }
    }
}

// Boot beeps: battery state
void startupBeep() {
    if (!battValid)                    buzzerStart(2, 400, 200);   // measurement failed
    else if (battState == BATT_CRIT)   buzzerStart(5, 120, 120);
    else if (battState == BATT_LOW)    buzzerStart(3, 150, 150);
    else                               buzzerStart(1, 60, 0);      // all fine
}

// ============================================================================
// OTA
// ============================================================================
// Starts an access point and a web server for firmware upload
void startOTAMode() {
    currentState = OTA_MODE;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("STOPKY-ENDPOINT-OTA", "12345678");

    httpServer.on("/", HTTP_GET, []() {
        httpServer.send(200, "text/html", otaHtml);
    });

    httpServer.on("/update", HTTP_POST, []() {
        httpServer.send(200, "text/plain", (Update.hasError()) ? "UPDATE FAILED" : "UPDATE OK! ESP32 is restarting...");
        delay(1000);
        ESP.restart();
    }, []() {
        HTTPUpload& upload = httpServer.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Update.begin(UPDATE_SIZE_UNKNOWN);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            Update.write(upload.buf, upload.currentSize);
        } else if (upload.status == UPLOAD_FILE_END) {
            Update.end(true);
        }
    });

    httpServer.begin();

    DEBUG_PRINTLN("\n==========================================");
    DEBUG_PRINTLN("!!! OTA MODE ACTIVE !!!");
    DEBUG_PRINTLN("Connect to Wi-Fi: STOPKY-ENDPOINT-OTA");
    DEBUG_PRINTLN("Open in a browser: http://192.168.4.1/");
    DEBUG_PRINTLN("==========================================\n");
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable the brownout detector
    DEBUG_BEGIN(115200);
    DEBUG_PRINTLN("\n--- Starting ESP32 Endpoint (v6.12) ---");

    // Outputs: LED and buzzer (both through NPN transistors, active HIGH)
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    pinMode(32, OUTPUT);
    digitalWrite(32, HIGH); // camera sensor into power-down mode

    // --- Double power-cycle detection (RTC memory) ---
    if (rtcMagic == RTC_MAGIC) {
        DEBUG_PRINTLN("[RTC] DOUBLE START DETECTED -> switching to OTA mode!");
        rtcMagic = 0;

        // Visual indication: the LED box blinks 3 times
        for (int k = 0; k < 3; k++) {
            digitalWrite(LED_PIN, HIGH);
            delay(100);
            digitalWrite(LED_PIN, LOW);
            delay(100);
        }

        startOTAMode();
        return;
    } else {
        DEBUG_PRINTLN("[RTC] Normal start. Arming the double-start marker.");
        rtcMagic = RTC_MAGIC;
    }

    // --- Sensor (active LOW, internal pull-up) ---
    pinMode(SENSOR_PIN, INPUT_PULLUP);

    // --- Battery: first measurement BEFORE Wi-Fi starts ---
    analogSetPinAttenuation(BATT_PIN, ADC_11db);
    battMeasureBlocking();
    DEBUG_PRINTF("[BATT] boot: %u mV, state %d, valid=%d\n", (unsigned)battMv, (int)battState, (int)battValid);
    startupBeep();

    // --- Wi-Fi & ESP-NOW (power save off, maximum TX power for 100 m+ range) ---
    if (!radioStart()) {
        DEBUG_PRINTLN("[ESP-NOW] ERROR: init failed!");
        return;
    }

    // Stagger for battery answers, derived from the MAC so that endpoints do not answer at once
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    replyStaggerMs = (unsigned long)(mac[5] % 8) * 40UL;
    DEBUG_PRINTF("[SYS] MAC %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    DEBUG_PRINTLN("[SYS] Ready in IDLE state. Waiting for the sensor...");
}

// ============================================================================
// LOOP (non-blocking, except the battery measurement on request)
// ============================================================================
void loop() {
    unsigned long now = millis();
    buzzerUpdate();

    // OTA mode: slow LED blink (500 ms)
    if (currentState == OTA_MODE) {
        httpServer.handleClient();
        digitalWrite(LED_PIN, ((now / 500) % 2) ? HIGH : LOW);
        return;
    }

    // Disarm the double-start marker after 2 s of stable operation
    static bool rtcCleared = false;
    if (!rtcCleared && now > 2000) {
        rtcMagic = 0;
        rtcCleared = true;
        DEBUG_PRINTLN("[RTC] 2 s elapsed. Double-start marker disarmed.");
    }

    // Audible feedback when the base changed the buzzer flag
    if (cfgChanged) {
        cfgChanged = false;
        if (buzzerOnHit) buzzerStart(2, 60, 60);   // enabled: two short beeps
        else             buzzerStart(1, 250, 0);   // disabled: one long beep
    }

    switch (currentState) {
        case IDLE:
            // Sensor triggered (active LOW)
            if (digitalRead(SENSOR_PIN) == LOW) {
                eventSeq++;
                memset(&eventMsg, 0, sizeof(eventMsg));
                eventMsg.key = SECRET_KEY;
                eventMsg.id = MSG_EVENT;
                eventMsg.payload[0] = eventSeq;
                eventAcked = false;
                noAckWarned = false;
                sendStartMs = now;
                lastActivityMs = now;

                // LED and buzzer at the same moment, then the first packet immediately
                digitalWrite(LED_PIN, HIGH);
                ledToggleState = true;
                blinkPreviousMillis = now;
                if (buzzerOnHit) buzzerStart(1, hitBeepMs, 0);
                esp_now_send(broadcastMAC, (uint8_t *)&eventMsg, sizeof(eventMsg));
                sendPreviousMillis = now;
                currentState = SENDING;

                DEBUG_PRINTF("\n[IDLE -> SENDING] Sensor triggered on GPIO %d (seq %u)\n", SENSOR_PIN, (unsigned)eventSeq);
                break;
            }
            idleHousekeeping(now);
            break;

        case SENDING:
            // 1. Re-send the event at maximum rate (every 20 ms) until the base acknowledges
            if (now - sendPreviousMillis >= SEND_INTERVAL_MS) {
                sendPreviousMillis = now;
                esp_now_send(broadcastMAC, (uint8_t *)&eventMsg, sizeof(eventMsg));
            }

            // 2. Diagnostics: fast LED blink (60 ms)
            if (now - blinkPreviousMillis >= BLINK_INTERVAL_MS) {
                blinkPreviousMillis = now;
                ledToggleState = !ledToggleState;
                digitalWrite(LED_PIN, ledToggleState ? HIGH : LOW);
            }

            // 3. Warning beeps once if the ACK takes too long (only when the buzzer is enabled)
            if (!noAckWarned && (now - sendStartMs > NOACK_WARN_MS)) {
                noAckWarned = true;
                if (buzzerOnHit) buzzerStart(3, 60, 60);
            }

            // 4. ACK received -> DELIVERED, LED stays on
            if (eventAcked) {
                DEBUG_PRINTLN("[SENDING -> DELIVERED] ACK received. LED is solid on.");
                currentState = DELIVERED;
                digitalWrite(LED_PIN, HIGH);
            }
            break;

        case DELIVERED:
            // LED stays on until the sensor releases (target raised / laser free)
            answerBatteryRequest(now);     // the base may check endpoints while a target is still down
            if (digitalRead(SENSOR_PIN) == HIGH) {
                DEBUG_PRINTLN("[DELIVERED -> IDLE] Sensor released. LED off.\n");
                digitalWrite(LED_PIN, LOW);
                lastActivityMs = now;
                currentState = IDLE;
            }
            break;

        default:
            break;
    }
}