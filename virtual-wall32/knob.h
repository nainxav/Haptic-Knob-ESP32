#ifndef KNOB_H
#define KNOB_H

#include <Arduino.h>

// Fixed hardware mapping inside knob.cpp:
//   Motor:  PWMA=D5, AIN1=D6, AIN2=D7
//   AS5600: SDA=D2, SCL=D1

// Initialize knob module with fixed hardware mapping.
void knob_init(void);

// Define wall region in degrees (wrapped 0..360).
// Free region is [centerDeg - halfWidthDeg, centerDeg + halfWidthDeg].
void knob_set_wall_region_deg(float centerDeg, float halfWidthDeg);

// Set wall gains:
//   kp_wall:   spring strength (cmd per degree beyond wall)
//   kd_wall:   damping on velocity when against the wall
//   friction:  viscous friction inside free region (cmd per deg/s)
void knob_set_wall_gains(float kp_wall, float kd_wall, float friction);

// Virtual wall update: call this from loop().
void knob_update_wall(void);

// ... kode lama ...

// --- TAMBAHAN BARU UNTUK MODE NEEDLE ---

// Dapatkan data sensor (posisi dan kecepatan) untuk kalkulasi fisika kustom
void knob_get_status(float* angleWrapped, float* angleUnwrapped, float* velocity);

// Kirim perintah torsi langsung (-1.0 sampai 1.0)
void knob_set_torque(float cmd);

#endif // KNOB_H
