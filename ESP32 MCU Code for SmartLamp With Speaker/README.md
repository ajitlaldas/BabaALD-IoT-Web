# BabaALD-IoT-Web
This is the MCU Code for a SmartLamp with Speaker Device which can be controlled from my WebApp
'
/////////////////////////////////////////////////////////////////////////
Uses ESP32 microcontroller 
Written in Aurduino IDE.
/////////////////////////////////////////////////////////////////////////

Brief features:
1) Smart Lamp with RGB colour (colour mix can be set from webapp)
2) 0.96" display to show Clock & Alarm etc
3) Feature for alarm
4) Uses Firebase as backend
5) Lamp tries to connect firebase through last configured parameters (SSID, Password, Firebase API key, Database URL, Firebase ID & Password)
6) Displays status on 0.96" LCD
7) If connection/authorization fails from last used parameters then it switch WiFi AP mode & starts a simple http web server
8) IP of the web server is displayed on 0.96"LCD display. User can connect to this WiFi AP & open web page with provided IP to enter new parameters.
9) On saving new parameters the device will reboot & connect with new parameters provided (saved by device in EEPROM).

///////////////////////////////////////////////////////////////////////////
 ESP32 + WS2812 (12 LEDs) + FirebaseClient v2.2.7 (async stream)
  + 0.96" OLED SSD1306 (128x64) + Buzzer + 2x Touch Buttons

  Pin assignments:
    LED strip   : GPIO 18
    OLED SDA    : GPIO 21
    OLED SCL    : GPIO 22
    Buzzer      : GPIO 25
    Touch btn 1 : GPIO 32  (touch-capable)
    Touch btn 2 : GPIO 33  (touch-capable)
    Portal btn  : GPIO 0   (BOOT button)

  Libraries required:
    - FirebaseClient v2.2.7 (mobizt)
    - NeoPixelBus
    - NeoPixelBrightnessBus
    - ArduinoJson
    - Adafruit_SSD1306
    - Adafruit_GFX
*/ 
