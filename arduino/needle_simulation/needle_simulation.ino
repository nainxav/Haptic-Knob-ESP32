#include <Arduino.h>
#include <math.h>

#include "knob-config.h"
#include "knob.h"

Knob knob;

// =====================
// NEEDLE STATES
// =====================
enum NeedleState {
  STATE_AIR,
  STATE_CAPSULE,
  STATE_INSERTED
};

NeedleState state = STATE_AIR;

// =====================
// POSITION THRESHOLDS (WITH HYSTERESIS)
// =====================
const float CAPSULE_ENTER_V = 0.55f;   // masuk kulit
const float CAPSULE_EXIT_V  = 0.50f;   // keluar kulit (lebih rendah → stabil)

// =====================
// PUNCTURE PARAMETERS
// =====================
const float JEBOL_PENETRATION = 0.03f;   // harus cukup dalam
const float JEBOL_TIME_S     = 0.35f;   // tahan niat ±350 ms
const float PUSH_VEL_THRESH = 0.015f;  // kecepatan dorong user

float punctureTimer = 0.0f;

// =====================
// FEEL PARAMETERS
// =====================
const float SOFT_ZONE_V = 0.05f;

const float K_SOFT = 900.0f;     // preload (capsule awal)
const float K_HARD = 2600.0f;    // resistance keras
const float B_FEEL = 700.0f;     // tahanan velocity

const int PWM_FLOOR = 120;
const int PWM_MAX   = 255;

// =====================
const float DT_S = 0.01f;
const uint32_t LOOP_US = (uint32_t)(DT_S * 1e6);
const bool FLIP_DIR = false;

// =====================
static inline float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

void setup() {
  Serial.begin(115200);
  knob.begin(DT_S, 4);
  knob.reset();

  Serial.println("# Injection Simulation: AIR → CAPSULE → INSERTED");
}

void loop() {
  static uint32_t t0 = micros();
  static uint32_t n  = 0;

  uint32_t target = t0 + n * LOOP_US;
  while ((int32_t)(micros() - target) < 0) {}
  n++;

  knob.updateVelocity();

  float V     = knob.getPosition();
  float vel_f = knob.getVelFilt();

  float cmd_pwm = 0.0f;
  bool pushTowardHighV = true;

  // =====================
  // STATE MACHINE
  // =====================
  switch (state) {

    // -----------------
    case STATE_AIR:
      // benar-benar bebas
      cmd_pwm = 0.0f;

      if (V > CAPSULE_ENTER_V) {
        state = STATE_CAPSULE;
        Serial.println(">> STATE: CAPSULE (nahan)");
      }
      break;

    // -----------------
    case STATE_CAPSULE: {
      pushTowardHighV = false; // dorong balik ke luar

      float pen = V - CAPSULE_ENTER_V;
      float pen_soft = pen;
      float pen_hard = max(0.0f, pen - SOFT_ZONE_V);

      // FEEL KERAS
      cmd_pwm  = K_SOFT * pen_soft;
      cmd_pwm += K_HARD * pen_hard * pen_hard;

      // tahanan kalau user maksa masuk
      if (vel_f > 0.0f) {
        cmd_pwm += B_FEEL * vel_f;
      }

      // -----------------
      // PUNCTURE LOGIC (USER INTENT)
      // -----------------
      bool deepEnough = (pen > JEBOL_PENETRATION);
      bool pushHard   = (vel_f > PUSH_VEL_THRESH);

      if (deepEnough && pushHard) {
        punctureTimer += DT_S;
      } else {
        punctureTimer = 0.0f;
      }

      if (punctureTimer > JEBOL_TIME_S) {
        state = STATE_INSERTED;
        punctureTimer = 0.0f;
        Serial.println(">>> JEBOL! (capsule breached)");
      }

      // batal kalau ditarik keluar
      if (V < CAPSULE_EXIT_V) {
        state = STATE_AIR;
        punctureTimer = 0.0f;
        Serial.println(">> reset to AIR");
      }
      break;
    }

    // -----------------
    case STATE_INSERTED:
      // benar-benar loose (jarum sudah masuk)
      cmd_pwm = 0.0f;

      if (V < CAPSULE_EXIT_V - 0.05f) {
        state = STATE_AIR;
        Serial.println(">> STATE: AIR (needle out)");
      }
      break;
  }

  // =====================
  // MOTOR OUTPUT
  // =====================
  if (cmd_pwm > 0.0f && state != STATE_INSERTED) {
    cmd_pwm += PWM_FLOOR; // lawan deadzone
  }

  cmd_pwm = clampf(cmd_pwm, 0.0f, (float)PWM_MAX);
  uint8_t pwmOut = (uint8_t)lroundf(cmd_pwm);

  bool dirHigh = pushTowardHighV;
  if (FLIP_DIR) dirHigh = !dirHigh;

  knob.setMotor(pwmOut, dirHigh);

  // =====================
  // DEBUG
  // =====================
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 80) {
    lastPrint = millis();
    Serial.print("V=");
    Serial.print(V, 3);
    Serial.print(" vel=");
    Serial.print(vel_f, 3);
    Serial.print(" pwm=");
    Serial.print((int)pwmOut);
    Serial.print(" state=");
    if (state == STATE_AIR) Serial.println("AIR");
    else if (state == STATE_CAPSULE) Serial.println("CAPSULE");
    else Serial.println("INSERTED");
  }
}