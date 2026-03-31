#ifndef KNOB_H
#define KNOB_H

#include <Arduino.h>

void knob_init(void);
void knob_get_status(float* angleWrapped, float* angleUnwrapped, float* velocity);
void knob_set_torque(float cmd);

#endif
