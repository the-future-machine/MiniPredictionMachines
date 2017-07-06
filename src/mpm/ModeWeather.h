#pragma once

#include <Mode.h>
#include "WeatherPacket.h"

#define API_KEY         "GED0JXRS9NWB7LYV"
#define URL_TEMPLATE    F("http://api.thingspeak.com/update?api_key={k}&field1={1}&field2={2}&field3={3}&field4={4}&field5={5}&field6={6}&field7={7}&field8={8}")

class ModeWeather_ : public Mode{
public:
    ModeWeather_();
    void begin();
    void modeStart();
    void modeStop();
    void modeUpdate();
    void handleData();
    void updateMessage();

protected:
    uint8_t magicBuf[2];
    uint8_t magicPtr;
    bool inPacket;
    uint8_t dataPtr;
    WeatherUnion packet;
    uint32_t lastRead;
    uint32_t lastSeq;
    uint32_t messageTimer;
    bool messageGot;

    void resetData();
    bool checkMagic();
    void uploadThingspeak();
};

extern ModeWeather_ ModeWeather;

