#pragma once
#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

class Knob {
public:
  explicit Knob();

  void begin(float dt = 0.01f, uint8_t ads_gain = 1);                    
  void setMotor(uint8_t pwm, bool dirHigh);  

  void  updateVelocity();    // filtered velocity (V/s)
  void  reset(float voltageNow = NAN);

  float getPosition();
  float getVelFilt();
  float getVelRaw();

private:
  Adafruit_ADS1115   ads_;
  float ADS_LSB_V_   = 0.0f;

  float nowVoltage_  = 0.0f;
  float lastVoltage_ = 0.0f;
  float velFilt_     = 0.0f;
  float velRaw_      = 0.0f;
  float dt_          = 0.01f;

  float readVoltage_();       // volts
  static void setupHighFreqPwmOnTimer2_();
  static void setupHighFreqPwmOnTimer1_();
};
