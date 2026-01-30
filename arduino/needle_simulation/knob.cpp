#include "knob.h"
#include "knob-config.h"
#include <math.h>

Knob::Knob() { }

void Knob::begin(float dt, uint8_t ads_gain) {
  dt_ = dt;
  if (dt_ < 0.001f) dt_ = 0.001f;
  if (dt_ > 0.100f) dt_ = 0.100f;

  pinMode(DIR_A, OUTPUT);
  setupHighFreqPwmOnTimer1_();
  
  
  if (!ads_.begin()) {
    Serial.println("ADS1115 NOT OK!");
    while (1) {}
  }

  Serial.println("ADS1115 OK!");

  if (ads_gain == 0){
    ads_.setGain(GAIN_TWOTHIRDS);
    ADS_LSB_V_ = 0.0001875f;
  }
  else if (ads_gain == 1){
    ads_.setGain(GAIN_ONE);
    ADS_LSB_V_ = 0.000125;
  }  
  else if (ads_gain == 2){
    ads_.setGain(GAIN_TWO);
    ADS_LSB_V_ = 0.0000625;
  }
  else if (ads_gain == 4){
    ads_.setGain(GAIN_FOUR);
    ADS_LSB_V_ = 0.00003125;
  }
  else if (ads_gain == 8){
    ads_.setGain(GAIN_EIGHT);
    ADS_LSB_V_ = 0.000015625;
  }

  nowVoltage_  = readVoltage_();
  lastVoltage_ = nowVoltage_;
  velRaw_  = 0.0f;
  velFilt_ = 0.0f;
}

void Knob::setMotor(uint8_t pwm, bool dirHigh) {
  if (pwm > 255) pwm = 255;
  digitalWrite(DIR_A, dirHigh ? HIGH : LOW);
  analogWrite(PWM_A, pwm);
}

float Knob::readVoltage_() {
  int16_t adc0 = ads_.readADC_SingleEnded(0);
  return adc0 * ADS_LSB_V_;
}

void Knob::updateVelocity() {
  // Sample position
  nowVoltage_ = readVoltage_();

  // Raw derivative
  velRaw_ = (nowVoltage_ - lastVoltage_) / dt_;
  lastVoltage_ = nowVoltage_;

  // Low-pass filter
  velFilt_ = (1.0f - VEL_ALPHA) * velFilt_ + VEL_ALPHA * velRaw_;

  // Deadband
  if (fabsf(velFilt_) < VEL_DEADBAND_VPS) velFilt_ = 0.0f;
}

void Knob::reset(float voltageNow) {
  nowVoltage_  = isnan(voltageNow) ? readVoltage_() : voltageNow;
  lastVoltage_ = nowVoltage_;
  velFilt_ = 0.0f;
  velRaw_  = 0.0f;
}

float Knob::getPosition() {  // keep your spelling
  return nowVoltage_;
}

float Knob::getVelFilt() {
  return velFilt_;
}

float Knob::getVelRaw() {
  return velRaw_;
}

// High frequency PWM on Timer2, pin 3 (OC2B) ~62.5kHz on AVR @16MHz
void Knob::setupHighFreqPwmOnTimer2_() {
  pinMode(PWM_A, OUTPUT);
  TCCR2A = 0;
  TCCR2B = 0;
  TCCR2A |= (1 << WGM20) | (1 << WGM21);
  TCCR2B |= (1 << CS20);
  TCCR2A |= (1 << COM2B1);
  analogWrite(PWM_A, 0);
}

void Knob::setupHighFreqPwmOnTimer1_() {
  pinMode(PWM_A, OUTPUT);
  TCCR1B = (TCCR1B & 0b11111000) | 0x01;
}
