/**************************************************************
   This work is based on many others.
   If published then under whatever licence free to be used,
   distibuted, changed and whatsoever.
   This Work is based on many others 
   and heavily modified for my personal use.
   It is basically based on the following great developments:
   - Adafruit Neopixel https://github.com/adafruit/Adafruit_NeoPixel
   - WS2812FX library https://github.com/kitesurfer1404/WS2812FX
   - fhem esp8266 implementation - Idea from https://github.com/sw-home/FHEM-LEDStripe 
   - FastLED library - see http://www.fastLed.io
   - ESPWebserver - see https://github.com/jasoncoon/esp8266-fastled-webserver
  
  My GIT source code storage
  https://github.com/tobi01001/LED_Stripe_Dynamic_web_conf

  Done by tobi01001

  **************************************************************

  MIT License

  Copyright (c) 2018 tobi01001

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.


 **************************************************************/

#include <FS.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

#define FASTLED_ESP8266_RAW_PIN_ORDER
#define FASTLED_ESP8266_DMA
#define FASTLED_USE_PROGMEM 1

#include "debug_help.h"

#include "defaults.h"

extern "C"
{
#include "user_interface.h"
}

// new approach starts here:
#include "led_strip.h"

// The delay being used for several init phases.
#ifdef DEBUG
#define INITDELAY 500
#else
#define INITDELAY 2
#endif
// TODO: Implement and use a "defaults.h" to enable specific defaults?

/* use build flags to define these */
#ifndef LED_NAME
#error "You need to give your LED a Name (build flag e.g. '-DLED_NAME=\"My LED\"')!"
#endif

#define BUILD_VERSION ("0.9_Segs_")
#ifndef BUILD_VERSION
#error "We need a SW Version and Build Version!"
#endif

#ifdef DEBUG
String build_version = BUILD_VERSION + String("DEBUG ") + String(__TIMESTAMP__);
#else
String build_version = BUILD_VERSION + String(__TIMESTAMP__);
#endif

#ifndef LED_COUNT
#error "You need to define the number of Leds by LED_COUNT (build flag e.g. -DLED_COUNT=50)"
#endif

/* Definitions for network usage */
/* maybe move all wifi stuff to separate files.... */
#define WIFI_TIMEOUT 5000
ESP8266WebServer server(80);
WebSocketsServer *webSocketsServer; // webSocketsServer = WebSocketsServer(81);

String AP_SSID = LED_NAME + String(ESP.getChipId());

/* END Network Definitions */

//flag for saving data
bool shouldSaveRuntime = false;

#include "FSBrowser.h"

// function Definitions
void saveEEPROMData(void),
    initOverTheAirUpdate(void),
    setupWiFi(void),
    handleSet(void),
    handleNotFound(void),
    handleGetModes(void),
    handleStatus(void),
    factoryReset(void),
    handleResetRequest(void),
    setupWebServer(void),
    showInitColor(CRGB Color),
    sendInt(String name, uint16_t value),
    sendString(String name, String value),
    sendAnswer(String jsonAnswer),
    broadcastInt(String name, uint16_t value),
    broadcastString(String name, String value),
    webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length),
    targetPaletteChanged(uint8_t num),
    modeChanged(uint8_t num),
    clearEEPROM(void);

const String
pals_setup(void);

uint32
getResetReason(void);

void targetPaletteChanged(uint8_t pal)
{
  sendAnswer("\"palette\": " + String(pal) + ", \"palette name\": \"" +
             (String)strip->getPalName(pal) + "\"");
  broadcastInt("pa", pal);
}

void modeChanged(uint8_t num)
{
  sendAnswer("\"mode\": 3, \"modename\": \"" +
             (String)strip->getModeName(num) +
             "\", \"wsfxmode\": " + String(num));
  // let anyone connected know what we just did
  broadcastInt("mo", num);
}

// used to send an answer as INT to the calling http request
// TODO: Use one answer function with parameters being overloaded
void sendInt(String name, uint16_t value)
{
  String answer = F("{ ");
  answer += F("\"currentState\" : { \"");
  answer += name;
  answer += F("\": ");
  answer += value;
  answer += " } }";
  DEBUGPRNT("Send HTML respone 200, application/json with value: " + answer);
  server.send(200, "application/json", answer);
}

// used to send an answer as String to the calling http request
// TODO: Use one answer function with parameters being overloaded
void sendString(String name, String value)
{
  String answer = F("{ ");
  answer += F("\"currentState\" : { \"");
  answer += name;
  answer += F("\": \"");
  answer += value;
  answer += "\" } }";
  DEBUGPRNT("Send HTML respone 200, application/json with value: " + answer);

  server.send(200, "application/json", answer);
}

// used to send an answer as JSONString to the calling http request
// send answer can embed a complete json string instead of a single name / value pair.
void sendAnswer(String jsonAnswer)
{
  String answer = "{ \"currentState\": { " + jsonAnswer + "} }";
  server.send(200, "application/json", answer);
}

// broadcasts the name and value to all websocket clients
void broadcastInt(String name, uint16_t value)
{
  String json = "{\"name\":\"" + name + "\",\"value\":" + String(value) + "}";
  DEBUGPRNT("Send websocket broadcast with value: " + json);
  webSocketsServer->broadcastTXT(json);
}

// broadcasts the name and value to all websocket clients
// TODO: One function with parameters being overloaded.
void broadcastString(String name, String value)
{
  String json = "{\"name\":\"" + name + "\",\"value\":\"" + String(value) + "\"}";
  DEBUGPRNT("Send websocket broadcast with value: " + json);
  webSocketsServer->broadcastTXT(json);
}

// calculates a simple CRC over the given buffer and length
unsigned int calc_CRC16(unsigned int crc, unsigned char *buf, int len)
{
  for (int pos = 0; pos < len; pos++)
  {
    crc ^= (unsigned int)buf[pos]; // XOR byte into least sig. byte of crc

    for (int i = 8; i != 0; i--)
    { // Loop over each bit
      if ((crc & 0x0001) != 0)
      {            // If the LSB is set
        crc >>= 1; // Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else         // Else LSB is not set
        crc >>= 1; // Just shift right
    }
  }
  return crc;
}

void print_segment(WS2812FX::segment * s)
{
  DEBUGPRNT("Printing segment content");
  #ifdef DEBUG
  Serial.println("\t\tCRC:\t" + String(s->CRC));
  Serial.println("\t\tisRunning:\t" + String(s->isRunning));
  Serial.println("\t\tReverse:\t" + String(s->reverse));
  Serial.println("\t\tInverse:\t" + String(s->inverse));
  Serial.println("\t\tMirror:\t" + String(s->mirror));
  Serial.println("\t\tAutoplay:\t" + String(s->autoplay));  
  Serial.println("\t\tAutopal:\t" + String(s->autoPal));
  Serial.println("\t\tbeat88:\t" + String(s->beat88));
  Serial.println("\t\thueTime:\t" + String(s->hueTime));
  Serial.println("\t\tmilliamps:\t" + String(s->milliamps));
  Serial.println("\t\tautoplayduration:\t" + String(s->autoplayDuration));
  Serial.println("\t\tautopalduration:\t" + String(s->autoPalDuration));
  Serial.println("\t\tsegements:\t" + String(s->segments));
  Serial.println("\t\tcooling:\t" + String(s->cooling));
  Serial.println("\t\tsparking:\t" + String(s->sparking));
  Serial.println("\t\ttwinkleSpeed:\t" + String(s->twinkleSpeed));
  Serial.println("\t\ttwinkleDensity:\t" + String(s->twinkleDensity));
  Serial.println("\t\tnumBars:\t" + String(s->numBars));
  Serial.println("\t\tmode:\t" + String(s->mode));
  Serial.println("\t\tfps:\t" + String(s->fps));
  Serial.println("\t\tdeltaHue:\t" + String(s->deltaHue));
  Serial.println("\t\tblur:\t" + String(s->blur));
  Serial.println("\t\tdamping:\t" + String(s->damping));
  Serial.println("\t\tdithering:\t" + String(s->dithering));
  Serial.println("\t\tsunrisetime:\t" + String(s->sunrisetime));
  Serial.println("\t\ttargetBrightness:\t" + String(s->targetBrightness));
  Serial.println("\t\ttargetPaletteNum:\t" + String(s->targetPaletteNum));
  Serial.println("\t\tcurrentPaletteNum:\t" + String(s->currentPaletteNum));
  Serial.println("\t\tblendType:\t" + String(s->blendType));
  Serial.println("\t\tcolorTemp:\t" + String(s->colorTemp));
  #endif
  DEBUGPRNT("done");
}

// write runtime data to EEPROM (when required by "shouldSave Runtime")
void saveEEPROMData(void)
{
  if (!shouldSaveRuntime)
    return;
  shouldSaveRuntime = false;
  DEBUGPRNT("\tGoing to store runtime on EEPROM...");
  // we will store the complete segment data
  
  WS2812FX::segment seg = *strip->getSegment();

  DEBUGPRNT("WS2812 segment:");
  print_segment(strip->getSegment());
  DEBUGPRNT("copied segment:");
  print_segment(&seg);


  seg.CRC = (uint16_t)calc_CRC16(0x5a5a,(unsigned char *)&seg + 2, sizeof(seg) - 2);

  strip->setCRC(seg.CRC);

  DEBUGPRNT("\tSegment size: " + String(strip->getSegmentSize())+ "\tCRC size: " + String(strip->getCRCsize()));
  DEBUGPRNT("\tCRC calculated " + String(seg.CRC) + "\tCRC stored " + String(strip->getCRC()));

  DEBUGPRNT("WS2812 segment with new CRC:");
  print_segment(strip->getSegment());

  // write the data to the EEPROM
  EEPROM.put(0, seg);
  // as we work on ESP, we also need to commit the written data.
  EEPROM.commit();

  DEBUGPRNT("EEPROM write finished...");
}


// reads the stored runtime data from EEPROM
// must be called after everything else is already setup to be working
// otherwise this may terribly fail and could override what was read already
void readRuntimeDataEEPROM(void)
{
  DEBUGPRNT("\tReading Config From EEPROM...");
  // copy segment...
  WS2812FX::segment seg;
  //read the configuration from EEPROM into RAM
  EEPROM.get(0, seg);

  DEBUGPRNT("Segment after Reading:");
  print_segment(&seg);

  // calculate the CRC over the data being stored in the EEPROM (except the CRC itself)
  uint16_t mCRC = (uint16_t)calc_CRC16(0x5a5a, (unsigned char *)&seg + 2, sizeof(seg) - 2);

  
  DEBUGPRNT("\tCRC calculated " + String(mCRC) + "\tCRC stored " + String(seg.CRC));
  
  // we have a matching CRC, so we update the runtime data.
  if (seg.CRC == mCRC)
  {
    DEBUGPRNT("\tWe got a CRC match!...");
    
    (*strip->getSegment()) = seg;
    #pragma message "This needs to be corrected. Now set to \"working\" only"
    //currentEffect = FX_WS2812;
    //stripIsOn = strip->isRunning();
    strip->setIsRunning(true);
    strip->setPower(true);
    strip->init();
  }
  else // load defaults
  {
    DEBUGPRNT("\tWe got NO NO NO CRC match!!!");
    DEBUGPRNT("Loading default Data...");
    strip->resetDefaults();
    #pragma message "This needs to be corrected. Now set to \"working\" only"
    strip->setIsRunning(true);
    strip->setPower(true);
  }


  // no need to save right now. next save should be after /set?....
  shouldSaveRuntime = false;
  strip->setTransition();
}

// we do not want anything to distrub the OTA
// therefore there is a flag which could be used to prevent from that...
// However, seems that a connection being active via websocket interrupts
// even if the web socket server is stopped??
bool OTAisRunning = false;

void initOverTheAirUpdate(void)
{
  DEBUGPRNT("Initializing OTA capabilities....");
  showInitColor(CRGB::Blue);
  delay(INITDELAY);
  /* init OTA */
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  ArduinoOTA.setRebootOnSuccess(true);

  // TODO: Implement Hostname in config and WIFI Settings?

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("esp8266Toby01");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    DEBUGPRNT("OTA start");

    strip->setIsRunning(false);
    strip->setPower(false);
    // the following is just to visually indicate the OTA start
    // which is done by blinking the complete stripe in different colors from yellow to green
    uint8_t factor = 85;
    for (uint8_t c = 0; c < 4; c++)
    {

      for (uint16_t i = 0; i < strip->getStripLength(); i++)
      {
        uint8_t r = 256 - (c * factor);
        uint8_t g = c > 0 ? (c * factor - 1) : (c * factor);
        //strip.setPixelColor(i, r, g, 0);
        strip->leds[i] = CRGB(strip_color32(r, g, 0));
      }
      strip->show();
      delay(250);
      for (uint16_t i = 0; i < strip->getStripLength(); i++)
      {
        strip->leds[i] = CRGB::Black;
      }
      strip->show();
      delay(500);
    }
    // we need to delete the websocket server in order to have OTA correctly running.
    delete webSocketsServer;
    // we stop the webserver to not get interrupted....
    server.stop();
    // we indicate our sktch that OTA is currently running (should actually not be required)
    OTAisRunning = true;
  });

  // what to do if OTA is finished...
  ArduinoOTA.onEnd([]() {
    DEBUGPRNT("OTA end");
    // OTA finished.
    // We fade out the green Leds being activated during OTA.
    for (uint8_t i = strip->leds[0].green; i > 0; i--)
    {
      for (uint16_t p = 0; p < strip->getStripLength(); p++)
      {
        strip->leds[p].subtractFromRGB(2);
        //strip.setPixelColor(p, 0, i-1 ,0);
      }
      strip->show();
      delay(2);
    }
    // indicate that OTA is no longer running.
    OTAisRunning = false;
    // no need to reset ESP as this is done by the OTA handler by default
  });
  // show the progress on the strips as well to be informed if anything gets stuck...
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DEBUGPRNT("Progress: " + String((progress / (total / 100))));

    // OTA Update will show increasing green LEDs during progress:
    uint16_t progress_value = progress * 100 / (total / strip->getStripLength());
    uint16_t pixel = (uint16_t)(progress_value / 100);
    uint16_t temp_color = progress_value - (pixel * 100);
    if (temp_color > 255)
      temp_color = 255;

    strip->leds[pixel] = strip_color32(0, (uint8_t)temp_color, 0);
    strip->show();
  });

  // something went wrong, we gonna show an error "message" via LEDs.
  ArduinoOTA.onError([](ota_error_t error) {
    DEBUGPRNT("Error[%u]: " + String(error));
    if (error == OTA_AUTH_ERROR) {
      DEBUGPRNT("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR) {
      DEBUGPRNT("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR) {
      DEBUGPRNT("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR) {
      DEBUGPRNT("Receive Failed");
    }
    else if (error == OTA_END_ERROR) {
      DEBUGPRNT("End Failed");
    }
    // something went wrong during OTA.
    // We will fade in to red...
    for (uint16_t c = 0; c < 256; c++)
    {
      for (uint16_t i = 0; i < strip->getStripLength(); i++)
      {
        //strip.setPixelColor(i,(uint8_t)c,0,0);
        strip->leds[i] = strip_color32((uint8_t)c, 0, 0);
      }
      strip->show();
      delay(2);
    }
    // We wait 10 seconds and then restart the ESP...
    delay(10000);
    ESP.restart();
  });
  // start the service
  ArduinoOTA.begin();
  DEBUGPRNT("OTA capabilities initialized....");
  showInitColor(CRGB::Green);
  delay(INITDELAY);
  showInitColor(CRGB::Black);
  delay(INITDELAY);
}

// for DEBUG purpose without Serial connection...
void showInitColor(CRGB Color)
{
#ifdef DEBUG
  FastLED.showColor(Color);
#endif
}

// setup the Wifi connection with Wifi Manager...
void setupWiFi(void)
{

  showInitColor(CRGB::Blue);
  delay(INITDELAY);

  WiFi.hostname(LED_NAME + String(ESP.getChipId()));

  WiFiManager wifiManager;

  // 4 Minutes should be sufficient.
  // Especially in case of WiFi loss...
  wifiManager.setConfigPortalTimeout(240);

//tries to connect to last known settings
//if it does not connect it starts an access point with the specified name
//here  "AutoConnectAP" with password "password"
//and goes into a blocking loop awaiting configuration
  DEBUGPRNT("Going to autoconnect and/or Start AP");
  if (!wifiManager.autoConnect(AP_SSID.c_str()))
  {
    DEBUGPRNT("failed to connect, we should reset as see if it connects");
    showInitColor(CRGB::Yellow);
    delay(3000);
    showInitColor(CRGB::Red);
    ESP.restart();
    delay(5000);
  }
//if we get here we have connected to the WiFi
  DEBUGPRNT("local ip: ");
  DEBUGPRNT(WiFi.localIP());

  showInitColor(CRGB::Green);
  delay(INITDELAY);
  showInitColor(CRGB::Black);
}

// helper function to change an 8bit value by the given percentage
// TODO: we could use 8bit fractions for performance
uint8_t changebypercentage(uint8_t value, uint8_t percentage)
{
  uint16_t ret = max((value * percentage) / 100, 10);
  if (ret > 255)
    ret = 255;
  return (uint8_t)ret;
}

// if /set was called
void handleSet(void)
{
  bool sendStatus = false;

// Debug only
#ifdef DEBUG
  DEBUGPRNT("<Begin>Server Args:");
  for (uint8_t i = 0; i < server.args(); i++)
  {
    DEBUGPRNT(server.argName(i) + "\t" + server.arg(i));
  }
  DEBUGPRNT("<End> Server Args");
#endif
  // to be completed in general
  // TODO: question: is there enough memory to store color and "timing" per pixel?
  // i.e. uint32_t onColor, OffColor, uint16_t ontime, offtime
  // = 12 * 300 = 3600 byte...???
  // this will be a new branch possibly....

  // The following list is unfortunately not complete....
  // mo = mode set (eihter +, - or value)
  // br = brightness (eihter +, - or value)
  // co = color (32 bit unsigned color)
  // re = red value of color (eihter +, - or value)
  // gr = green value of color (eihter +, - or value)
  // bl = blue value of color (eihter +, - or value)
  // sp = speed (eihter +, - or value)
  // sec = sunrise / sunset time in seconds....
  // min = sunrise/ sunset time in minutes
  // pi = pixel to be set (clears others?)
  // rnS = Range start Pixel;
  // rnE = Range end Pixel;

  // here we set a new mode if we have the argument mode
  if (server.hasArg("mo"))
  {
    // flag to decide if this is an library effect
    bool isWS2812FX = false;
    // current library effect number
    uint8_t effect = strip->getMode();

#ifdef DEBUG
    DEBUGPRNT("got Argument mo....");
#endif

    // just switch to the next if we get an "u" for up
    if (server.arg("mo")[0] == 'u')
    {

#ifdef DEBUG
      DEBUGPRNT("got Argument mode up....");
#endif

      //effect = effect + 1;
      isWS2812FX = true;
      effect = strip->nextMode(AUTO_MODE_UP);
    }
    // switch to the previous one if we get a "d" for down
    else if (server.arg("mo")[0] == 'd')
    {
#ifdef DEBUG
      DEBUGPRNT("got Argument mode down....");
#endif
      //effect = effect - 1;
      isWS2812FX = true;
      effect = strip->nextMode(AUTO_MODE_DOWN);
    }
    // if we get an "o" for off, we switch off
    else if (server.arg("mo")[0] == 'o')
    {
#ifdef DEBUG
      DEBUGPRNT("got Argument mode Off....");
#endif
      strip->setPower(false);
      sendString("state", "off");
      broadcastInt("power", false);
    }
    // for backward compatibility and FHEM:
    // --> activate fire flicker
    else if (server.arg("mo")[0] == 'f')
    {
#ifdef DEBUG
      DEBUGPRNT("got Argument fire....");
#endif
      effect = FX_MODE_FIRE_FLICKER;
      isWS2812FX = true;
    }
    // for backward compatibility and FHEM:
    // --> activate rainbow effect
    else if (server.arg("mo")[0] == 'r')
    {
#ifdef DEBUG
      DEBUGPRNT("got Argument mode rainbow cycle....");
#endif
      effect = FX_MODE_RAINBOW_CYCLE;
      isWS2812FX = true;
    }
    // for backward compatibility and FHEM:
    // --> activate the K.I.T.T. (larson scanner)
    else if (server.arg("mo")[0] == 'k')
    {
#ifdef DEBUG
      DEBUGPRNT("got Argument mode KITT....");
#endif
      effect = FX_MODE_LARSON_SCANNER;
      isWS2812FX = true;
    }
    // for backward compatibility and FHEM:
    // --> activate Twinkle Fox
    else if (server.arg("mo")[0] == 's')
    {
#ifdef DEBUG
      DEBUGPRNT("got Argument mode Twinkle Fox....");
#endif
      effect = FX_MODE_TWINKLE_FOX;
      isWS2812FX = true;
    }
    // for backward compatibility and FHEM:
    // --> activate Twinkle Fox in white...
    else if (server.arg("mo")[0] == 'w')
    {
#ifdef DEBUG
      DEBUGPRNT("got Argument mode White Twinkle....");
#endif
      strip->setColor(CRGBPalette16(CRGB::White));
      effect = FX_MODE_TWINKLE_FOX;
      isWS2812FX = true;
    }
    // sunrise effect
    else if (server.arg("mo") == "Sunrise")
    {
#ifdef DEBUG
      DEBUGPRNT("got Argument mode sunrise....");
#endif

      // milliseconds time to full sunrise
      //      uint32_t mytime = myEEPROMSaveData.sParam.deltaTime * myEEPROMSaveData.sParam.steps;
      //      const uint16_t mysteps = 512; // defaults to 512 as color values are 255...
      // sunrise time in seconds
      if (server.hasArg("sec"))
      {
#ifdef DEBUG
        DEBUGPRNT("got Argument sec....");
#endif
        //        mytime = 1000 * (uint32_t)strtoul(&server.arg("sec")[0], NULL, 10);
        strip->setSunriseTime(((uint16_t)strtoul(&server.arg("sec")[0], NULL, 10)) / 60);
      }
      // sunrise time in minutes
      else if (server.hasArg("min"))
      {
#ifdef DEBUG
        DEBUGPRNT("got Argument min....");
#endif
        //        mytime = (1000 * 60) * (uint8_t)strtoul(&server.arg("min")[0], NULL, 10);
         strip->setSunriseTime(((uint16_t)strtoul(&server.arg("sec")[0], NULL, 10)));
      }
      /*
      // use default if time less than 1000 ms;
      // because smaller values should not be possible.
      if (mytime < 1000)
      {
        // default will be 10 minutes
        // = (1000 ms * 60) = 1 minute *10 = 10 minutes
        mytime = 1000 * 60 * 10; // for readability
      }
      mySunriseStart(mytime, mysteps, true);
      // answer for the "calling" party
      */
      isWS2812FX = true;
      effect = FX_MODE_SUNRISE;
      strip->setTransition();
      broadcastInt("sunriseset", strip->getSunriseTime());
      sendStatus = true;
    }
    // the same for sunset....
    else if (server.arg("mo") == "Sunset")
    {
#ifdef DEBUG
      DEBUGPRNT("got Argument mode sunset....");
#endif
      // milliseconds time to full sunrise
      //      uint32_t mytime = myEEPROMSaveData.sParam.deltaTime * myEEPROMSaveData.sParam.steps;
      //      const uint16_t mysteps = 512; // defaults to 1000;
      // sunrise time in seconds
      if (server.hasArg("sec"))
      {
#ifdef DEBUG
        DEBUGPRNT("got Argument sec....");
#endif
        //        mytime = 1000 * (uint32_t)strtoul(&server.arg("sec")[0], NULL, 10);
        strip->setSunriseTime(((uint16_t)strtoul(&server.arg("sec")[0], NULL, 10)) / 60);
      }
      // sunrise time in minutes
      else if (server.hasArg("min"))
      {
#ifdef DEBUG
        DEBUGPRNT("got Argument min....");
#endif
        //        mytime = (1000 * 60) * (uint8_t)strtoul(&server.arg("min")[0], NULL, 10);
        strip->setSunriseTime( ((uint16_t)strtoul(&server.arg("min")[0], NULL, 10)));
      }

      // answer for the "calling" party
      isWS2812FX = true;
      effect = FX_MODE_SUNSET;
      strip->setTransition();
      broadcastInt("sunriseset", strip->getSunriseTime());
      sendStatus = true;
    }
    // finally - if nothing matched before - we switch to the effect  being provided.
    // we don't care if its actually an int or not
    // because it will be zero anyway if not.
    else
    {
#ifdef DEBUG
      DEBUGPRNT("got Argument mode and seems to be an Effect....");
#endif
      effect = (uint8_t)strtoul(&server.arg("mo")[0], NULL, 10);
      isWS2812FX = true;
    }
    // make sure we roll over at the max number
    if (effect >= strip->getModeCount())
    {
#ifdef DEBUG
      DEBUGPRNT("Effect to high....");
#endif
      effect = 0;
    }
    // activate the effect...
    if (isWS2812FX)
    {
      strip->setMode(effect);
      // in case it was stopped before
      // seems to be obsolete but does not hurt anyway...
      strip->start();

#ifdef DEBUG
      DEBUGPRNT("gonna send mo response....");
#endif
      broadcastInt("power", true);
    }
  }
  // global on/off
  if (server.hasArg("power"))
  {
#ifdef DEBUG
    DEBUGPRNT("got Argument power....");
#endif
    if (server.arg("power")[0] == '0')
    {
      strip->setPower(false);
      strip->setIsRunning(false);
    }
    else
    {
      strip->start();
      strip->setMode(strip->getMode());
    }
      

    sendString("state", strip->getPower() ? "on" : "off");
    broadcastInt("power", strip->getPower());
  }

  // if we got a palette change
  if (server.hasArg("pa"))
  {
    // TODO: Possibility to setColors and new Palettes...
    uint8_t pal = (uint8_t)strtoul(&server.arg("pa")[0], NULL, 10);
    DEBUGPRNT("New palette with value: " + String(pal));
    strip->setTargetPalette(pal);
    //  sendAnswer(   "\"palette\": " + String(pal) + ", \"palette name\": \"" +
    //                (String)strip->getPalName(pal) + "\"");
    //  broadcastInt("pa", pal);
  }

  // if we got a new brightness value
  if (server.hasArg("br"))
  {
    DEBUGPRNT("got Argument brightness....");
    uint8_t brightness = strip->getBrightness();
    if (server.arg("br")[0] == 'u')
    {
      brightness = changebypercentage(brightness, 110);
    }
    else if (server.arg("br")[0] == 'd')
    {
      brightness = changebypercentage(brightness, 90);
    }
    else
    {
      brightness = constrain((uint8_t)strtoul(&server.arg("br")[0], NULL, 10), BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    }
    strip->setBrightness(brightness);
    sendInt("brightness", brightness);
    broadcastInt("br", strip->getBrightness());
  }

  // if we got a speed value
  // for backward compatibility.
  // is beat88 value anyway
  if (server.hasArg("sp"))
  {
#ifdef DEBUG
    DEBUGPRNT("got Argument speed....");
#endif
    uint16_t speed = strip->getBeat88();
    if (server.arg("sp")[0] == 'u')
    {
      uint16_t ret = max((speed * 115) / 100, 10);
      if (ret > BEAT88_MAX)
        ret = BEAT88_MAX;
      speed = ret;
    }
    else if (server.arg("sp")[0] == 'd')
    {
      uint16_t ret = max((speed * 80) / 100, 10);
      if (ret > BEAT88_MAX)
        ret = BEAT88_MAX;
      speed = ret;
    }
    else
    {
      speed = constrain((uint16_t)strtoul(&server.arg("sp")[0], NULL, 10), BEAT88_MIN, BEAT88_MAX);
    }
    strip->setSpeed(speed);
    strip->show();
    sendAnswer("\"speed\": " + String(speed) + ", \"beat88\": \"" + String(speed));
    broadcastInt("sp", strip->getBeat88());
    strip->setTransition();
  }

  // if we got a speed value (as beat88)
  if (server.hasArg("be"))
  {
#ifdef DEBUG
    DEBUGPRNT("got Argument speed (beat)....");
#endif
    uint16_t speed = strip->getBeat88();
    if (server.arg("be")[0] == 'u')
    {
      uint16_t ret = max((speed * 115) / 100, 10);
      if (ret > BEAT88_MAX)
        ret = BEAT88_MAX;
      speed = ret;
    }
    else if (server.arg("be")[0] == 'd')
    {
      uint16_t ret = max((speed * 80) / 100, 10);
      if (ret > BEAT88_MAX)
        ret = BEAT88_MAX;
      speed = ret;
    }
    else
    {
      speed = constrain((uint16_t)strtoul(&server.arg("be")[0], NULL, 10), BEAT88_MIN, BEAT88_MAX);
    }
    strip->setSpeed(speed);
    strip->show();
    sendAnswer("\"speed\": " + String(speed) + ", \"beat88\": \"" + String(speed));
    broadcastInt("sp", strip->getBeat88());
    strip->setTransition();
  }

  // color handling
  // this is a bit tricky, as it handles either RGB as one or different values.

  // current color (first value from palette)
  uint32_t color = strip->getColor(0);
  bool setColor = false;
  // we got red
  if (server.hasArg("re"))
  {
    setColor = true;
#ifdef DEBUG
    DEBUGPRNT("got Argument red....");
#endif
    uint8_t re = Red(color);
    if (server.arg("re")[0] == 'u')
    {
      re = changebypercentage(re, 110);
    }
    else if (server.arg("re")[0] == 'd')
    {
      re = changebypercentage(re, 90);
    }
    else
    {
      re = constrain((uint8_t)strtoul(&server.arg("re")[0], NULL, 10), 0, 255);
    }
    color = (color & 0x00ffff) | (re << 16);
  }
  // we got green
  if (server.hasArg("gr"))
  {
    setColor = true;
#ifdef DEBUG
    DEBUGPRNT("got Argument green....");
#endif
    uint8_t gr = Green(color);
    if (server.arg("gr")[0] == 'u')
    {
      gr = changebypercentage(gr, 110);
    }
    else if (server.arg("gr")[0] == 'd')
    {
      gr = changebypercentage(gr, 90);
    }
    else
    {
      gr = constrain((uint8_t)strtoul(&server.arg("gr")[0], NULL, 10), 0, 255);
    }
    color = (color & 0xff00ff) | (gr << 8);
  }
  // we got blue
  if (server.hasArg("bl"))
  {
    setColor = true;
#ifdef DEBUG
    DEBUGPRNT("got Argument blue....");
#endif
    uint8_t bl = Blue(color);
    if (server.arg("bl")[0] == 'u')
    {
      bl = changebypercentage(bl, 110);
    }
    else if (server.arg("bl")[0] == 'd')
    {
      bl = changebypercentage(bl, 90);
    }
    else
    {
      bl = constrain((uint8_t)strtoul(&server.arg("bl")[0], NULL, 10), 0, 255);
    }
    color = (color & 0xffff00) | (bl << 0);
  }
  // we got a 32bit color value (24 actually)
  if (server.hasArg("co"))
  {
    setColor = true;
#ifdef DEBUG
    DEBUGPRNT("got Argument color....");
#endif
    color = constrain((uint32_t)strtoul(&server.arg("co")[0], NULL, 16), 0, 0xffffff);
  }
  // we got one solid color value as r, g, b
  if (server.hasArg("solidColor"))
  {
    setColor = true;
#ifdef DEBUG
    DEBUGPRNT("got Argument solidColor....");
#endif
    uint8_t r, g, b;
    r = constrain((uint8_t)strtoul(&server.arg("r")[0], NULL, 10), 0, 255);
    g = constrain((uint8_t)strtoul(&server.arg("g")[0], NULL, 10), 0, 255);
    b = constrain((uint8_t)strtoul(&server.arg("b")[0], NULL, 10), 0, 255);
    color = (r << 16) | (g << 8) | (b << 0);
    // CRGB solidColor(color); // obsolete?

    broadcastInt("pa", strip->getPalCount()); // this reflects a "custom palette"
  }
  // a signle pixel...
  //FIXME: Does not yet work. Lets simplyfy all of this!
  if (server.hasArg("pi"))
  {
#ifdef DEBUG
    DEBUGPRNT("got Argument pixel....");
#endif
    //setEffect(FX_NO_FX);
    uint16_t pixel = constrain((uint16_t)strtoul(&server.arg("pi")[0], NULL, 10), 0, strip->getStripLength() - 1);
    //strip_setpixelcolor(pixel, color);

    strip->setMode(FX_MODE_VOID);
    strip->leds[pixel] = CRGB(color);
    sendStatus = true;
    // a range of pixels from start rnS to end rnE
  }
  //FIXME: Does not yet work. Lets simplyfy all of this!
  else if (server.hasArg("rnS") && server.hasArg("rnE"))
  {
#ifdef DEBUG
    DEBUGPRNT("got Argument range start / range end....");
#endif
    uint16_t start = constrain((uint16_t)strtoul(&server.arg("rnS")[0], NULL, 10), 0, strip->getStripLength());
    uint16_t end = constrain((uint16_t)strtoul(&server.arg("rnE")[0], NULL, 10), start, strip->getStripLength());

    strip->setMode(FX_MODE_VOID);
    for (uint16_t i = start; i <= end; i++)
    {
      strip->leds[i] = CRGB(color);
    }
    //set_Range(start, end, color);
    sendStatus = true;
    // one color for the complete strip
  }
  else if (server.hasArg("rgb"))
  {
#ifdef DEBUG
    DEBUGPRNT("got Argument rgb....");
#endif
    strip->setColor(color);
    strip->setMode(FX_MODE_STATIC);
    sendStatus = true;
    // finally set a new color
  }
  else
  {
    if (setColor)
    {
      strip->setColor(color);
      sendStatus = true;
    }
  }

  // autoplay flag changes
  if (server.hasArg("autoplay"))
  {
    uint16_t value = String(server.arg("autoplay")).toInt();
    strip->setAutoplay((AUTOPLAYMODES)value);
    sendInt("Autoplay Mode", value);
    broadcastInt("autoplay", value);
  }

  // autoplay duration changes
  if (server.hasArg("autoplayDuration"))
  {
    uint16_t value = String(server.arg("autoplayDuration")).toInt();
    strip->setAutoplayDuration(value);
    sendInt("Autoplay Mode Interval", value);
    broadcastInt("autoplayDuration", value);
  }

  // auto plaette change
  if (server.hasArg("autopal"))
  {
    uint16_t value = String(server.arg("autopal")).toInt();
    strip->setAutopal((AUTOPLAYMODES)value);
    sendInt("Autoplay Palette", value);
    broadcastInt("autopal", value);
  }

  // auto palette change duration changes
  if (server.hasArg("autopalDuration"))
  {
    uint16_t value = String(server.arg("autopalDuration")).toInt();
    strip->setAutopalDuration(value);
    sendInt("Autoplay Palette Interval", value);
    broadcastInt("autopalDuration", value);
  }

  // time for cycling through the basehue value changes
  if (server.hasArg("huetime"))
  {
    uint16_t value = String(server.arg("huetime")).toInt();
    sendInt("Hue change time", value);
    broadcastInt("huetime", value);
    strip->setHuetime(value);
  }

#pragma message "We could implement a value to change how a palette is distributed accross the strip"

  // the hue offset for a given effect (if - e.g. not spread across the whole strip)
  if (server.hasArg("deltahue"))
  {
    uint16_t value = constrain(String(server.arg("deltahue")).toInt(), 0, 255);
    sendInt("Delta hue per change", value);
    broadcastInt("deltahue", value);
    strip->setDeltaHue(value);
    strip->setTransition();
  }

  // parameter for teh "fire" - flame cooldown
  if (server.hasArg("cooling"))
  {
    uint16_t value = String(server.arg("cooling")).toInt();
    sendInt("Fire Cooling", value);
    broadcastInt("cooling", value);
    strip->setCooling(value);
    strip->setTransition();
  }

  // parameter for the sparking - new flames
  if (server.hasArg("sparking"))
  {
    uint16_t value = String(server.arg("sparking")).toInt();
    sendInt("Fire sparking", value);
    broadcastInt("sparking", value);
    strip->setSparking(value);
    strip->setTransition();
  }

  // parameter for twinkle fox (speed)
  if (server.hasArg("twinkleSpeed"))
  {
    uint16_t value = String(server.arg("twinkleSpeed")).toInt();
    sendInt("Twinkle Speed", value);
    broadcastInt("twinkleSpeed", value);
    strip->setTwinkleSpeed(value);
    strip->setTransition();
  }

  // parameter for twinkle fox (density)
  if (server.hasArg("twinkleDensity"))
  {
    uint16_t value = String(server.arg("twinkleDensity")).toInt();
    sendInt("Twinkle Density", value);
    broadcastInt("twinkleDensity", value);
    strip->setTwinkleDensity(value);
    strip->setTransition();
  }

  // parameter for number of bars (beat sine glows etc...)
  if (server.hasArg("numBars"))
  {
    uint16_t value = String(server.arg("numBars")).toInt();
    if (value >= (LED_COUNT / strip->getSegments() / 10))
      value = max((LED_COUNT / strip->getSegments() / 10), 2);
    sendInt("Number of Bars", value);
    broadcastInt("numBars", value);
    strip->setNumBars(value);
    strip->setTransition();
  }

  // parameter to change the palette blend type for cetain effects
  if (server.hasArg("blendType"))
  {
    uint16_t value = String(server.arg("blendType")).toInt();

    broadcastInt("blendType", value);
    if (value)
    {
      strip->setBlendType(LINEARBLEND);
      sendString("BlendType", "LINEARBLEND");
    }
    else
    {
      strip->setBlendType(NOBLEND);
      sendString("BlendType", "NOBLEND");
    }
    strip->setTransition();
  }

  // parameter to change the Color Temperature of the Strip
  if (server.hasArg("ColorTemperature"))
  {
    uint8_t value = String(server.arg("ColorTemperature")).toInt();

    broadcastInt("ColorTemperature", value);
    sendString("ColorTemperature", strip->getColorTempName(value));
    strip->setColorTemperature(value);
    strip->setTransition();
  }

  // parameter to change direction of certain effects..
  if (server.hasArg("reverse"))
  {
    uint16_t value = String(server.arg("reverse")).toInt();
    sendInt("reverse", value);
    broadcastInt("reverse", value);
    strip->getSegment()->reverse = value;
    strip->setTransition();
  }

  // parameter to invert colors of all effects..
  if (server.hasArg("inverse"))
  {
    uint16_t value = String(server.arg("inverse")).toInt();
    sendInt("inverse", value);
    broadcastInt("inverse", value);
    strip->setInverse(value);
    strip->setTransition();
  }

  // parameter to divide LEDS into two equal halfs...
  if (server.hasArg("mirror"))
  {
    uint16_t value = String(server.arg("mirror")).toInt();
    sendInt("mirror", value);
    broadcastInt("mirror", value);
    strip->setMirror(value);
    strip->setTransition();
  }

  // parameter so set the max current the leds will draw
  if (server.hasArg("current"))
  {
    uint16_t value = String(server.arg("current")).toInt();
    sendInt("Lamp Max Current", value);
    broadcastInt("current", value);
    strip->setMilliamps(value);
  }

  // parameter for the blur against the previous LED values
  if (server.hasArg("LEDblur"))
  {
    uint8_t value = String(server.arg("LEDblur")).toInt();
    sendInt("LEDblur", value);
    broadcastInt("LEDblur", value);
    strip->setBlur(value);
    strip->setTransition();
  }

  // parameter for the frames per second (FPS)
  if (server.hasArg("fps"))
  {
    uint8_t value = String(server.arg("fps")).toInt();
    sendInt("fps", value);
    broadcastInt("fps", value);
    strip->setMaxFPS(value);
    strip->setTransition();
  }

  if (server.hasArg("dithering"))
  {
    uint8_t value = String(server.arg("dithering")).toInt();
    sendInt("dithering", value);
    broadcastInt("dithering", value);
    strip->setDithering(value);
  }

  if (server.hasArg("sunriseset"))
  {
    uint8_t value = String(server.arg("sunriseset")).toInt();
    sendInt("sunriseset", value);
    broadcastInt("sunriseset", value);
    strip->getSegment()->sunrisetime = value;
  }

  // reset to default values
  if (server.hasArg("resetdefaults"))
  {
    uint8_t value = String(server.arg("resetdefaults")).toInt();
    if (value)
      strip->resetDefaults();
    sendInt("resetdefaults", 0);
    broadcastInt("resetdefaults", 0);
    sendStatus = true;
    strip->setTransition();
  }

  if (server.hasArg("damping"))
  {
    uint8_t value = constrain(String(server.arg("damping")).toInt(), 0, 100);
    sendInt("damping", value);
    broadcastInt("damping", value);
    strip->getSegment()->damping = value;
  }

#ifdef DEBUG
  // Testing different Resets
  // can then be triggered via web interface (at the very bottom)
  if (server.hasArg("resets"))
  {
    uint8_t value = String(server.arg("resets")).toInt();
    switch (value)
    {
    case 0:
      break;
    case 1: //
      ESP.reset();
      break;
    case 2:
      ESP.restart();
      break;
    case 3:
      ESP.wdtDisable();
      break;
    case 4:
      while (1)
      {
      }
      break;
    case 5:
      volatile uint8_t a = 0;
      volatile uint8_t b = 5;
      volatile uint8_t c = b / a;
      break;
    }
  }
#endif

  // parameter for number of segemts
  if (server.hasArg("segments"))
  {
    uint16_t value = String(server.arg("segments")).toInt();
    sendInt("segments", value);
    broadcastInt("segments", value);
    strip->getSegment()->segments = constrain(value, 1, LED_COUNT / 10);
    strip->setTransition();
  }

  // new parameters, it's time to save
  shouldSaveRuntime = true;
  if (sendStatus)
  {
    handleStatus();
  }
  /// strip->setTransition();  <-- this is not wise as it removes the smooth fading for colors. So we need to set it case by case
}

// if something unknown was called...
void handleNotFound(void)
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handleGetModes(void)
{
  const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(56) + 1070;
  DynamicJsonBuffer jsonBuffer(bufferSize);

  JsonObject &root = jsonBuffer.createObject();

  JsonObject &modeinfo = root.createNestedObject("modeinfo");
  modeinfo["count"] = strip->getModeCount();

  JsonObject &modeinfo_modes = modeinfo.createNestedObject("modes");
  for (uint8_t i = 0; i < strip->getModeCount(); i++)
  {
    modeinfo_modes[strip->getModeName(i)] = i;
  }

#ifdef DEBUG
  root.printTo(Serial);
#endif

  String message = "";
  root.prettyPrintTo(message);
  server.send(200, "application/json", message);
}

void handleGetPals(void)
{
  const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(56) + 1070;
  DynamicJsonBuffer jsonBuffer(bufferSize);

  JsonObject &root = jsonBuffer.createObject();

  JsonObject &modeinfo = root.createNestedObject("palinfo");
  modeinfo["count"] = strip->getPalCount();

  JsonObject &modeinfo_modes = modeinfo.createNestedObject("pals");
  for (uint8_t i = 0; i < strip->getPalCount(); i++)
  {
    modeinfo_modes[strip->getPalName(i)] = i;
  }

#ifdef DEBUG
  root.printTo(Serial);
#endif

  String message = "";
  root.prettyPrintTo(message);
  server.send(200, "application/json", message);
}

void handleStatus(void)
{
  uint32_t answer_time = micros();

  String message;
  message.reserve(1500);
  uint16_t num_leds_on = 0;
  // if brightness = 0, no LED can be lid.
  if (strip->getBrightness())
  {
    // count the number of active LEDs
    // in rare occassions, this can still be 0, depending on the effect.
    for (uint16_t i = 0; i < strip->getStripLength(); i++)
    {
      if (strip->leds[i])
        num_leds_on++;
    }
  }

  message += F("{\n  \"currentState\": {\n    \"state\": ");
  if (strip->getPower())
  {
    message += F("\"on\"");
  }
  else
  {
    message += F("\"off\"");
  }
  message += F(",\n    \"Buildversion\": \"");
  message += build_version; //String(BUILD_VERSION);
  message += F("\",\n    \"Lampenname\": \"");
  message += String(LED_NAME);
  message += F("\",\n    \"Anzahl Leds\": ");
  message += String(strip->getStripLength());
  message += F(",\n    \"Lamp Voltage\": ");
  message += String(strip->getVoltage());
  message += F(",\n    \"Lamp Max Current\": ");
  message += String(strip->getMilliamps());
  message += F(",\n    \"Lamp Max Power (mW)\": ");
  message += String(strip->getVoltage() * strip->getMilliamps());
  message += F(",\n    \"Lamp current Power\": ");
  message += String(strip->getCurrentPower());
  message += F(",\n    \"Leds an\": ");
  message += String(num_leds_on);
  message += F(",\n    \"mode\": ");
  message += String(FX_WS2812);
  message += F(",\n    \"modename\": ");
  switch (FX_WS2812)
  {
  case FX_NO_FX:
    message += F("\"No FX");
    break;
  case FX_SUNRISE:
    message += F("\"Sunrise Effect");
    break;
  case FX_SUNSET:
    message += F("\"Sunset Effect");
    break;
  case FX_WS2812:
    message += F("\"WS2812fx ");
    message += String(strip->getModeName(strip->getMode()));
    break;
  default:
    message += F("\"UNKNOWN");
    break;
  }
  message += F("\", \n    \"wsfxmode\": ");
  message += String(strip->getMode());
  message += F(", \n    \"beat88\": ");
  message += String(strip->getBeat88());
  message += F(", \n    \"speed\": ");
  message += String(strip->getBeat88());
  message += F(", \n    \"brightness\": ");
  message += String(strip->getBrightness());

  // Palettes and Colors
  message += F(", \n    \"palette count\": ");
  message += String(strip->getPalCount());
  message += F(", \n    \"palette\": ");
  message += String(strip->getTargetPaletteNumber());
  message += F(", \n    \"palette name\": \"");
  message += String(strip->getTargetPaletteName());
  message += F("\"");

  CRGB col = CRGB::Black;
  // We return either black (strip effectively off)
  // or the color of the first pixel....
  for (uint16_t i = 0; i < strip->getStripLength(); i++)
  {
    if (strip->leds[i])
    {
      col = strip->leds[i];
      break;
    }
  }
  message += F(", \n    \"rgb\": ");
  message += String(((col.r << 16) |
                     (col.g << 8) |
                     (col.b << 0)) &
                    0xffffff);
  message += F(", \n    \"color red\": ");
  message += String(col.red);
  message += F(", \n    \"color green\": ");
  message += String(col.green);
  message += F(", \n    \"color blue\": ");
  message += String(col.blue);

  message += F(", \n    \"BlendType\": ");
  if (strip->getSegment()->blendType == NOBLEND)
  {
    message += "\"No Blend\"";
  }
  else if (strip->getSegment()->blendType == LINEARBLEND)
  {
    message += "\"Linear Blend\"";
  }
  else
  {
    message += "\"Unknown Blend\"";
  }

  message += F(", \n    \"Reverse\": ");
  message += getReverse();

  message += F(", \n    \"Hue change time\": ");
  message += getHueTime();
  message += F(", \n    \"Delta hue per change\": ");
  message += getDeltaHue();

  message += F(", \n    \"Autoplay Mode\": ");
  message += getAutoplay();
  message += F(", \n    \"Autoplay Mode Interval\": ");
  message += getAutoplayDuration();

  message += F(", \n    \"Autoplay Palette\": ");
  message += getAutopal();

  message += F(", \n    \"Autoplay Palette Interval\": ");
  message += getAutopalDuration();

  message += F(", \n    \"Fire Cooling\": ");
  message += getCooling();

  message += F(", \n    \"Fire sparking\": ");
  message += getSparking();

  message += F(", \n    \"Twinkle Speed\": ");
  message += getTwinkleSpeed();

  message += F(", \n    \"Twinkle Density\": ");
  message += getTwinkleDensity();

  message += F("\n  },\n  \"sunRiseState\": {\n    \"sunRiseMode\": ");

  if (strip->getMode() == FX_MODE_SUNRISE)
  {
    message += F("\"Sunrise\"");
  }
  else if (strip->getMode() == FX_MODE_SUNSET)
  {
    message += F("\"Sunset\"");
  }
  else
  {
    message += F("\"None\"");
  }
  message += F(",\n    \"sunRiseActive\": ");
  if (strip->getMode() == FX_MODE_SUNRISE || strip->getMode() == FX_MODE_SUNSET)
  {
    message += F("\"on\"");
    message += F(", \n    \"sunRiseCurrStep\": ");
    message += F("..Repair needed..");
    message += F(", \n    \"sunRiseTotalSteps\": ");
    message += F("..Repair needed..");
    message += F(", \n    \"sunRiseTimeToFinish\": ");
    message += F("..Repair needed..");
  }
  else
  {
    message += F("\"off\", \n    \"sunRiseCurrStep\": 0, \n    \"sunRiseTotalSteps\": 0, \n    \"sunRiseTimeToFinish\": 0");
  }

  message += F("\n  }");

#ifdef DEBUG
  message += F(",\n  \"ESP_Data\": {\n    \"DBG_Debug code\": \"On\",\n    \"DBG_CPU_Freq\": ");
  message += String(ESP.getCpuFreqMHz());
  message += F(",\n    \"DBG_Flash Real Size\": ");
  message += String(ESP.getFlashChipRealSize());
  message += F(",\n    \"DBG_Free RAM\": ");
  message += String(ESP.getFreeHeap());
  message += F(",\n    \"DBG_Free Sketch Space\": ");
  message += String(ESP.getFreeSketchSpace());
  message += F(",\n    \"DBG_Sketch Size\": ");
  message += String(ESP.getSketchSize());
  message += F("\n  }");

  message += F(",\n  \"Server_Args\": {");
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += F("\n    \"");
    message += server.argName(i);
    message += F("\": \"");
    message += server.arg(i);
    if (i < server.args() - 1)
      message += F("\",");
    else
      message += F("\"");
  }
  message += F("\n  }");
#endif
  message += F(",\n  \"Stats\": {\n    \"Answer_Time\": ");
  answer_time = micros() - answer_time;
  message += answer_time;
  message += F(",\n    \"FPS\": ");
  message += FastLED.getFPS();
  message += F("\n  }");
  message += F("\n}");

#ifdef DEBUG
  DEBUGPRNT(message);
#endif

  server.send(200, "application/json", message);
}

void factoryReset(void)
{
#ifdef DEBUG
  DEBUGPRNT("Someone requested Factory Reset");
#endif
  // on factory reset, each led will be red
  // increasing from led 0 to max.
  for (uint16_t i = 0; i < strip->getStripLength(); i++)
  {
    strip->leds[i] = 0xa00000;
    strip->show();
    delay(2);
  }
  strip->show();
  delay(INITDELAY);
  /*#ifdef DEBUG
  DEBUGPRNT("Reset WiFi Settings");
  #endif
  wifiManager.resetSettings();
  delay(INITDELAY);
  */
  clearEEPROM();
//reset and try again
#ifdef DEBUG
  DEBUGPRNT("Reset ESP and start all over...");
#endif
  delay(3000);
  ESP.restart();
}

uint32 getResetReason(void)
{
  return ESP.getResetInfoPtr()->reason;
}

/*
 * Clear the CRC to startup Fresh....
 * Used in case we end up in a WDT reset (either SW or HW)
 */
void clearCRC(void)
{
// invalidating the CRC - in case somthing goes terribly wrong...
#ifdef DEBUG
  DEBUGPRNT("Clearing CRC in EEPRom");
#endif
  EEPROM.begin(strip->getCRCsize());
  for (uint i = 0; i < EEPROM.length(); i++)
  {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  EEPROM.end();
#ifdef DEBUG
  DEBUGPRNT("Reset ESP and start all over with default values...");
#endif
  delay(1000);
  ESP.restart();
}

void clearEEPROM(void)
{
//Clearing EEPROM
#ifdef DEBUG
  DEBUGPRNT("Clearing EEPROM");
#endif
  EEPROM.begin(strip->getSegmentSize());
  for (uint i = 0; i < EEPROM.length(); i++)
  {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  EEPROM.end();
}

// Received Factoryreset request.
// To be sure we check the related parameter....
void handleResetRequest(void)
{
  if (server.arg("rst") == "FactoryReset")
  {
    server.send(200, "text/plain", "Will now Reset to factory settings. You need to connect to the WLAN AP afterwards....");
    factoryReset();
  }
  else if (server.arg("rst") == "Defaults")
  {

    strip->setTargetPalette(0);
    strip->setMode(0);
    strip->stop();
    strip->resetDefaults();
    server.send(200, "text/plain", "Strip was reset to the default values...");
    shouldSaveRuntime = true;
  }
}

void setupWebServer(void)
{
  showInitColor(CRGB::Blue);
  delay(INITDELAY);
  SPIFFS.begin();
  {
#ifdef DEBUG
    Dir dir = SPIFFS.openDir("/");
    while (dir.next())
    {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      DEBUGPRNT("FS_FILE:");
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), String(fileSize).c_str());
    }

    Serial.printf("\n");
#endif
  }

  server.on("/all", HTTP_GET, []() {
#ifdef DEBUG
    DEBUGPRNT("Called /all!");
#endif
    String json = getFieldsJson(fields, fieldCount);
    server.send(200, "text/json", json);
  });

  server.on("/fieldValue", HTTP_GET, []() {
    String name = server.arg("name");
#ifdef DEBUG
    DEBUGPRNT("Called /fieldValue with arg name =");
    DEBUGPRNT(name);
#endif

    String value = getFieldValue(name, fields, fieldCount);
    server.send(200, "text/json", value);
  });

  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm"))
      server.send(404, "text/plain", "FileNotFound");
  });

  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  },
            handleFileUpload);

  server.on("/set", handleSet);
  server.on("/getmodes", handleGetModes);
  server.on("/getpals", handleGetPals);
  server.on("/status", handleStatus);
  server.on("/reset", handleResetRequest);
  server.onNotFound(handleNotFound);

  server.serveStatic("/", SPIFFS, "/", "max-age=86400");
  delay(INITDELAY);
  server.begin();

  showInitColor(CRGB::Yellow);
  delay(INITDELAY);
#ifdef DEBUG
  DEBUGPRNT("HTTP server started.\n");
#endif
  webSocketsServer = new WebSocketsServer(81);
  webSocketsServer->begin();
  webSocketsServer->onEvent(webSocketEvent);

  showInitColor(CRGB::Green);
  delay(INITDELAY);
#ifdef DEBUG
  DEBUGPRNT("webSocketServer started.\n");
#endif
  showInitColor(CRGB::Black);
  delay(INITDELAY);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
#ifdef DEBUG
  switch (type)
  {
  case WStype_DISCONNECTED:

    Serial.printf("[%u] Disconnected!\n", num);
    break;

  case WStype_CONNECTED:
  {
    IPAddress ip = webSocketsServer->remoteIP(num);
    Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

    // send message to client
    // webSocketsServer.sendTXT(num, "Connected");
  }
  break;

  case WStype_TEXT:
    Serial.printf("[%u] get Text: %s\n", num, payload);

    // send message to client
    // webSocketsServer.sendTXT(num, "message here");

    // send data to all connected clients
    // webSocketsServer.broadcastTXT("message here");
    break;

  case WStype_BIN:
    Serial.printf("[%u] get binary length: %u\n", num, length);
    hexdump(payload, length);

    break;

  default:
    break;
  }
#endif
}

// setup network and output pins
void setup()
{
  // Sanity delay to get everything settled....
  delay(500);

#ifdef DEBUG
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
#endif
  DEBUGPRNT("\n");
  DEBUGPRNT(F("Booting"));

  DEBUGPRNT("");
  DEBUGPRNT(F("Checking boot cause:"));

  switch (getResetReason())
  {
  case REASON_DEFAULT_RST:
    DEBUGPRNT(F("\tREASON_DEFAULT_RST: Normal boot"));
    break;
  case REASON_WDT_RST:
    DEBUGPRNT(F("\tREASON_WDT_RST"));
    clearCRC(); // should enable default start in case of
    break;
  case REASON_EXCEPTION_RST:
    DEBUGPRNT(F("\tREASON_EXCEPTION_RST"));
    clearCRC();
    break;
  case REASON_SOFT_WDT_RST:
    DEBUGPRNT(F("\tREASON_SOFT_WDT_RST"));
    clearCRC();
    break;
  case REASON_SOFT_RESTART:
    DEBUGPRNT(F("\tREASON_SOFT_RESTART"));
    break;
  case REASON_DEEP_SLEEP_AWAKE:
    DEBUGPRNT(F("\tREASON_DEEP_SLEEP_AWAKE"));
    break;
  case REASON_EXT_SYS_RST:
    DEBUGPRNT(F("\n\tREASON_EXT_SYS_RST: External trigger..."));
    break;

  default:
    DEBUGPRNT(F("\tUnknown cause..."));
    break;
  }

  stripe_setup(LED_COUNT,
               STRIP_FPS,
               STRIP_VOLTAGE,
               STRIP_MILLIAMPS,
               RainbowColors_p,
               F("Rainbow Colors"),
               UncorrectedColor); //TypicalLEDStrip);

  EEPROM.begin(strip->getSegmentSize());

  // callbacks for "untraced changes". Can feed the websocket.
  strip->setTargetPaletteCallback(&targetPaletteChanged);
  strip->setModeCallback(&modeChanged);

  setupWiFi();

  setupWebServer();

  initOverTheAirUpdate();

// if we got that far, we show by a nice little animation
// as setup finished signal....
  for (uint8_t a = 0; a < 1; a++)
  {
    for (uint16_t c = 0; c < 256; c += 3)
    {
      for (uint16_t i = 0; i < strip->getStripLength(); i++)
      {
        strip->leds[i].green = c;
      }
      strip->show();
      delay(1);
    }
    DEBUGPRNT("Init done - fading green out");
    delay(2);
    for (uint8_t c = 255; c > 0; c -= 3)
    {
      for (uint16_t i = 0; i < strip->getStripLength(); i++)
      {
        strip->leds[i].subtractFromRGB(4);
      }
      strip->show();
      delay(1);
    }
  }
  //strip->stop();
  delay(INITDELAY);
#ifdef DEBUG
  DEBUGPRNT("Init finished.. Read runtime data");
#endif
  readRuntimeDataEEPROM();
#ifdef DEBUG
  DEBUGPRNT("Runtime Data loaded");
  FastLED.countFPS();
#endif
  //setEffect(FX_NO_FX);
}

// request receive loop
void loop()
{
  uint32_t now = millis();
  static uint32_t nextEEPROM_save = now + 1000;
  static uint32_t wifi_check_time = now + WIFI_TIMEOUT;

#ifdef DEBUG
  static unsigned long last_status_msg = 0;
#endif
  if (OTAisRunning)
    return;
    // if someone requests a call to the factory reset...
    //static bool ResetRequested = false;

#ifdef DEBUG
  // Debug Watchdog. to be removed for "production".
  if (now - last_status_msg > 10000)
  {
    last_status_msg = now;
    DEBUGPRNT("\n");
    DEBUGPRNT("\tWS2812FX\t" + String(strip->getMode()) + "\t" + strip->getModeName(strip->getMode()));
    DEBUGPRNT("\tC:\t" + strip->getCurrentPaletteName() + "\tT:\t" + strip->getTargetPaletteName() + "\t\tFPS:\t" + String(FastLED.getFPS()));
  }
#endif
  // Checking WiFi state every WIFI_TIMEOUT
  // Reset on disconnection
  if (now > wifi_check_time)
  {
    DEBUGPRNT("Checking WiFi... ");
    if (WiFi.status() != WL_CONNECTED)
    {
#ifdef DEBUG
      DEBUGPRNT("WiFi connection lost. Reconnecting...");
      DEBUGPRNT("Lost Wifi Connection....");
#endif
      // Show the WiFi loss with yellow LEDs.
      // Whole strip lid finally.
      for (uint16_t i = 0; i < strip->getStripLength(); i++)
      {
        strip->leds[i] = 0xa0a000;
        strip->show();
      }
      // Reset after 6 seconds....
      delay(3000);
#ifdef DEBUG
      DEBUGPRNT("Resetting ESP....");
#endif
      delay(3000);
      ESP.restart();
    }
    wifi_check_time = now + WIFI_TIMEOUT;
  }

  ArduinoOTA.handle(); // check and handle OTA updates of the code....

  webSocketsServer->loop();

  server.handleClient();

  strip->service();

  if (shouldSaveRuntime && now > nextEEPROM_save)
  {
    saveEEPROMData();
    nextEEPROM_save = now + 1000;
    shouldSaveRuntime = false;
  }
}
