/**************************************************************
   This work is based on many others.
   If published then under whatever licence free to be used,
   distibuted, changed and whatsoever.
   Work is based on:
   WS2812BFX library by  - see:
   fhem esp8266 implementation by   - see:
   WiFiManager library by - - see:
   ... many others ( see includes)

 **************************************************************/
#include <FS.h>

#include "Arduino.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <Bounce2.h>
#include <ArduinoOTA.h>


// new approach starts here:
#include <led_strip.h>

#include <pahcolor.h>

/* Flash Button can be used here for toggles.... */
bool hasResetButton = false;
Bounce debouncer = Bounce();




/* Definitions for network usage */
/* maybe move all wifi stuff to separate files.... */
#define WIFI_TIMEOUT 5000
WiFiServer server(80);
WiFiManager wifiManager;

String AP_SSID = "LED_stripe_" + String(ESP.getChipId());

char chrResetButtonPin[3]="X";
char chrLEDCount[5] = "0";
char chrLEDPin[2] = "0";

//default custom static IP
char static_ip[16] = "";
char static_gw[16] = "";
char static_sn[16] = "255.255.255.0";

WiFiManagerParameter ResetButtonPin("RstPin", "Reset Pin", chrResetButtonPin, 3);
WiFiManagerParameter LedCountConf("LEDCount","LED Count", chrLEDCount, 4);
WiFiManagerParameter LedPinConf("LEDPIN", "Strip Data Pin", chrLEDPin, 3);

//flag for saving data
bool shouldSaveConfig = false;

/* END Network Definitions */

unsigned long last_wifi_check_time = 0;



//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("\n\tWe are now invited to save the configuration...");
  shouldSaveConfig = true;
}

void readConfigurationFS(void)
{
  //clean FS, for testing

  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(chrResetButtonPin, json["chrResetButtonPin"]);
          strcpy(chrLEDCount, json["chrLEDCount"]);
          strcpy(chrLEDPin, json["chrLEDPin"]);
          if(json["ip"]) {
            Serial.println("setting custom ip from config");
            //static_ip = json["ip"];
            strcpy(static_ip, json["ip"]);
            strcpy(static_gw, json["gateway"]);
            strcpy(static_sn, json["subnet"]);
            //strcat(static_ip, json["ip"]);
            //static_gw = json["gateway"];
            //static_sn = json["subnet"];
            Serial.println(static_ip);
/*            Serial.println("converting ip");
            IPAddress ip = ipFromCharArray(static_ip);
            Serial.println(ip);*/
          } else {
            Serial.println("no custom ip in config");
          }
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
  Serial.print("Static IP: \t");
  Serial.println(static_ip);
  Serial.print("LED Count: \t");
  Serial.println(chrLEDCount);
  Serial.print("LED Pin: \t");
  Serial.println(chrLEDPin);
  Serial.print("Rst Btn Pin: \t");
  Serial.println(chrResetButtonPin);
}

void initOverTheAirUpdate(void)
{
  Serial.println("\nInitializing OTA capabilities....");
  /* init OTA */
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  //ArduinoOTA.setHostname("esp8266Toby01");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("OTA start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA end");
    for(uint8_t i = Green(strip.getPixelColor(0)); i>0; i--)
    {
      for(uint16_t p=0; p<strip.getLength(); p++)
      {
        strip.setPixelColor(p, 0, i-1 ,0);
      }
      strip.show();
      delay(2);
    }
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    uint16_t pixel = (uint16_t)(progress / (total / strip.getLength()));
    strip.setPixelColor(pixel, 0x00ff00);
    strip.show();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    for(uint8_t c = 0; c<256; c++)
    {
      for(uint16_t i = 0; i<strip.getLength(); i++)
      {
        strip.setPixelColor(i,c,0,0);
      }
      strip.show();
      delay(2);
    }
  });
  ArduinoOTA.begin();
  Serial.println("OTA capabilities initialized....");
  delay(500);
}

void setupResetButton(uint8_t buttonPin)
{
    pinMode(buttonPin,INPUT_PULLUP);
    // After setting up the button, setup the Bounce instance :
    debouncer.attach(buttonPin);
    debouncer.interval(10); // interval in ms
}

void updateConfiguration()
{
  Serial.println("Updating configuration just received");
  // only copy the values in case the Parameter wa sset in config!
  if(shouldSaveConfig)
  {
    strcpy(chrResetButtonPin, ResetButtonPin.getValue());
    strcpy(chrLEDCount, LedCountConf.getValue());
    strcpy(chrLEDPin, LedPinConf.getValue());
  }
  String sLedCount = chrLEDCount;
  String sLedPin   = chrLEDPin;

  uint16_t ledCount = sLedCount.toInt();
  uint8_t ledPin = sLedPin.toInt();

  // if something went wrong here (GPIO = 0 or LEDs = 0)
  // we reset and start allover again
  if(ledCount == 0 || ledPin == 0)
  {
    Serial.println("\n\tSomething went wrong! Config will be deleted and ESP Reset!");
    SPIFFS.format();
    wifiManager.resetSettings();
    Serial.println("\nCountdown to Reset:");
    for(uint8_t i = 0; i<10; i++)
    {
      Serial.println(10-i);
    }
    ESP.reset();
  }

  Serial.print("LEDCount: ");
  Serial.println(ledCount);

  Serial.print("LED datapin: ");
  Serial.println(ledPin);

  if(chrResetButtonPin[0] == 'x' || chrResetButtonPin[0] == 'X')
  {
      hasResetButton = false;
      Serial.println("No Reset Button specified.");
  }
  else
  {
      String srstPin = chrResetButtonPin;
      uint8_t rstPin =  srstPin.toInt();
      Serial.print("Reset Button Pin: ");
      Serial.println(rstPin);
      hasResetButton = true;
      setupResetButton(rstPin);
  }

  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["chrResetButtonPin"] = chrResetButtonPin;
    json["chrLEDCount"] = chrLEDCount;
    json["chrLEDPin"] = chrLEDPin;

/*  Maybe we deal with static IP later.
    For now we just don't save it....
    json["ip"] = WiFi.localIP().toString();
    json["gateway"] = WiFi.gatewayIP().toString();
    json["subnet"] = WiFi.subnetMask().toString();
*/
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.prettyPrintTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  Serial.println("\nEverything in place... setting up stripe.");
  stripe_setup(ledCount, ledPin, DEFAULT_PIXEL_TYPE);
}

void setupWiFi(void)
{

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&ResetButtonPin);
  wifiManager.addParameter(&LedCountConf);
  wifiManager.addParameter(&LedPinConf);


  //exit after config instead of connecting
  //wifiManager.setBreakAfterConfig(true);

  //reset settings - for testing
  //wifiManager.resetSettings();

  // 4 Minutes should be sufficient.
  // Especially in case of WiFi loss...
  wifiManager.setConfigPortalTimeout(240);

  //tries to connect to last known settings
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP" with password "password"
  //and goes into a blocking loop awaiting configuration
  Serial.println("Going to autoconnect and/or Start AP");
  if (!wifiManager.autoConnect(AP_SSID.c_str())) {
    Serial.println("failed to connect, we should reset as see if it connects");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  Serial.println("Print LED config again to be sure: ");
  Serial.print("LED Count: \t");
  Serial.println(chrLEDCount);
  Serial.print("LED Pin: \t");
  Serial.println(chrLEDPin);
  Serial.print("Rst Btn Pin: \t");
  Serial.println(chrResetButtonPin);
  //if you get here you have connected to the WiFi
  Serial.print("local ip: ");
  Serial.println(WiFi.localIP());
  delay(1);
  server.begin();
  Serial.println("HTTP server started.\n");
}


// setup network and output pins
void setup()
{
  bool winit = true;
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println(F("Booting"));
    // Setup the button with an internal pull-up :

  readConfigurationFS();

  setupWiFi();

  updateConfiguration();

  initOverTheAirUpdate();

  // if we got that far, we show by a nice littel animation
  for(uint8_t num=0; num<4; num++)
  {
    for(uint16_t i=0; i<strip.getLength(); i++)
    {
      if(i%2)
        strip.setPixelColor(i,0x00a000);
      else
        strip.setPixelColor(i,0xa00000);
    }
    strip.show();
    delay(400);
    for(uint16_t i=0; i<strip.getLength(); i++)
    {
      if(i%2)
        strip.setPixelColor(i,0xa00000);
      else
        strip.setPixelColor(i,0x00a000);
    }
    strip.show();
    delay(300);
  }
}



// request receive loop
void loop()
{
  unsigned long now = millis();
  static uint8_t life_sign = 0;
  static unsigned long last_status_msg = 0;

  // if someone requests a call to the factory reset...
  static bool ResetRequested = false;

  if(now - last_status_msg > 20000)
  {
    last_status_msg = now;
    /*
    Serial.print("\nCurrent State:\n");
    Serial.print("Life Sign Counter\t");
    Serial.print(life_sign++);
    Serial.print("\nstripIsOn\t");
    Serial.print(stripIsOn);
    Serial.print("\nstripWasOff\t");
    Serial.print(stripWasOff);
    Serial.print("\npreviousEffect\t");
    Serial.println(previousEffect);
    */
    Serial.print("\ncurrentEffect\t");
    Serial.println(currentEffect);

  }

  if(now - last_wifi_check_time > WIFI_TIMEOUT) {
    //Serial.print("\nChecking WiFi... ");
    if(WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost. Reconnecting...");
        Serial.println("Lost Wifi Connection....");
        for(uint16_t i = 0; i<strip.getLength(); i++)
        {
          strip.setPixelColor(i, 0xa0a000);
          strip.show();
        }
        delay(3000);
        Serial.println("Resetting ESP....");
        delay(3000);
        ESP.reset();
    } else {
      // Serial.println("OK");
    }
    last_wifi_check_time = now;
  }
  ArduinoOTA.handle(); // check and handle OTA updates of the code....

  //Button Handling
  if(hasResetButton)
  {
    debouncer.update();
    if(debouncer.read() == LOW)
    {
      ResetRequested = true;
    }
  }

  if(ResetRequested)
  {
      ResetRequested = false;
      Serial.println("Someone requested Factory Reset");
      for(uint16_t i = 0; i<strip.getLength(); i++)
      {
        strip.setPixelColor(i, 0xa00000);
        strip.show();
        delaymicro(10);
      }
      strip.show();
      // formatting File system
      Serial.println("Format File System");
      delay(1000);
      SPIFFS.format();
      delay(1000);
      Serial.println("Reset WiFi Settings");
      wifiManager.resetSettings();
      delay(3000);
      //reset and try again
      Serial.println("Reset ESP and start all over...");
      ESP.reset();
  }

  // listen for incoming clients
  WiFiClient client = server.available();  // Check if a client has connected

  if (client)
  {
    Serial.println(F("new client"));
    String inputLine = "";
   // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    boolean isGet = false;
    boolean isPost = false;
    boolean isPostData = false;
    int postDataLength;
    int ledix = 0;
    int tupel = 0;
    int redLevel = 0;
    int greenLevel = 0;
    int blueLevel = 0;
    uint8_t linecount=0;
    while(client.connected())
    {
      if(client.available())
      {
        inputLine = client.readStringUntil('\n');
        //Serial.printf("Received Line # %u \n", linecount++);
        //Serial.println(inputLine);

        // SET SINGLE PIXEL url should be GET /rgb/n/rrr,ggg,bbb
        if (inputLine.length() > 3 && inputLine.substring(0,9) == F("GET /rgb/")) {
          int slash = inputLine.indexOf('/', 9 );
          ledix = inputLine.substring(9,slash).toInt();
          int urlend = inputLine.indexOf(' ', 9 );
          String getParam = inputLine.substring(slash+1,urlend+1);
          int komma1 = getParam.indexOf(',');
          int komma2 = getParam.indexOf(',',komma1+1);
          redLevel = getParam.substring(0,komma1).toInt();
          greenLevel = getParam.substring(komma1+1,komma2).toInt();
          blueLevel = getParam.substring(komma2+1).toInt();
          strip_setpixelcolor(ledix, redLevel, greenLevel, blueLevel);
          isGet = true;
        }
        // SET DELAY url should be GET /delay/n
        if (inputLine.length() > 3 && inputLine.substring(0,11) == F("GET /delay/")) {
          stripe_setDelayInterval((uint16_t)inputLine.substring(11).toInt());
          isGet = true;
        }
        // SET BRIGHTNESS url should be GET /brightness/n
        if (inputLine.length() > 3 && inputLine.substring(0,16) == F("GET /brightness/")) {
          stripe_setBrightness(inputLine.substring(16).toInt());
          isGet = true;
        }
        // SET PIXEL RANGE url should be GET /range/x,y/rrr,ggg,bbb
        if (inputLine.length() > 3 && inputLine.substring(0,11) == F("GET /range/")) {
          int slash = inputLine.indexOf('/', 11 );
          int komma1 = inputLine.indexOf(',');
          int x = inputLine.substring(11, komma1).toInt();
          int y = inputLine.substring(komma1+1, slash).toInt();
          int urlend = inputLine.indexOf(' ', 11 );
          String getParam = inputLine.substring(slash+1,urlend+1);
          komma1 = getParam.indexOf(',');
          int komma2 = getParam.indexOf(',',komma1+1);
          redLevel = getParam.substring(0,komma1).toInt();
          greenLevel = getParam.substring(komma1+1,komma2).toInt();
          blueLevel = getParam.substring(komma2+1).toInt();
          set_Range((uint16_t)x, (uint16_t) y, (uint8_t) redLevel, (uint8_t) greenLevel, (uint8_t) blueLevel);
          isGet = true;
        }
        // POST PIXEL DATA
        if (inputLine.length() > 3 && inputLine.substring(0,10) == F("POST /leds")) {
          isPost = true;
          //Serial.println("Received POST Data...");
        }
        if (inputLine.length() > 3 && inputLine.substring(0,16) == F("Content-Length: ")) {
          //postDataLength = inputLine.substring(16).toInt();
          //Serial.printf("\t\tGot Postdata with Length %u\n", postDataLength);
          client.readStringUntil('\n');
          inputLine = client.readStringUntil('\n');
          //Serial.print("Inputline with content:\n\t-->Start of Line\n\t\t");
          //Serial.println(inputLine);
          //Serial.println("\t--> End of Line");
          uint8_t r,g,b = 0;
          uint16_t pixel = 0;
          for(uint16_t i=0; i<inputLine.length()-1; i+=3)
          {
            r = colorVal(inputLine[i]);
            g = colorVal(inputLine[i+1]);
            b = colorVal(inputLine[i+2]);
            if(pixel < strip.getLength()) strip.setPixelColor(pixel++,r,g,b);
          }
          strip.show();
          isGet = true;
        }
        // SET ALL PIXELS OFF url should be GET /off
        if (inputLine.length() > 3 && inputLine.substring(0,8) == F("GET /off")) {
          strip_On_Off(false);
          strip.clear();
          isGet = true;
        }

        // GET STATUS url should be GET /status
        if (inputLine.length() > 3 && inputLine.substring(0,11) == F("GET /status")) {
          isGet = true;
        }
        // GET Config url should be GET /config
        if (inputLine.length() > 3 && inputLine.substring(0,14) == F("GET /factreset")) {
          ResetRequested = true;
          isGet = true;
        }

        // SET FIRE EFFECT
        if (inputLine.length() > 3 && inputLine.substring(0,9) == F("GET /fire")) {
          setEffect(FX_FIRE);
          isGet = true;
        }

        // SET Sunrise EFFECT
        if (inputLine.length() > 3 && inputLine.substring(0,13) == F("GET /sunrise/")) {
          int slash = inputLine.indexOf(',', 13 );
          uint32_t mytime = 1000 * (uint32_t)inputLine.substring(13,slash).toInt();
          uint16_t mysteps = 1000;
          isGet = true;
          mySunriseStart(mytime, mysteps, true);
          setEffect(FX_SUNRISE);
          isGet = true;
          //Serial.printf("\n\tGoing to start sunrise with a duration of %u seconds.\n", mytime/1000);

          // temporary... needs different approach.

        }
        // SET Sunset EFFECT
        if (inputLine.length() > 3 && inputLine.substring(0,12) == F("GET /sunset/")) {
          int slash = inputLine.indexOf(',', 12 );
          uint32_t mytime = 1000 * (uint32_t)inputLine.substring(12,slash).toInt();
          uint16_t mysteps = 1000;
          isGet = true;
          mySunriseStart(mytime, mysteps, false);
          setEffect(FX_SUNSET);
          isGet = true;
          //Serial.printf("\n\tGoing to start sunrise with a duration of %u seconds.\n", mytime/1000);

          // temporary... needs different approach.

        }

        // SET RAINBOW EFFECT
        if (inputLine.length() > 3 && inputLine.substring(0,12) == F("GET /rainbow")) {
          setEffect(FX_RAINBOW);
          isGet = true;
        }
        // SET WHITE_SPARKS EFFECT
        if (inputLine.length() > 3 && inputLine.substring(0,17) == F("GET /white_sparks")) {

          setEffect(FX_WHITESPARKS);

          isGet = true;
        }
        // SET SPARKS EFFECT
        if (inputLine.length() > 3 && inputLine.substring(0,11) == F("GET /sparks")) {

          setEffect(FX_SPARKS);

          isGet = true;
        }
        // SET KNIGHTRIDER EFFECT
        if (inputLine.length() > 3 && inputLine.substring(0,16) == F("GET /knightrider")) {

          setEffect(FX_KNIGHTRIDER);
          isGet = true;
        }
        // SET no_effects
        if (inputLine.length() > 3 && inputLine.substring(0,9) == F("GET /nofx")) {

          setEffect(FX_NO_FX);

          isGet = true;
        }
        // let the given range blink
        // rework for encapsulation and such....
        if (inputLine.length() > 3 && inputLine.substring(0,11) == F("GET /blink/")) {
          int slash = inputLine.indexOf('/', 11 );
          int komma1 = inputLine.indexOf(',');
          fx_blinker_start_pixel = inputLine.substring(11, komma1).toInt();
          fx_blinker_end_pixel = inputLine.substring(komma1+1, slash).toInt();
          int urlend = inputLine.indexOf(' ', 11 );
          String getParam = inputLine.substring(slash+1,urlend+1);
          komma1 = getParam.indexOf(',');
          int komma2 = getParam.indexOf(',',komma1+1);
          int komma3 = getParam.indexOf(',',komma2+1);
          int komma4 = getParam.indexOf(',',komma3+1);
          fx_blinker_red = getParam.substring(0,komma1).toInt();
          fx_blinker_green = getParam.substring(komma1+1, komma2).toInt();
          fx_blinker_blue = getParam.substring(komma2+1, komma3).toInt();

          fx_blinker_time_on = getParam.substring(komma3+1, komma4).toInt();
          fx_blinker_time_off = getParam.substring(komma4+1).toInt();

          setEffect(FX_BLINKER);

          isGet = true;
        }
        // ws2812fx library handling
        // either move all effects together (should be easily possible to integrate in library)
        // or ease the handling a bit...
        // check how fhem could be initialized with all effects....
        if (inputLine.length() > 3 && inputLine.substring(0,14) == F("GET /ws2812fx/")) {
          //Serial.println("\nReceived ws2812 fx command...:");
          static uint8_t wsfx_mode = 0;
          int slash = inputLine.indexOf('/', 14 );
          bool next = false;
          bool prev = false;
          uint8_t new_fx = 0;
          if(inputLine.substring(14,slash) == F("next"))
          {
            next = true;
          }
          else if (inputLine.substring(14,slash) == F("prev"))
          {
            prev = true;
          }
          else
          {
              new_fx = (uint8_t)inputLine.substring(14,slash).toInt();
          }
          int urlend = inputLine.indexOf(' ', 14 );
          String getParam = inputLine.substring(slash+1,urlend+1);
          //Serial.print("getParam substring looks like: ");
          //Serial.println(getParam);
          //Serial.print("Komma drin und Position: ");
          //Serial.println(getParam.indexOf(','));
          if(getParam.indexOf(',') > 0)
          {
          //  Serial.print("Input has color values rgb: ");
            int komma1 = getParam.indexOf(',');
            int komma2 = getParam.indexOf(',',komma1+1);
            uint8_t red = (uint8_t)getParam.substring(0,komma1).toInt();
            uint8_t green = (uint8_t)getParam.substring(komma1+1,komma2).toInt();
            uint8_t blue = (uint8_t)getParam.substring(komma2+1).toInt();
            redLevel = red;
            greenLevel = green;
            blueLevel = blue;
            strip.setColor(red, green, blue);
            strip.trigger();
          }

          setEffect(FX_WS2812);

          //Serial.print("New fx raw value: ");
          Serial.println(new_fx);
          if(next) new_fx++;
          if(prev) new_fx--;

          if(new_fx >= strip.getModeCount())
          {
            new_fx = strip.getModeCount()-1;
          }
          wsfx_mode = new_fx;


          strip.setMode(wsfx_mode);
          strip.start();
          strip.trigger();

          isGet = true;
        }
        if(isGet)
        {
          sendOkResponse(client);
          // give the web browser time to receive the data
          delay(1);
          // close the connection:
          client.stop();
          Serial.println(F("client disconnected"));
        }
        else if (isPost)
        {
          /*
          sendOkResponse(client);
          // give the web browser time to receive the data
          delay(1);
          // close the connection:
          client.stop();
          Serial.println(F("client disconnected"));
          */
        }
        else
        {
          client.println(F("HTTP/1.1 500 Invalid request"));
          client.println(F("Connection: close"));  // the connection will be closed after completion of the response
          client.println();
          delay(1);
          client.stop();
          Serial.println(F("client disconnected"));
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();
    Serial.println(F("client disconnected"));
  }

  // take care of the current effects to be displayed
  effectHandler();


}

void sendOkResponse(WiFiClient client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println(F("Connection: close"));  // the connection will be closed after completion of the response
  client.println();
  // standard response
  client.print(F("OK,"));
  client.print((uint16_t)strip.getLength());
  client.print(F(","));
  uint16_t oncount=0;
  for(uint16_t i=0; i<strip.getLength(); i++) {
    if (strip.getPixelColor(i) != 0) oncount++;
  }
  if(oncount==0)
  {
    if(stripIsOn && (currentEffect != FX_NO_FX))
    {
      oncount = currentEffect;
    }
  }
  client.print(oncount);
  // Speed
  client.print(F(","));
  //client.print(strip.getSpeed());
  client.print(stripe_getDelayInterval());
  // Brightness
  client.print(F(","));
  client.print(strip.getBrightness());
  // Color
  client.print(F(","));
  client.print(strip.getColor());
  // current mode (total)
  client.print(F(","));
  if(currentEffect == FX_WS2812)
    client.print(currentEffect + strip.getMode());
  else
  {
    client.print(currentEffect);
  }
  // current Effect (text)
  client.print(F(","));
  switch (currentEffect) {
    case FX_NO_FX :
      client.print(F("No FX"));
      break;
    case FX_FIRE :
      client.print(F("Fire Effect"));
      break;
    case FX_RAINBOW :
      client.print(F("Rainbow Effect"));
      break;
    case FX_BLINKER :
      client.print(F("LED Range Blinker"));
      break;
    case FX_SPARKS :
      client.print(F("Sparks Effect"));
      break;
    case FX_WHITESPARKS :
      client.print(F("White Sparks Effect"));
      break;
    case FX_KNIGHTRIDER :
      client.print(F("Knightrider Effect"));
      break;
    case FX_SUNRISE :
      client.print(F("Sunrise Effect"));
      break;
    case FX_SUNSET :
      client.print(F("Sunset Effect"));
      break;
    case FX_WS2812 :
      client.print(F("WS2812fx "));
      client.print(strip.getModeName(strip.getMode()));
      break;
    default :
      break;
  }
}
