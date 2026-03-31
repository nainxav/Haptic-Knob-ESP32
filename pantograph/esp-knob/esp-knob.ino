// ============================================================
// Pantograph Haptic Knob – Generic ESP32 Firmware
//
// Flash this SAME code to BOTH ESP32 boards.
// Each board: reads encoder, sends angle to PC, receives torque from PC.
// The PC (browser visualization) handles all the force math.
//
// Serial protocol (115200 baud):
//   ESP32 → PC:  "A:<unwrapped_angle>,<velocity>\n"   (200 Hz)
//   PC → ESP32:  "T:<torque>\n"                        (torque in -1..1)
// ============================================================

#include <Arduino.h>
#include "knob.h"

static float currentTorque = 0.0f;
static String inputBuffer  = "";

void setup() {
  Serial.begin(115200);
  delay(300);

  knob_init();

  Serial.println("KNOB_READY");
}

void loop() {
  // 1. Read encoder
  float angleWrapped, angleUnwrapped, velocity;
  knob_get_status(&angleWrapped, &angleUnwrapped, &velocity);

  // 2. Send angle + velocity to PC
  Serial.print("A:");
  Serial.print(angleUnwrapped, 2);
  Serial.print(",");
  Serial.println(velocity, 2);

  // 3. Check for torque commands from PC
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      inputBuffer.trim();
      if (inputBuffer.startsWith("T:")) {
        currentTorque = inputBuffer.substring(2).toFloat();
        if (currentTorque >  1.0f) currentTorque =  1.0f;
        if (currentTorque < -1.0f) currentTorque = -1.0f;
      }
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }

  // 4. Drive motor
  knob_set_torque(currentTorque);

  delay(5); // ~200 Hz
}
