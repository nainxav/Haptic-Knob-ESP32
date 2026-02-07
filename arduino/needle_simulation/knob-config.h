#pragma once
#include <Arduino.h>

// Motor Shield from Keyes or Fundumoto or HW-723
constexpr uint8_t PWM_A   = 10;    
constexpr uint8_t DIR_A   = 12;

// Velocity filter
constexpr float VEL_ALPHA        = 0.4f;   // 0..1
constexpr float VEL_DEADBAND_VPS = 0.05f;  // V/s
