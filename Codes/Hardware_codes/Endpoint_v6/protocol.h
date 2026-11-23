/*
 * protocol.h - ESP-NOW protocol shared by the Base and the Endpoint sketches (v6.12).
 * Keep this file IDENTICAL in both sketch folders.
 *
 * All endpoint -> base messages are broadcast (an endpoint does not need to know the
 * base MAC address). The base identifies an endpoint by the sender MAC address and maps it
 * to a lane/segment in its registry.
 */
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Message types (struct_message.id)
// ---------------------------------------------------------------------------
// Endpoint -> Base
#define MSG_EVENT        1   // Sensor triggered (target hit / laser gate crossed). payload[0] = sequence number
#define MSG_HELLO        2   // Endpoint booted and wants its configuration. payload = battery (see below)
#define MSG_BATT_REPORT  5   // Battery report (answer to a request, or low-battery self-report). payload = battery
// Base -> Endpoint
#define MSG_CONFIG       3   // Settings. payload[0] = CFG_FLAG_* bits, payload[1] = beep length in 10 ms units (0 = unknown)
#define MSG_BATT_REQ     4   // Battery request (broadcast). payload[0] = BATT_REQ_MEASURE: measure first (the endpoint switches
                             // its radio off for ~0.3 s, only sent while no run is in progress), 0: report the cached value
#define MSG_ACK          6   // Acknowledge. payload[0] = acknowledged type, payload[1] = sequence (for MSG_EVENT)
#define MSG_OTA          99  // Remote command: reboot endpoint into OTA mode

// ---------------------------------------------------------------------------
// Payload definitions
// ---------------------------------------------------------------------------
// MSG_BATT_REQ payload[0]
#define BATT_REQ_MEASURE  1

// MSG_CONFIG payload[0] flags
#define CFG_FLAG_BUZZER_ON_HIT  0x01   // beep together with the 3W LED when the sensor triggers

// Battery payload (MSG_HELLO, MSG_BATT_REPORT):
//   payload[0..1] = battery voltage in mV (little endian), payload[2] = BATT_FLAG_* bits
#define BATT_FLAG_STATE_MASK  0x03     // 0 = OK, 1 = LOW, 2 = CRITICAL
#define BATT_FLAG_ERROR       0x04     // no valid measurement yet / implausible reading (divider fault?)
#define BATT_FLAG_STALE       0x80     // the last measurement failed, the value is older

// ---------------------------------------------------------------------------
// Frame (16 bytes with padding)
// ---------------------------------------------------------------------------
typedef struct struct_message {
    uint32_t key;         // shared secret (SECRET_KEY from config.h)
    int      id;          // message type, MSG_*
    uint8_t  payload[7];  // type specific
} struct_message;

static inline void packBattery(struct_message &m, uint16_t mv, uint8_t flags) {
    m.payload[0] = (uint8_t)(mv & 0xFF);
    m.payload[1] = (uint8_t)(mv >> 8);
    m.payload[2] = flags;
}
static inline uint16_t unpackBatteryMv(const struct_message &m) {
    return (uint16_t)(m.payload[0] | (m.payload[1] << 8));
}
static inline uint8_t unpackBatteryFlags(const struct_message &m) {
    return m.payload[2];
}