#include "knob.h"
#include <Wire.h>
#include <math.h>

// ====== FIXED HARDWARE PINS ======

// ===== MOTOR DRIVER =====
static const uint8_t PIN_PWMA = 18;   // PWM → IO18
static const uint8_t PIN_AIN1 = 19;   // DIR1
static const uint8_t PIN_AIN2 = 23;   // DIR2

// ===== I2C AS5600 =====
static const uint8_t PIN_SDA  = 21;   // SDA
static const uint8_t PIN_SCL  = 22;   // SCL

// ====== AS5600 registers ======
static const byte AS5600_ADDR      = 0x36;
static const byte AS5600_RAW_ANGLE = 0x0C; // high byte, low byte at 0x0D

// ====== Encoder state ======
static uint16_t      g_lastRaw     = 0;
static int32_t       g_accumCounts = 0;      // multi-turn count (for logging)
static unsigned long g_lastMicros  = 0;

static const float COUNTS_TO_DEG = 360.0f / 4096.0f;

// ====== Virtual wall parameters ======
static float g_wallCenterDeg    = 180.0f;  // default center
static float g_wallHalfWidthDeg = 40.0f;   // default ±40° free region
static float g_wallKp           = 0.02f;   // spring strength (cmd / deg)
static float g_wallKd           = 0.002f;  // damping on velocity (cmd / (deg/s))
static float g_wallFriction     = 0.0005f; // viscous friction inside window

// Debug print throttle
static unsigned long g_lastPrintMs = 0;

// ====== Homing parameters ======
static const float HOME_TARGET_DEG   = 180.0f; // where we want to end up
static const float HOME_ANGLE_TOL    = 5.0f;   // deg
static const float HOME_VEL_TOL      = 5.0f;   // deg/s
static const float HOME_KP           = -0.001f; // tuned for your current sign convention (negative)
static const float HOME_MAX_CMD      = 0.03f;   // max |cmd|
static const unsigned long HOME_MAX_MS = 2000;// max homing duration

// ---------- internal helpers ----------

// Read raw 12-bit angle from AS5600
static uint16_t knob_read_raw_angle(void)
{
    Wire.beginTransmission(AS5600_ADDR);
    Wire.write(AS5600_RAW_ANGLE);
    Wire.endTransmission();

    Wire.requestFrom(AS5600_ADDR, (uint8_t)2);
    if (Wire.available() < 2) {
        // fallback: return last good value
        return g_lastRaw;
    }

    uint8_t highByte = Wire.read();
    uint8_t lowByte  = Wire.read();

    uint16_t raw = ((uint16_t)highByte << 8) | lowByte;
    raw &= 0x0FFF;   // 12-bit
    return raw;
}

// cmd in [-1,1], sign = direction, magnitude = PWM fraction
static void knob_drive_motor_command(float cmd)
{
    // clamp
    if (cmd >  1.0f) cmd =  1.0f;
    if (cmd < -1.0f) cmd = -1.0f;

    // deadband: small torque -> stop motor
    if (fabsf(cmd) < 0.02f) {
        digitalWrite(PIN_AIN1, LOW);
        digitalWrite(PIN_AIN2, LOW);
        analogWrite(PIN_PWMA, 0);
        return;
    }

    int pwm = (int)(fabsf(cmd) * 1023.0f);
    if (pwm > 1023) pwm = 1023;

    if (cmd > 0.0f) {
        digitalWrite(PIN_AIN1, HIGH);
        digitalWrite(PIN_AIN2, LOW);
    } else {
        digitalWrite(PIN_AIN1, LOW);
        digitalWrite(PIN_AIN2, HIGH);
    }

    analogWrite(PIN_PWMA, pwm);
}

// Shortest signed difference between angles in degrees: (-180, 180]
static float shortest_deg_diff(float targetDeg, float currentDeg)
{
    float d = targetDeg - currentDeg;
    while (d > 180.0f)  d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

// Shared encoder step: compute raw, dt, delta, unwrapped angle, velocity.
static void knob_step_encoder(uint16_t* rawOut,
                              float*    angleWrappedDegOut,
                              float*    angleUnwrappedDegOut,
                              float*    velDegPerSecOut)
{
    // 1) Read encoder
    uint16_t raw = knob_read_raw_angle();

    // 2) Time step (seconds)
    unsigned long nowUs = micros();
    float dt = (nowUs - g_lastMicros) / 1e6f;
    if (dt <= 0.0f || dt > 0.1f) {   // guard against weird dt
        dt = 0.01f;
    }
    g_lastMicros = nowUs;

    // 3) Flip-safe delta in counts (signed, -2048..+2047)
    int16_t delta = (int16_t)((raw - g_lastRaw + 2048) & 0x0FFF) - 2048;
    g_lastRaw = raw;

    // 4) Accumulate multi-turn counts & compute angle / velocity
    g_accumCounts += delta;  // can grow positive or negative (for plotting)

    float angle_wrapped_deg   = raw           * COUNTS_TO_DEG;   // 0..360-ish
    float angle_unwrapped_deg = g_accumCounts * COUNTS_TO_DEG;   // multi-turn
    float vel_deg_s           = delta         * COUNTS_TO_DEG / dt;

    if (rawOut)               *rawOut               = raw;
    if (angleWrappedDegOut)   *angleWrappedDegOut   = angle_wrapped_deg;
    if (angleUnwrappedDegOut) *angleUnwrappedDegOut = angle_unwrapped_deg;
    if (velDegPerSecOut)      *velDegPerSecOut      = vel_deg_s;
}

// Simple homing routine: gently move knob toward HOME_TARGET_DEG.
static void knob_home_to_center(void)
{
    Serial.println("Homing to ~center...");

    unsigned long startMs = millis();

    while (millis() - startMs < HOME_MAX_MS) {
        uint16_t raw;
        float angleWrappedDeg;
        float angleUnwrappedDeg;
        float velDegPerSec;

        knob_step_encoder(&raw, &angleWrappedDeg, &angleUnwrappedDeg, &velDegPerSec);

        float errDeg = shortest_deg_diff(HOME_TARGET_DEG, angleWrappedDeg);

        // Check if we're close enough and not moving much
        if (fabsf(errDeg) < HOME_ANGLE_TOL && fabsf(velDegPerSec) < HOME_VEL_TOL) {
            break;
        }

        // PD control toward center (note: HOME_KP, HOME_KD are negative to match your current wiring)
        float cmd = HOME_KP * errDeg;

        // Clamp homing command
        if (cmd >  HOME_MAX_CMD) cmd =  HOME_MAX_CMD;
        if (cmd < -HOME_MAX_CMD) cmd = -HOME_MAX_CMD;

        knob_drive_motor_command(cmd);

        delay(5); // ~200 Hz
    }

    // Stop motor at end of homing
    knob_drive_motor_command(0.0f);

    Serial.println("Homing done.");
}

// ---------- public API ----------

void knob_init(void)
{
    // Motor pins
    pinMode(PIN_PWMA, OUTPUT);
    pinMode(PIN_AIN1, OUTPUT);
    pinMode(PIN_AIN2, OUTPUT);     // 20 kHz PWM

    // I2C for AS5600
    Wire.begin(PIN_SDA, PIN_SCL);

    // Initial encoder read / timing
    uint16_t raw = knob_read_raw_angle();
    g_lastRaw      = raw;
    g_accumCounts  = 0;
    g_lastMicros   = micros();
    g_lastPrintMs  = 0;

    Serial.println("knob_init: AS5600 + TB6612FNG ready (virtual wall mode)");

    // Slowly move knob toward HOME_TARGET_DEG (~180°)
    knob_home_to_center();
}

void knob_set_wall_region_deg(float centerDeg, float halfWidthDeg)
{
    g_wallCenterDeg    = centerDeg;
    g_wallHalfWidthDeg = halfWidthDeg;
}

void knob_set_wall_gains(float kp_wall, float kd_wall, float friction)
{
    g_wallKp       = kp_wall;
    g_wallKd       = kd_wall;
    g_wallFriction = friction;
}

void knob_update_wall(void)
{
    unsigned long nowMs = millis();

    uint16_t raw;
    float angleWrappedDeg;
    float angleUnwrappedDeg;
    float velDegPerSec;

    knob_step_encoder(&raw, &angleWrappedDeg, &angleUnwrappedDeg, &velDegPerSec);

    // Wall edges in wrapped degrees
    float lower = g_wallCenterDeg - g_wallHalfWidthDeg;
    float upper = g_wallCenterDeg + g_wallHalfWidthDeg;

    float cmd = 0.0f;

    if (angleWrappedDeg >= lower && angleWrappedDeg <= upper) {
        // Inside free region:
        // Only friction, so if vel ~ 0, cmd ~ 0 and the motor does NOT move by itself.
        if (fabsf(velDegPerSec) > 1.0f) { // small threshold to avoid chasing noise
            cmd = -g_wallFriction * velDegPerSec;
        } else {
            cmd = 0.0f;
        }
    } else if (angleWrappedDeg > upper) {
        // Beyond upper wall: push back toward upper
        float disp = angleWrappedDeg - upper;    // positive
        cmd = -g_wallKp * disp - g_wallKd * velDegPerSec;
    } else { // angleWrappedDeg < lower
        // Beyond lower wall: push back toward lower
        float disp = angleWrappedDeg - lower;    // negative
        cmd = -g_wallKp * disp - g_wallKd * velDegPerSec;
    }

    knob_drive_motor_command(cmd);

    // Debug print (CSV: raw,wrapped,unwrapped,vel,cmd)
    if (nowMs - g_lastPrintMs > 50) { // 20 Hz print
        g_lastPrintMs = nowMs;
        Serial.print(raw);
        Serial.print(",");
        Serial.print(angleWrappedDeg, 1);
        Serial.print(",");
        Serial.print(angleUnwrappedDeg, 1);
        Serial.print(",");
        Serial.print(velDegPerSec, 2);
        Serial.print(",");
        Serial.println(cmd, 3);
    }

    delay(5); // ~200 Hz loop
}

// ... kode lama ...

// --- IMPLEMENTASI TAMBAHAN ---

void knob_get_status(float* angleWrapped, float* angleUnwrapped, float* velocity)
{
    // Panggil fungsi internal static untuk update data
    knob_step_encoder(NULL, angleWrapped, angleUnwrapped, velocity);
}

void knob_set_torque(float cmd)
{
    // Panggil fungsi internal static untuk drive motor
    knob_drive_motor_command(cmd);
}
