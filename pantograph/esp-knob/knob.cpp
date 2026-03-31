#include "knob.h"
#include <Wire.h>
#include <math.h>

static const uint8_t PIN_PWMA = 18;
static const uint8_t PIN_AIN1 = 19;
static const uint8_t PIN_AIN2 = 23;

static const uint8_t PIN_SDA  = 21;
static const uint8_t PIN_SCL  = 22;

static const byte AS5600_ADDR      = 0x36;
static const byte AS5600_RAW_ANGLE = 0x0C;

static uint16_t      g_lastRaw     = 0;
static int32_t       g_accumCounts = 0;
static unsigned long g_lastMicros  = 0;

static const float COUNTS_TO_DEG = 360.0f / 4096.0f;

static uint16_t knob_read_raw_angle(void) {
    Wire.beginTransmission(AS5600_ADDR);
    Wire.write(AS5600_RAW_ANGLE);
    Wire.endTransmission();
    Wire.requestFrom(AS5600_ADDR, (uint8_t)2);
    if (Wire.available() < 2) return g_lastRaw;
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    uint16_t raw = ((uint16_t)hi << 8) | lo;
    return raw & 0x0FFF;
}

static void knob_drive_motor_command(float cmd) {
    if (cmd >  1.0f) cmd =  1.0f;
    if (cmd < -1.0f) cmd = -1.0f;
    if (fabsf(cmd) < 0.02f) {
        digitalWrite(PIN_AIN1, LOW);
        digitalWrite(PIN_AIN2, LOW);
        analogWrite(PIN_PWMA, 0);
        return;
    }
    int pwm = (int)(fabsf(cmd) * 1023.0f);
    if (pwm > 1023) pwm = 1023;
    if (cmd > 0.0f) { digitalWrite(PIN_AIN1, HIGH); digitalWrite(PIN_AIN2, LOW); }
    else             { digitalWrite(PIN_AIN1, LOW);  digitalWrite(PIN_AIN2, HIGH); }
    analogWrite(PIN_PWMA, pwm);
}

static void knob_step_encoder(uint16_t* rawOut, float* wrappedOut, float* unwrappedOut, float* velOut) {
    uint16_t raw = knob_read_raw_angle();
    unsigned long nowUs = micros();
    float dt = (nowUs - g_lastMicros) / 1e6f;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.01f;
    g_lastMicros = nowUs;

    int16_t delta = (int16_t)((raw - g_lastRaw + 2048) & 0x0FFF) - 2048;
    g_lastRaw = raw;
    g_accumCounts += delta;

    if (rawOut)       *rawOut       = raw;
    if (wrappedOut)   *wrappedOut   = raw * COUNTS_TO_DEG;
    if (unwrappedOut) *unwrappedOut = g_accumCounts * COUNTS_TO_DEG;
    if (velOut)       *velOut       = delta * COUNTS_TO_DEG / dt;
}

void knob_init(void) {
    pinMode(PIN_PWMA, OUTPUT);
    pinMode(PIN_AIN1, OUTPUT);
    pinMode(PIN_AIN2, OUTPUT);
    Wire.begin(PIN_SDA, PIN_SCL);
    uint16_t raw = knob_read_raw_angle();
    g_lastRaw     = raw;
    g_accumCounts = 0;
    g_lastMicros  = micros();
}

void knob_get_status(float* angleWrapped, float* angleUnwrapped, float* velocity) {
    knob_step_encoder(NULL, angleWrapped, angleUnwrapped, velocity);
}

void knob_set_torque(float cmd) {
    knob_drive_motor_command(cmd);
}
