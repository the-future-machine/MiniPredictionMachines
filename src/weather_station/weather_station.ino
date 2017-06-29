#include <Arduino.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <MutilaDebug.h>
#include <Mode.h>
#include <SoftwareSerial.h>
#include <Millis.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

// Includes used by other classes (must put here for Arduino IDE)
#include <OneWire.h>
#include <DallasTemperature.h>

// Include our classes for the various components
#include "HC12Serial.h"
#include "MoistureSensor.h"
#include "TemperatureSensor.h"
#include "WindspeedSensor.h"
#include "RainfallSensor.h"

// And general configuration like pins
#include "Config.h"

// Flags which will set from interrupt handlers (these need the volatile keyword)
volatile bool flgWindspeed = false;
volatile bool flgRainfall = false;
volatile bool flgTimer = false;
uint8_t rainfallDebounceCounter = 0;

// Other globals
uint32_t wakeupCounter = 0;
uint32_t lastSend = 0;
uint32_t lastRainMinute = 0;
uint32_t sendSeqNo = 0;

// Interrupt handlers
ISR(WDT_vect)
{
    flgTimer = true;
}

void rainfallIntHandler()
{
    flgRainfall = true;
}

void windspeedIntHandler()
{
    flgWindspeed = true;
}

// Regular functions
void sendData()
{
    sendSeqNo++;
    DB(F("sending data..."));
    float tempC = TemperatureSensor.getCelcius();
    bool moist = MoistureSensor.isMoist();
    float windspeed = calculateWindspeedMs(WindspeedSensor.readPulses(), wakeupCounter*WDT_PERIOD_MS);
    float rainMin = RainfallSensor.rainfallMinutes(1);
    float rainHr = RainfallSensor.rainfallMinutes(60);
    float rainDay = RainfallSensor.rainfallMinutes(60*24);
    for (uint8_t i=0; i<3; i++) {
        Serial.print(F("SQ="));  Serial.print(sendSeqNo);
        Serial.print(F(" TE=")); Serial.print(tempC);
        Serial.print(F(" MO=")); Serial.print(moist);
        Serial.print(F(" WS=")); Serial.print(windspeed);
        Serial.print(F(" RM=")); Serial.print(rainMin);
        Serial.print(F(" RH=")); Serial.print(rainHr);
        Serial.print(F(" RD=")); Serial.println(rainDay);
        delay(10);
    }
}

void goSleep()
{
#ifdef DEBUG
    // this to allow clean serial messages
    delay(10); 
#endif

    // Select sleep mode.  Here using most power-saving mode
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();     // ready, set...
    sleep_mode();       // sleep

    // Wake up here. What day it is?
    sleep_disable(); 

#ifdef DEBUG
    // this to allow clean serial messages
    delay(10);
#endif
}

void setup()
{
    Serial.begin(115200);
    DBLN(F("\n\nS:setup"));

    HC12Serial.begin(HC12_BAUD);

    MoistureSensor.begin();
    TemperatureSensor.begin();
    WindspeedSensor.begin();
    RainfallSensor.begin();

    noInterrupts(); // disable interrupts while we're setting up handlers

    // Interrupts for pulses which come in from windspeed and rainfall sensors
    attachInterrupt(digitalPinToInterrupt(WINDSPEED_PIN), windspeedIntHandler, FALLING);
    attachInterrupt(digitalPinToInterrupt(RAINFALL_PIN), rainfallIntHandler, FALLING);

    // Watchdog timer setup
    MCUSR &= ~(1<<WDRF); // Clear the reset flag
  
    /* In order to change WDE or the prescaler, we need to
     * set WDCE (This will allow updates for 4 clock cycles).
     */
    WDTCSR |= (1<<WDCE) | (1<<WDE);

    // set prescaler (0.5 second)
    WDTCSR = (0<<WDP3) | (1<<WDP2) | (0<<WDP1) | (1<<WDP0); 

    // Enable the WD interrupt (no reset)
    WDTCSR |= _BV(WDIE);

    // enable those interrupts
    interrupts();
    DBLN(F("E:setup"));
}

void loop()
{
    if (flgWindspeed) {
        DBLN(F("wind pulse"));
        flgWindspeed = false;
        WindspeedSensor.addPulse();
    }

    if (flgRainfall) {
        flgRainfall = false;
        if (millis() < 100) {
            DBLN(F("Discard spurious rain pulse on reset"));
        } else {
            if (rainfallDebounceCounter == 0) {
                DBLN(F("rain pulse"));
                RainfallSensor.addPulse();
                rainfallDebounceCounter = RAINFALL_DEBOUNCE_COUNT;
            } else {
                DBLN(F("rain pulse (discard bounce)"));
            }
        }
    }

    if (flgTimer) {
        flgTimer = false;
        wakeupCounter++;
        DB(F("wakeup #"));
        DBLN(wakeupCounter);
        uint32_t realMillis = wakeupCounter*WDT_PERIOD_MS;

        // decremement the debounce counter
        if (rainfallDebounceCounter > 0) {
            rainfallDebounceCounter--;
        }

        if (realMillis >= lastRainMinute + ((uint32_t)RAIN_SAVE_PERIOD_SEC*1000)) {
            lastRainMinute = realMillis;
            RainfallSensor.addPulseMinute();
        }

        if (realMillis >= lastSend + ((uint32_t)SEND_DATA_PERIOD_SEC*1000)) {
            lastSend = realMillis;
            sendData();
        }
    }

    goSleep();
}

