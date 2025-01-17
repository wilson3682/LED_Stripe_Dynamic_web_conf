This work is based on inredibly good work from many others... I just modified it to my personal use.

So thanks to:
-------------
   - FastLED library - see http://www.fastLed.io
   - ESPWebserver - see https://github.com/jasoncoon/esp8266-fastled-webserver
   - Adafruit Neopixel https://github.com/adafruit/Adafruit_NeoPixel
   - WS2812FX library https://github.com/kitesurfer1404/WS2812FX
   - fhem esp8266 implementation - Idea from https://github.com/sw-home/FHEM-LEDStripe 

# LED_Stripe_Dynamic_web_conf
LED Stripe including simple Web Server, basics from the ws2812fx library - but different effects etc... 
FHEM control is possible with the related perl fhem module...

- need to describe installation etc. in more detail - first approach below

Major differences to WS2812FX / ESPWebserver:
-
- somehow combines the "best of both"
- almost everything is adjustable via web interface
- includes (basic) sunrise / sunset effects with adjustable time (minutes) 
- a void effect which can be used to remotely change sinlge LEDs or ranges
- everything is smoothed, i.e. switching and changeing is smoothened to the max, brighness change is smoothed...
- can be controlled from FHEM (home automation - separate module not yet published)
- heavily use of color palettes...
- setting of parameters / modes is no longer a complete "REST" interface but done with HTTP GET query strings. This allows changing more parameters at once (either via URL with query string directly or via home automation...). e.g. to activate effect number 2 with color palette number 3 and the speed set to 1000 it just needs: http://esp.address:/set?mo=2&pa=3&sp=1000 - changes are communicated via HTTP response and websocket broadcast. The current state can be checked with http://esp.address/status
- every setting is stored to the EEPROM (with CRC protection, defaults being loaded on mismatch, sudden ESP reset (by watchdog or exception)
- ...



If you intend to use, compile and run this code you need to:
- have a WS2812FX strip on **Pin 3 (RX)** on a ESP8266 (nodemcu / wemos D1 mini)
- set necessary build flags: e.g.: https://github.com/tobi01001/LED_Stripe_Dynamic_web_conf/blob/104365b7201e178d758255fcc8b8eab150287510/platformio.ini#L20 / **'-DLED_NAME="LED Development Board"' -DLED_COUNT=250** defining the LED_NAME (the web page and hostname is set to this) and the number of LEDs (LED_COUNT) on the stripe (theoretically limited to 65535 but for performance reasons rather limited to 300 (max fps you can get with 300 LEDs will be around 111)
- upload the data file system image
- upload the compiled code

I personally use platform.io with Visual Studio Code. So I don't know exactly how to do this with the Arduino IDE.

When you first startup and the WiFiManager Library does not find credentials being stored it will open a WiFi access point. Connect to it and navigate to 192.168.4.1. There you should be able to connect to your WiFi. After restrart the ESP connects to your Wifi and if its ready to go all the LEDs will fade to Green and back to black...

If you define the DEBUG build flag (-DDEBUG) it will enable debug code and the WifiManager will also publish the IP-Adress received...

The Web-Page is a mix of German and English (sorry) - I needed to provide somethign for my kids. But I will change back to complete English (or a language flag) in the future...


in case of questions, comments, issues ... feel free to contact me.
