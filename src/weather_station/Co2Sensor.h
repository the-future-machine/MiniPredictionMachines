#pragma once

#include <stdint.h>

class Co2SensorClass {
public:
    Co2SensorClass(uint8_t pin);
    float getCo2Ppm();
private:
    uint8_t _pin;
};

extern Co2SensorClass CO2Sensor;
