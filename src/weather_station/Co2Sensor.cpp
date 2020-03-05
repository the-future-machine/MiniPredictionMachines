#include "Co2Sensor.h"
#include "Config.h"

Co2SensorClass CO2Sensor(CO2_PIN);

Co2SensorClass::Co2SensorClass(uint8_t pin) : _pin(pin) { }

float Co2SensorClass::getCo2Ppm() {
  int val = analogRead(_pin);
  return (float(val) / 1024.0f) * 2000.0f;
}
