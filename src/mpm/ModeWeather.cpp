#include <EspApConfigurator.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Millis.h>
#include <stdio.h>
#include "ModeWeather.h"
#include "ModeRealTime.h"
#include "HC12Serial.h"
#include "OLED.h"
#include "EspID.h"
#include "Config.h"

// for system_get_free_heap_size()
extern "C" {
    #include "user_interface.h"
}

ModeWeather_ ModeWeather;

ModeWeather_::ModeWeather_() :
    lastSeq(0),
    messageCheckTimer(0),
    messageTimeout(0),
    messageGot(false)
{
    resetData();
}

void ModeWeather_::begin()
{
    DBLN(F("ModeWeather::begin"));
}

void ModeWeather_::modeStart()
{
    DBLN(F("ModeWeather::modeStart"));
}

void ModeWeather_::modeStop()
{
    DBLN(F("ModeWeather::modeStop"));
}

void ModeWeather_::modeUpdate()
{
    // Timeout
    if ((magicPtr>0 || dataPtr>0) && Millis() - lastRead > PACKET_READ_TIMEOUT_MS) {
        DBLN(F("timeout"));
        resetData();
    }

    while(HC12Serial.available()) {
        int c = HC12Serial.read();
        if (c < 0) {
            DBLN(F("serial error"));
            resetData();
            break;
        }
        lastRead = Millis();
        if (!inPacket) {
            // do the magic...
            magicBuf[magicPtr++] = (uint8_t)c;
            if (!checkMagic()) { 
                DBLN(F("bad magic"));
                resetData(); 
                break; 
            }
            if (magicPtr == 2) {
                inPacket = true;
            }
            break;
        } else {
            packet.bytes[dataPtr++] = (uint8_t)c;
            if (dataPtr >= sizeof(WeatherPacket)) {
                handleData();
                resetData();
            }
        }
    }

    // Decide if we need to update the message
    if (!messageGot && Millis() > messageCheckTimer + (OLED_MESSAGE_DELAY_S*1000)) {
        updateMessage();
    }

    // Work out if we want to timeout the message
    if (messageTimeout != 0 && ModeRealTime.unixTime() > messageTimeout) {
        // set messageTimeout to 0 so we don't end up refreshing the OLED every modeUpdate...
        messageTimeout = 0;
        displayLastData();
    }

}

void ModeWeather_::updateMessage()
{
    // TODO: it might be nice if this was non-blocking, although that does seem like quite
    // a lot of trouble...
    DBLN(F("updateMessage"));
    messageCheckTimer = Millis();

    // This might take a little while, so give the ESP a chance to do it's stuff
    yield();
    
    // example URL: /current_message?now=123456789&pubkey=XXXXX&did=0EA4F2&hmac=a65238b87fed...
    String url = API_BASE_URL;
    url += F("/current_message?now=");
    url += ModeRealTime.unixTime();
    url += F("&pubkey=");
    url += F(TIMESTREAMS_API_PUBKEY);
    url += F("&did=");
    url += EspID.get();
    url += F("&hmac=TODO");
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == 200) {
        // We expect e|message, where e is expiry time in unix seconds, and message is 
        // the text we want to display.  Note: message may contain embedded | characters
        String body = http.getString();
        DB(F("http OK, body: "));
        DBLN(body);
        uint8_t i = 0;
        String expiryStr = "";
        while(i < body.length()) {
            if (body[i] == '|') {
                i++;
                break;
            } else {
                expiryStr += body[i];
                i++;
            }
        }
        // Hopefully this doesn't do some funky integer overflow stuff...  :s
        messageTimeout = expiryStr.toInt();

        // Copy the rest of the string to "message"
        body = body.substring(i);

        DB(F("expiryStr="));
        DB(expiryStr);
        DB(F(" messageTimeout="));
        DB(messageTimeout);
        DB(F(" message=\""));
        DB(body);
        DBLN('\"');
        OLED.displayText(body.c_str(), 'C', 'M');
    } else {
        DB(F("failed, code="));
        DB(httpCode);
        DB(F(" body="));
        DBLN(http.getString());
    }
    messageGot = true;
}

void ModeWeather_::displayLastData()
{
    DBLN(F("ModeWeather::displayLastData"));

    // only display if the message from the API has:
    // 1. never been retrieved
    // 2. expired
    if (messageTimeout == 0 || messageTimeout < ModeRealTime.unixTime()) {
        OLED.clearBuffer();               
        OLED.setFont(OLED_MESSAGE_FONT);  
        OLED.drawStr(0, OLED_MESSAGE_FONT_HEIGHT, lastDataReceived.c_str());
        OLED.drawStr(0, OLED_MESSAGE_FONT_HEIGHT+((OLED_MESSAGE_FONT_HEIGHT+OLED_MESSAGE_FONT_VSEP)*2), "Temp (C):");
        OLED.drawStr(0, OLED_MESSAGE_FONT_HEIGHT+((OLED_MESSAGE_FONT_HEIGHT+OLED_MESSAGE_FONT_VSEP)*3), "Wind (m/s):");
        OLED.drawStr(0, OLED_MESSAGE_FONT_HEIGHT+((OLED_MESSAGE_FONT_HEIGHT+OLED_MESSAGE_FONT_VSEP)*4), "Moisture?");
        OLED.drawStr(0, OLED_MESSAGE_FONT_HEIGHT+((OLED_MESSAGE_FONT_HEIGHT+OLED_MESSAGE_FONT_VSEP)*5), "Rain (mm):");
        OLED.sendBuffer();
    }
}

void ModeWeather_::handleData()
{
    messageCheckTimer = Millis();
    messageGot = false;

    if (!weatherPacketCsTest(&packet.data)) {
        DBLN(F("bad checksum"));
    } else {
        DBLN(F("good checksum"));
    }

    // Only handle a given message once
    if (packet.data.sequenceNumber != lastSeq) {
        lastSeq = packet.data.sequenceNumber;
        lastDataReceived = ModeRealTime.isoTimestamp();
        DB(lastDataReceived);
        DB(F(" SQ="));
        DB(packet.data.sequenceNumber);
        DB(F(" TE="));
        DB(packet.data.temperatureC);
        DB(F(" MO="));
        DB(packet.data.moisture);
        DB(F(" WS="));
        DB(packet.data.windSpeedMs);
        DB(F(" RM="));
        DB(packet.data.rainFallMmMinute);
        DB(F(" RH="));
        DB(packet.data.rainFallMmHour);
        DB(F(" RD="));
        DB(packet.data.rainFallMmDay);
        DB(F(" BV="));
        DB(packet.data.batteryVoltage);
        DB(F(" DC="));
        DB(packet.data.dutyCycle);
        DB(F(" CS="));
        DBLN(packet.data.checksum);
        displayLastData();
        uploadThingspeak();
    }
}

void ModeWeather_::resetData()
{
    memset(magicBuf, 0, 2);
    magicPtr = 0;
    inPacket = false;
    dataPtr = 0;
}

bool ModeWeather_::checkMagic()
{
    for (uint8_t i=0; i<magicPtr && i<2; i++) {
        if (magicBuf[i] != WEATHER_PACKET_MAGIC[i]) {
            return false;
        }
    }
    return true;
}

void ModeWeather_::uploadThingspeak()
{
    HTTPClient http;
    
    String url = URL_TEMPLATE;
    url.replace("{k}", API_KEY);
    url.replace("{1}", String(packet.data.temperatureC, 3));
    url.replace("{2}", String(packet.data.moisture));
    url.replace("{3}", String(packet.data.windSpeedMs, 3));
    url.replace("{4}", String(packet.data.rainFallMmMinute, 3));
    url.replace("{5}", String(packet.data.rainFallMmHour, 3));
    url.replace("{6}", String(packet.data.rainFallMmDay, 3));
    url.replace("{7}", String(packet.data.batteryVoltage, 3));
    url.replace("{8}", String(system_get_free_heap_size()));

    yield();

    http.begin(url.c_str());
    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        DB(F("Thingspeak response: "));
        DBLN(payload);
    }

}
