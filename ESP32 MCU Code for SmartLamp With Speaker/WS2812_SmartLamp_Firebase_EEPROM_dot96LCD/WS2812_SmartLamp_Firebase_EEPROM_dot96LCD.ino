/*
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

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#define LOG_SERIAL

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FirebaseClient.h>
#include <NeoPixelBus.h>
#include <NeoPixelBrightnessBus.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include <math.h>
#include <esp_heap_caps.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================
// Pin Config
// ============================================================
const uint16_t PixelCount         = 12;
const uint8_t  PixelPin           = 18;
const uint8_t  BUZZER_PIN         = 25;
const uint8_t  TOUCH_PIN_1        = 32;
const uint8_t  TOUCH_PIN_2        = 33;
const uint8_t  PORTAL_TRIGGER_PIN = 0;

#define OLED_WIDTH       128
#define OLED_HEIGHT       64
#define OLED_RESET        -1
#define TOUCH_THRESHOLD   40   // lower = more sensitive; tune for your hardware

// ============================================================
// OLED
// ============================================================
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ============================================================
// LED Strip
// ============================================================
NeoPixelBrightnessBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> strip(PixelCount, PixelPin);

// ============================================================
// Mode Control
// ============================================================
enum DeviceMode { MODE_PORTAL, MODE_FIREBASE };
DeviceMode currentMode = MODE_PORTAL;

// ============================================================
// Firebase Objects
// ============================================================
WiFiClientSecure  ssl_client;
AsyncClientClass  aClient(ssl_client);
WiFiClientSecure  ssl_client2;
AsyncClientClass  aClient2(ssl_client2);

FirebaseApp      app;
RealtimeDatabase Database;
RealtimeDatabase Database2;
UserAuth*        user_auth = nullptr;

AsyncResult databaseResult;

static char g_scratch[512];

void asyncCB(AsyncResult& aResult);
void processData(AsyncResult& aResult);

// ============================================================
// Handshake State
// ============================================================
bool   g_handshakeDone    = false;
bool   g_handshakePending = false;
time_t g_syncedTime       = 0;

// ============================================================
// Connection Status (for icons)
// ============================================================
bool g_wifiConnected     = false;
bool g_firebaseReady     = false;
bool g_streamEstablished = false;

// ============================================================
// Alarm State
// ============================================================
struct AlarmState {
  bool          enabled      = false;
  char          setTime[6]   = {0};    // "HH:MM"
  bool          ringing      = false;
  unsigned long ringStart    = 0;
  unsigned long lastBeep     = 0;
  bool          snoozeActive = false;
  unsigned long snoozeUntil  = 0;
};
AlarmState alrm;
int lastPrintMin = 0;

// ============================================================
// Touch Button State
// ============================================================
unsigned long bothPressedAt  = 0;
bool          bothWerePressed = false;

// ============================================================
// Display Notification State
// ============================================================
char          g_lampUpdateMsg[48]  = {0};
unsigned long g_lampUpdateUntil    = 0;

// ============================================================
// Device Model
// ============================================================
struct smartDevicesAttributes {
  bool NightMode  = false;
  bool OnOff      = false;
  bool SetColour  = false;
  bool SetPattern = false;
  bool SetAlarm   = false;
};

struct mySmartLamp {
  int      RED         = 0;
  int      GREEN       = 0;
  int      BLUE        = 0;
  uint8_t  BRIGHTNESS  = 128;
  bool     isON        = false;
  bool     NightMode   = false;
  bool     isAlarmEnabled = true;
  char     Pattern[32];
  char     type[32];
  char     alarmSetTime[6];
  uint64_t lastSucessfulHandshakeEpoch = 0;
  smartDevicesAttributes attributes;
  mySmartLamp() {
    strncpy(Pattern,      "default",    sizeof(Pattern)      - 1);
    strncpy(type,         "Smart Lamp", sizeof(type)         - 1);
    strncpy(alarmSetTime, "06:00",      sizeof(alarmSetTime) - 1);
  }
};

mySmartLamp smartLamp;
RgbColor    lampPixelRGBColours[PixelCount];

// ============================================================
// Frame Timing
// ============================================================
float         frameUpdatesPerSeconds = 1.0f;
int           frameUpdatesIntervalMS = 1000;
unsigned long lastFrameMillis        = 0;

void recomputeFrameInterval() {
  float fps = (frameUpdatesPerSeconds <= 0.0f) ? 0.02f : frameUpdatesPerSeconds;
  frameUpdatesIntervalMS = (int)round(1000.0f / fps);
  if (frameUpdatesIntervalMS < 1) frameUpdatesIntervalMS = 1;
}

// ============================================================
// Config
// ============================================================
struct WifiCred { char ssid[64]; char pass[64]; };

struct Config {
  WifiCred wifi[5];
  int      wifiCount     = 0;
  int      lastWifiIndex = -1;
  char     apiKey[48];
  char     dbUrl[80];
  char     fbEmail[64];
  char     fbPass[64];
  char     deviceName[48];
  Config() {
    memset(wifi,       0, sizeof(wifi));
    memset(apiKey,     0, sizeof(apiKey));
    memset(dbUrl,      0, sizeof(dbUrl));
    memset(fbEmail,    0, sizeof(fbEmail));
    memset(fbPass,     0, sizeof(fbPass));
    memset(deviceName, 0, sizeof(deviceName));
  }
} cfg;

// ============================================================
// Portal Globals
// ============================================================
WebServer*    server       = nullptr;
DNSServer*    dns          = nullptr;
bool          portalActive = false;
unsigned long portalStart  = 0;

static wl_status_t prevWiFi  = WL_NO_SHIELD;
static bool        prevReady = false;

// ============================================================
// Global Path / UID
// ============================================================
char g_uid[64]         = {0};
char g_targetPath[160] = {0};

// ============================================================
// Heap helper
// ============================================================
static size_t getMaxFreeBlock() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

// ============================================================
// Buzzer
// ============================================================
void beep(int freqHz, int durationMs) {
  tone(BUZZER_PIN, freqHz, durationMs);
  delay(durationMs);
  noTone(BUZZER_PIN);
}

void playStartupTone() {
  beep(523, 150); delay(50);
  beep(659, 150); delay(50);
  beep(784, 400);
}

void playSuccessTone() {
  beep(784, 150); delay(50);
  beep(1047, 600);
}

void playAlarmBeep() {
  // Non-blocking single short beep — called from loop every second while ringing
  tone(BUZZER_PIN, 1800, 120);
}

void stopAlarmTone() {
  noTone(BUZZER_PIN);
}

// ============================================================
// OLED Drawing Helpers
// ============================================================

// WiFi icon ~11x11px at (x,y)
// Connected = concentric arcs + dot; disconnected = single outline + slash
void drawWifiIcon(int x, int y, bool connected) {
  if (connected) {
    display.drawCircle(x + 5, y + 8, 7, SSD1306_WHITE);
    display.drawCircle(x + 5, y + 8, 4, SSD1306_WHITE);
    display.fillCircle(x + 5, y + 8, 1, SSD1306_WHITE);
    // Clip to top half only by overdrawing bottom with black
    display.fillRect(x, y + 9, 11, 4, SSD1306_BLACK);
  } else {
    display.drawCircle(x + 5, y + 8, 5, SSD1306_WHITE);
    display.fillCircle(x + 5, y + 8, 1, SSD1306_WHITE);
    display.fillRect(x, y + 9, 11, 4, SSD1306_BLACK);
    // Diagonal slash
    display.drawLine(x + 1, y + 1, x + 10, y + 10, SSD1306_WHITE);
  }
}

// Cloud / Firebase icon ~11x8px at (x,y)
void drawCloudIcon(int x, int y, bool connected) {
  display.drawRoundRect(x, y + 3, 11, 6, 2, SSD1306_WHITE);
  display.drawCircle(x + 3,  y + 4, 2, SSD1306_WHITE);
  display.drawCircle(x + 8,  y + 4, 2, SSD1306_WHITE);
  if (!connected) {
    display.drawLine(x, y + 1, x + 10, y + 9, SSD1306_WHITE);
  }
}

// Battery icon ~14x7px at (x,y) — static placeholder (no ADC wired yet)
void drawBatteryIcon(int x, int y) {
  display.drawRect(x, y + 1, 12, 6, SSD1306_WHITE);
  display.fillRect(x + 12, y + 3, 2, 2, SSD1306_WHITE);  // terminal nub
  display.fillRect(x + 1, y + 2, 4, 4, SSD1306_WHITE);   // ~33% bar placeholder
}

// Bell icon ~10x11px at (x,y); filled = alarm enabled
void drawBellIcon(int x, int y, bool enabled) {
  if (enabled) {
    display.fillCircle(x + 5, y + 4, 4, SSD1306_WHITE);
    display.fillRect(x + 1, y + 6, 8, 3, SSD1306_WHITE);
    display.fillRect(x + 3, y + 9, 4, 2, SSD1306_WHITE);
  } else {
    display.drawCircle(x + 5, y + 4, 4, SSD1306_WHITE);
    display.drawRect(x + 1, y + 6, 8, 3, SSD1306_WHITE);
    display.drawRect(x + 3, y + 9, 4, 2, SSD1306_WHITE);
  }
}

// ============================================================
// OLED Screens
// ============================================================

void showBootScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(14, 16);
  display.print("VATSAL&DARSHU");
  display.setTextSize(1);
  display.setCursor(22, 48);
  display.print("Smart Home");
  display.display();
}

void showBootStatus(const char* line1, const char* line2 = nullptr) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 18);
  display.print(line1);
  if (line2 && line2[0]) {
    display.setCursor(0, 34);
    display.print(line2);
  }
  display.display();
}

void showPortalScreen() {
  const char* ssid  = (cfg.wifiCount > 0 && cfg.lastWifiIndex >= 0)
                      ? cfg.wifi[cfg.lastWifiIndex].ssid : "none";
  char ip[24];
  snprintf(ip, sizeof(ip), "%s", WiFi.softAPIP().toString().c_str());

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("! Setup Mode Active");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  display.setCursor(0, 12);
  display.print("No WiFi/Firebase");

  display.setCursor(0, 22);
  display.print("SSID: ");
  display.print(ssid);

  display.setCursor(0, 32);
  // Truncate email to fit
  char shortEmail[20] = {0};
  strncpy(shortEmail, cfg.fbEmail, 19);
  display.print("User: ");
  display.print(shortEmail);

  display.setCursor(0, 44);
  display.print("Connect to me:");
  display.setCursor(0, 54);
  display.print("http://");
  display.print(ip);

  display.display();
}

void showClockScreen() {
  struct tm ti;
  if (!getLocalTime(&ti)) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ---- Top bar ----
  const char* dayNames[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
  const char* monNames[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                             "JUL","AUG","SEP","OCT","NOV","DEC"};
  char dateBuf[18];
  snprintf(dateBuf, sizeof(dateBuf), "%s|%02d%s'%02d",
           dayNames[ti.tm_wday], ti.tm_mday,
           monNames[ti.tm_mon],  ti.tm_year - 100);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(dateBuf);

  // Status icons — right-aligned in top bar
  // WiFi at x=88, Cloud at x=101, Battery at x=114
  drawWifiIcon(   88, 0, g_wifiConnected);
  drawCloudIcon( 101, 0, g_firebaseReady && g_streamEstablished);
  drawBatteryIcon(114, 0);

  // Divider
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // ---- Large time ----
  display.setTextSize(3);   // each char ~18px wide, 24px tall
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", ti.tm_hour, ti.tm_min);
  // Centre horizontally: 5 chars * 18px = 90px; (128-90)/2 = 19
  display.setCursor(19, 14);
  display.print(timeBuf);

  // Seconds — small, right of main time
  display.setTextSize(1);
  char secBuf[4];
  snprintf(secBuf, sizeof(secBuf), "%02d", ti.tm_sec);
  display.setCursor(110, 30);
  display.print(secBuf);

  // Divider
  display.drawLine(0, 46, 127, 46, SSD1306_WHITE);

  // ---- Bottom bar: alarm or lamp update notification ----
  display.setTextSize(1);

  if (g_lampUpdateUntil > 0 && millis() < g_lampUpdateUntil) {
    display.setCursor(0, 52);
    display.print(g_lampUpdateMsg);
  } else {
    g_lampUpdateUntil = 0;
    drawBellIcon(0, 50, alrm.enabled);
    display.setCursor(14, 52);
    if (alrm.enabled) {
      display.print("Alarm: ");
      //display.print(alrm.setTime[0] ? alrm.setTime : smartLamp.alarmSetTime);
      display.print(alrm.setTime);
      display.print("-");
      display.print(smartLamp.alarmSetTime);
      
    } else {
      display.print("Alarm: Off");
    }
  }

  display.display();
}

// ============================================================
// Storage
// ============================================================
bool loadConfig() {
  if (!LittleFS.exists("/cfg.txt")) return false;
  File f = LittleFS.open("/cfg.txt", "r");
  if (!f) return false;
  int len = f.readBytes(g_scratch, sizeof(g_scratch) - 1);
  f.close();
  g_scratch[len] = 0;

  char* p = g_scratch;
  auto nextLine = [&]() -> char* {
    if (!p || !*p) return nullptr;
    char* start = p;
    while (*p && *p != '\n') p++;
    if (*p == '\n') { *p = 0; p++; }
    int l = strlen(start);
    if (l > 0 && start[l-1] == '\r') start[l-1] = 0;
    return start;
  };

  char* line;
  line = nextLine(); if (!line) return false;
  cfg.wifiCount = constrain(atoi(line), 0, 5);
  line = nextLine(); if (!line) return false;
  cfg.lastWifiIndex = atoi(line);
  for (int i = 0; i < cfg.wifiCount; i++) {
    line = nextLine(); if (!line) break; strncpy(cfg.wifi[i].ssid, line, 63);
    line = nextLine(); if (!line) break; strncpy(cfg.wifi[i].pass, line, 63);
  }
  line = nextLine(); if (line) strncpy(cfg.apiKey,     line, 47);
  line = nextLine(); if (line) strncpy(cfg.dbUrl,      line, 79);
  line = nextLine(); if (line) strncpy(cfg.fbEmail,    line, 63);
  line = nextLine(); if (line) strncpy(cfg.fbPass,     line, 63);
  line = nextLine(); if (line) strncpy(cfg.deviceName, line, 47);
  return true;
}

bool saveConfig() {
  File f = LittleFS.open("/cfg.txt", "w");
  if (!f) return false;
  f.println(cfg.wifiCount);
  f.println(cfg.lastWifiIndex);
  for (int i = 0; i < cfg.wifiCount; i++) {
    f.println(cfg.wifi[i].ssid);
    f.println(cfg.wifi[i].pass);
  }
  f.println(cfg.apiKey);
  f.println(cfg.dbUrl);
  f.println(cfg.fbEmail);
  f.println(cfg.fbPass);
  f.println(cfg.deviceName);
  f.close();
  return true;
}

void addOrUpdateWifi(const char* ssid, const char* pass) {
  for (int i = 0; i < cfg.wifiCount; i++) {
    if (strcmp(cfg.wifi[i].ssid, ssid) == 0) {
      strncpy(cfg.wifi[i].pass, pass, 63);
      cfg.lastWifiIndex = i;
      saveConfig(); return;
    }
  }
  int idx = (cfg.wifiCount < 5) ? cfg.wifiCount++ : max(0, cfg.lastWifiIndex);
  strncpy(cfg.wifi[idx].ssid, ssid, 63);
  strncpy(cfg.wifi[idx].pass, pass, 63);
  cfg.lastWifiIndex = idx;
  saveConfig();
}

// ============================================================
// HTML Pages
// ============================================================
String htmlPage() {
  String html; html.reserve(3000);
  html += "<!doctype html><html><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<title>Smart Lamp</title>"
          "<style>body{font-family:Arial;margin:16px}"
          "input,button{width:100%;padding:8px;margin:4px 0}"
          ".card{border:1px solid #ccc;border-radius:8px;padding:12px;margin-bottom:12px}"
          "</style></head><body><h2>Smart Lamp Setup</h2>";
  html += "<div class='card'><h3>Saved Wi-Fi</h3><form method='POST' action='/use-saved'>";
  for (int i = 0; i < cfg.wifiCount; i++) {
    html += "<label><input type='radio' name='saved' value='";
    html += String(i); html += '\'';
    if (i == cfg.lastWifiIndex) html += " checked";
    html += "> "; html += cfg.wifi[i].ssid; html += "</label><br/>";
  }
  html += "<button type='submit'>Connect Saved</button></form></div>";
  html += "<div class='card'><h3>Add / Update</h3><form method='POST' action='/submit'>"
          "SSID:<input name='ssid'>"
          "Password:<input name='wifipass' type='password'>"
          "API Key:<input name='apikey' value='"; html += cfg.apiKey;
  html += "'>DB URL:<input name='dburl' value='"; html += cfg.dbUrl;
  html += "'>Email:<input name='fbemail' value='"; html += cfg.fbEmail;
  html += "'>FB Password:<input name='fbpass' type='password'>"
          "Device Name:<input name='devname' value='"; html += cfg.deviceName;
  html += "'><button type='submit'>Save & Reboot</button></form></div></body></html>";
  return html;
}

String statusPage() {
  String s; s.reserve(900);
  s += "<!doctype html><html><head><meta charset='utf-8'><title>Status</title>"
       "<style>body{font-family:Arial;margin:16px}.kv{margin:4px 0}</style>"
       "</head><body><h2>Smart Lamp Status</h2>";
  s += "<div class='kv'><b>UID:</b> ";         s += g_uid;
  s += "</div><div class='kv'><b>Path:</b> ";  s += g_targetPath;
  s += "</div><div class='kv'><b>isON:</b> ";  s += smartLamp.isON ? "ON" : "OFF";
  s += "</div><div class='kv'><b>NightMode:</b> "; s += smartLamp.NightMode ? "true" : "false";
  s += "</div><div class='kv'><b>Pattern:</b> ";   s += smartLamp.Pattern;
  s += "</div><div class='kv'><b>RGB:</b> ";
  s += smartLamp.RED; s+=','; s+=smartLamp.GREEN; s+=','; s+=smartLamp.BLUE;
  s += "</div><div class='kv'><b>Brightness:</b> "; s += smartLamp.BRIGHTNESS;
  s += "</div><div class='kv'><b>Alarm enabled:</b> "; s += alrm.enabled ? "true" : "false";
  s += "</div><div class='kv'><b>Alarm time:</b> "; s += smartLamp.alarmSetTime;
  s += "</div><div class='kv'><b>WiFi:</b> ";   s += g_wifiConnected ? "OK" : "FAIL";
  s += "</div><div class='kv'><b>Firebase:</b> "; s += g_firebaseReady ? "OK" : "FAIL";
  s += "</div><div class='kv'><b>Stream:</b> "; s += g_streamEstablished ? "OK" : "FAIL";
  s += "</div><div class='kv'><b>Heap:</b> ";   s += ESP.getFreeHeap();
  s += "</div><div class='kv'><b>MaxBlock:</b> "; s += (uint32_t)getMaxFreeBlock();
  s += "</div><div class='kv'><b>IP:</b> "; s += WiFi.localIP().toString();
  s += "</div><p><a href='/'>Config</a></p></body></html>";
  return s;
}

// ============================================================
// Portal
// ============================================================
static void macSuffix(char* out, size_t outlen) {
  uint64_t mac = ESP.getEfuseMac();
  snprintf(out, outlen, "%06llX", mac & 0xFFFFFFULL);
}

void stopPortal() {
  if (server) { server->stop(); delete server; server = nullptr; }
  if (dns)    { dns->stop();    delete dns;    dns    = nullptr; }
  portalActive = false;
}

void startPortal() {
  WiFi.mode(WIFI_AP_STA);
  char suf[16]; macSuffix(suf, sizeof(suf));
  char apName[32]; snprintf(apName, sizeof(apName), "SmartLamp-%s", suf);
  WiFi.softAP(apName);
  delay(200);

  if (!server) server = new WebServer(80);
  if (!dns)    dns    = new DNSServer();

  server->on("/", HTTP_GET,    []() { server->send(200, "text/html", htmlPage()); });
  server->on("/status", HTTP_GET, []() { server->send(200, "text/html", statusPage()); });
  server->on("/submit", HTTP_POST, []() {
    char ssid[64]={0},wpass[64]={0},apikey[48]={0};
    char dburl[80]={0},fbemail[64]={0},fbpass[64]={0},devname[48]={0};
    strncpy(ssid,    server->arg("ssid").c_str(),    63);
    strncpy(wpass,   server->arg("wifipass").c_str(),63);
    strncpy(apikey,  server->arg("apikey").c_str(),  47);
    strncpy(dburl,   server->arg("dburl").c_str(),   79);
    strncpy(fbemail, server->arg("fbemail").c_str(), 63);
    strncpy(fbpass,  server->arg("fbpass").c_str(),  63);
    strncpy(devname, server->arg("devname").c_str(), 47);
    if (ssid[0])    addOrUpdateWifi(ssid, wpass);
    if (apikey[0])  strncpy(cfg.apiKey,     apikey,  47);
    if (dburl[0])   strncpy(cfg.dbUrl,      dburl,   79);
    if (fbemail[0]) strncpy(cfg.fbEmail,    fbemail, 63);
    if (fbpass[0])  strncpy(cfg.fbPass,     fbpass,  63);
    if (devname[0]) strncpy(cfg.deviceName, devname, 47);
    saveConfig();
    server->send(200, "text/plain", "Saved. Rebooting...");
    delay(500); ESP.restart();
  });
  server->on("/use-saved", HTTP_POST, []() {
    if (server->hasArg("saved")) {
      int idx = server->arg("saved").toInt();
      if (idx >= 0 && idx < cfg.wifiCount) cfg.lastWifiIndex = idx;
      saveConfig();
    }
    server->send(200, "text/plain", "Saved. Rebooting...");
    delay(500); ESP.restart();
  });
  server->onNotFound([]() {
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", "");
  });

  dns->start(53, "*", WiFi.softAPIP());
  server->begin();
  portalActive = true;
  portalStart  = millis();
  currentMode  = MODE_PORTAL;

  showPortalScreen();
  Serial.printf("Portal active at http://%s\n", WiFi.softAPIP().toString().c_str());
}

// ============================================================
// NTP
// ============================================================
void setupTime() {
  showBootStatus("Syncing time with", "NTP Server...");
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Syncing time");

  unsigned long start = millis();
  while (millis() - start < 15000) {
    time_t t = time(nullptr);
    if (t > 1704067200UL) {
      g_syncedTime = t;
      Serial.printf("\nTime OK: %ld\n", (long)t);
      break;
    }
    Serial.print('.'); delay(500);
  }
  if (g_syncedTime == 0)
    Serial.println("\nWARNING: NTP sync failed!");

  Serial.printf("Post-NTP: Heap=%u MaxBlock=%u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)getMaxFreeBlock());
}

double nowEpochSeconds() {
  time_t t = time(nullptr);
  if (t < 1704067200UL) {
    Serial.printf("WARNING: time() garbage: %ld — using cached\n", (long)t);
    return (double)g_syncedTime;
  }
  return (double)t;
}

// ============================================================
// Firebase Helpers
// ============================================================
bool firebaseReady() {
  return (WiFi.status() == WL_CONNECTED) && app.ready();
}

void patchHandshake() {
  if (!firebaseReady() || g_targetPath[0] == 0) return;
  if (g_handshakePending) return;

  char leaf[192];
  snprintf(leaf, sizeof(leaf), "%s/lastSucessfulHandshakeEpoch", g_targetPath);

  g_handshakePending = true;
  g_handshakeDone    = true;   // prevent re-trigger loop

  Serial.printf("Queuing handshake to: %s\n", leaf);
  Database2.set<double>(aClient2, leaf, nowEpochSeconds(), databaseResult);
  processData(databaseResult);
}

void writeSetAlarm(bool val) {
  if (!firebaseReady() || g_targetPath[0] == 0) return;
  char leaf[192];
  snprintf(leaf, sizeof(leaf), "%s/attributes/SetAlarm", g_targetPath);
  Database2.set<bool>(aClient2, leaf, val, databaseResult);
  processData(databaseResult);
  Serial.printf("writeSetAlarm(%s)\n", val ? "true" : "false");
}

// ============================================================
// JSON Helpers
// ============================================================

// Build short description of what changed in a patch event
void describePatch(JsonDocument& doc, char* out, size_t outLen) {
  char tmp[48] = "Upd:";
  bool any = false;
  auto cat = [&](const char* s) {
    if (strlen(tmp) + strlen(s) + 2 < sizeof(tmp)) {
      if (any) strncat(tmp, ",", sizeof(tmp)-strlen(tmp)-1);
      strncat(tmp, s, sizeof(tmp)-strlen(tmp)-1);
      any = true;
    }
  };

  if (doc.containsKey("Pattern"))    cat("pattern");
  if (doc.containsKey("isON"))       cat(doc["isON"] ? "ON" : "OFF");
  if (doc.containsKey("NightMode"))  cat("night");
  if (doc.containsKey("alarmSetTime")) cat("alarm time");
  if (doc.containsKey("attributes")) cat("attribs");
  if (!any) strncpy(tmp, "Lamp updated", sizeof(tmp)-1);
  strncpy(out, tmp, outLen - 1);
}

void applyJsonToLamp(JsonDocument& doc, mySmartLamp& L) {
  if (doc.containsKey("RED"))        L.RED        = (int)doc["RED"];
  if (doc.containsKey("GREEN"))      L.GREEN      = (int)((float)doc["GREEN"] * 0.85f);
  if (doc.containsKey("BLUE"))       L.BLUE       = (int)((float)doc["BLUE"]  * 0.90f);
  if (doc.containsKey("BRIGHTNESS")) L.BRIGHTNESS = (uint8_t)(int)doc["BRIGHTNESS"];
  if (doc.containsKey("isON"))       L.isON       = (bool)doc["isON"];
  if (doc.containsKey("NightMode"))  L.NightMode  = (bool)doc["NightMode"];
  if (doc.containsKey("isAlarmEnabled"))  L.isAlarmEnabled  = (bool)doc["isAlarmEnabled"];
  if (doc.containsKey("Pattern"))    strncpy(L.Pattern,      doc["Pattern"]      | "default",    sizeof(L.Pattern)      - 1);
  if (doc.containsKey("type"))       strncpy(L.type,         doc["type"]         | "Smart Lamp", sizeof(L.type)         - 1);
  if (doc.containsKey("AlarmTime")) strncpy(L.alarmSetTime, doc["AlarmTime"] | L.alarmSetTime,    sizeof(L.alarmSetTime) - 1);
  if (doc.containsKey("lastSucessfulHandshakeEpoch"))
    L.lastSucessfulHandshakeEpoch = (uint64_t)(double)doc["lastSucessfulHandshakeEpoch"];
  if (doc.containsKey("attributes")) {
    JsonObject a = doc["attributes"].as<JsonObject>();
    if (a.containsKey("NightMode"))  L.attributes.NightMode  = (bool)a["NightMode"];
    if (a.containsKey("OnOff"))      L.attributes.OnOff      = (bool)a["OnOff"];
    if (a.containsKey("SetColour"))  L.attributes.SetColour  = (bool)a["SetColour"];
    if (a.containsKey("SetPattern")) L.attributes.SetPattern = (bool)a["SetPattern"];
    if (a.containsKey("SetAlarm"))   L.attributes.SetAlarm   = (bool)a["SetAlarm"];
  }
  L.RED        = constrain(L.RED,        0, 255);
  L.GREEN      = constrain(L.GREEN,      0, 255);
  L.BLUE       = constrain(L.BLUE,       0, 255);
  L.BRIGHTNESS = (uint8_t)constrain((int)L.BRIGHTNESS, 0, 255);

  // Sync alrm state
  alrm.enabled = L.isAlarmEnabled;
  if (L.alarmSetTime[0]) strncpy(alrm.setTime, L.alarmSetTime, sizeof(alrm.setTime) - 1);
}

// ============================================================
// Pattern Engine
// ============================================================
void setAllPixelsToLampColor(const mySmartLamp& lamp) {
  RgbColor c(lamp.RED, lamp.GREEN, lamp.BLUE);
  for (int i = 0; i < PixelCount; i++) lampPixelRGBColours[i] = c;
}

void applyLamp(const mySmartLamp& lamp) {
  strip.SetBrightness(lamp.BRIGHTNESS);
  if (lamp.isON) {
    for (int i = 0; i < PixelCount; i++) strip.SetPixelColor(i, lampPixelRGBColours[i]);
  } else {
    RgbColor off(0, 0, 0);
    for (int i = 0; i < PixelCount; i++) strip.SetPixelColor(i, off);
  }
  strip.Show();
}

RgbColor hsv(float h, float s, float v) { return RgbColor(HsbColor(h, s, v)); }

unsigned long lastRandomChange = 0;
float         heartbeatBPM     = 72.0f;
float         rotatePhase      = 0.0f;
int           mutatePhase      = 0;
unsigned long mutateLastSwitch = 0;
unsigned long mutateHoldMs     = 5000;

void patternDefault(const mySmartLamp& lamp) { setAllPixelsToLampColor(lamp); applyLamp(lamp); }

void patternRandom(const mySmartLamp& lamp) {
  if (millis() - lastRandomChange >= 3000UL) {
    lastRandomChange = millis();
    RgbColor c = hsv((float)random(360)/360.0f, 0.85f, lamp.BRIGHTNESS/255.0f);
    for (int i = 0; i < PixelCount; i++) lampPixelRGBColours[i] = c;
    applyLamp(lamp);
  }
}

void patternHeartbeat(const mySmartLamp& lamp) {
  float phase = fmod((millis()%60000UL)/1000.0f * heartbeatBPM/60.0f, 1.0f);
  float vBase = lamp.BRIGHTNESS/255.0f, vPulse = 0.0f;
  if (phase < 0.10f) { vPulse = powf(1.0f - phase/0.10f, 3.0f); }
  else if (phase < 0.30f) { float x=(phase-0.20f)/0.10f; if(x<1.0f) vPulse=0.6f*powf(1.0f-x,3.0f); }
  HsbColor base(RgbColor(lamp.RED, lamp.GREEN, lamp.BLUE));
  float v = constrain(vBase*(0.4f+0.6f*(1.0f+vPulse)), 0.0f, 1.0f);
  for (int i=0;i<PixelCount;i++) lampPixelRGBColours[i]=hsv(base.H,base.S,v);
  applyLamp(lamp);
}

void patternGroove(const mySmartLamp& lamp) {
  static float h[PixelCount]; static bool init=false;
  if (!init){for(int i=0;i<PixelCount;i++) h[i]=(float)random(360)/360.0f; init=true;}
  float v=lamp.BRIGHTNESS/255.0f;
  for(int i=0;i<PixelCount;i++){
    h[i]+=0.002f+0.003f*(i%3); if(random(1000)<2) h[i]+=0.1f;
    if(h[i]>1.0f) h[i]-=1.0f;
    lampPixelRGBColours[i]=hsv(h[i],0.9f,v);
  }
  applyLamp(lamp);
}

void patternPsychedelic(const mySmartLamp& lamp) {
  static float base=0.0f; base+=0.02f; if(base>1.0f) base-=1.0f;
  float v=lamp.BRIGHTNESS/255.0f;
  for(int i=0;i<PixelCount;i++) lampPixelRGBColours[i]=hsv(fmod(base+i*0.07f,1.0f),1.0f,v);
  applyLamp(lamp);
}

void patternRotate(const mySmartLamp& lamp) {
  static RgbColor ring[PixelCount]; static bool init=false;
  if(!init){
    RgbColor v7[7]={RgbColor(148,0,211),RgbColor(75,0,130),RgbColor(0,0,255),
                    RgbColor(0,255,0),RgbColor(255,255,0),RgbColor(255,127,0),RgbColor(255,0,0)};
    for(int i=0;i<PixelCount;i++) ring[i]=v7[i%7]; init=true;
  }
  rotatePhase+=(60.0f/60.0f)*PixelCount*(frameUpdatesIntervalMS/1000.0f);
  while(rotatePhase>=PixelCount) rotatePhase-=PixelCount;
  for(int i=0;i<PixelCount;i++) lampPixelRGBColours[i]=ring[(int)floorf(rotatePhase+i)%PixelCount];
  applyLamp(lamp);
}

void patternMutate(const mySmartLamp& lamp) {
  if(millis()-mutateLastSwitch>mutateHoldMs){
    mutateLastSwitch=millis(); mutateHoldMs=4000+random(4000); mutatePhase=random(5);
  }
  switch(mutatePhase){
    case 0: patternDefault(lamp);    break;
    case 1: patternRandom(lamp);     break;
    case 2: patternHeartbeat(lamp);  break;
    case 3: patternGroove(lamp);     break;
    case 4: patternPsychedelic(lamp);break;
    default: patternDefault(lamp);
  }
}

void setFpsBasedOnState() {
  if (smartLamp.NightMode) frameUpdatesPerSeconds = 0.02f;
  else frameUpdatesPerSeconds =
    (strncmp(smartLamp.Pattern,"default",7)==0 || smartLamp.Pattern[0]==0) ? 1.0f : 30.0f;
  recomputeFrameInterval();
}

void applyNightMode() {
  const RgbColor WARM(255,147,41);
  strip.SetBrightness(30);
  for(int i=0;i<PixelCount;i++){strip.SetPixelColor(i,WARM); lampPixelRGBColours[i]=WARM;}
  strip.Show();
}

void updatePatternEngine(const mySmartLamp& lamp) {
  if(lamp.NightMode){applyNightMode(); return;}
  if(!lamp.isON){applyLamp(lamp); return;}
  char p[32]; strncpy(p,lamp.Pattern,sizeof(p)-1);
  for(char*c=p;*c;c++) *c=tolower(*c);
  if      (strcmp(p,"random")==0)                           patternRandom(lamp);
  else if (strcmp(p,"heartbeat")==0)                        patternHeartbeat(lamp);
  else if (strcmp(p,"groove")==0)                           patternGroove(lamp);
  else if (strcmp(p,"psychedelic")==0||
           strcmp(p,"pshychedelic")==0)                     patternPsychedelic(lamp);
  else if (strcmp(p,"rotate")==0)                           patternRotate(lamp);
  else if (strcmp(p,"mutate")==0)                           patternMutate(lamp);
  else                                                      patternDefault(lamp);
}

// ============================================================
// Alarm Logic
// ============================================================
void checkAlarm() {
  // Handle snooze window — alarm temporarily disabled for 5s
  if (alrm.snoozeActive) {
    if (millis() >= alrm.snoozeUntil) {
      alrm.snoozeActive = false;
      writeSetAlarm(true);   // re-enable in Firebase
    } else {
      if (alrm.ringing) { stopAlarmTone(); alrm.ringing = false; }
      return;
    }
  }

  if (!alrm.enabled) {
    if (alrm.ringing) { stopAlarmTone(); alrm.ringing = false; }
    return;
  }

  struct tm ti;
  if (!getLocalTime(&ti)) return;
   
  char nowTime[6];
  snprintf(nowTime, sizeof(nowTime), "%02d:%02d", ti.tm_hour, ti.tm_min);

  if(lastPrintMin!=(int)ti.tm_min)
  {
    Serial.printf("ALARM SETPOINT: %s, PRESNT TIME: %s ti.tm_min:%d lastPrintMin:%d\n",alrm.setTime, nowTime,(int)ti.tm_min,lastPrintMin);
    lastPrintMin = (int)ti.tm_min;
  }

  if (strcmp(nowTime, alrm.setTime) == 0) {
    if (!alrm.ringing) {
      alrm.ringing  = true;
      alrm.ringStart = millis();
      alrm.lastBeep  = 0;
      Serial.printf("Alarm ringing! time=%s\n", nowTime);
    }
  } else {
    if (alrm.ringing) {
      stopAlarmTone();
      alrm.ringing = false;
    }
  }

  // Non-blocking repeating beep every 1 second while ringing
  if (alrm.ringing) {
    unsigned long now = millis();
    if (now - alrm.lastBeep >= 1000) {
      alrm.lastBeep = now;
      playAlarmBeep();
    }
  }
}

// ============================================================
// Touch Button Logic
// ============================================================
void handleTouchButtons() {
  bool t1 = (touchRead(TOUCH_PIN_1) < TOUCH_THRESHOLD);
  bool t2 = (touchRead(TOUCH_PIN_2) < TOUCH_THRESHOLD);
  bool bothNow = t1 && t2;

  if (bothNow) {
    if (!bothWerePressed) {
      bothPressedAt  = millis();
      bothWerePressed = true;
    } else if (millis() - bothPressedAt >= 1500) {
      // Held 1.5s — dismiss alarm
      if (alrm.ringing || alrm.enabled) {
        Serial.println("Both touch held 1.5s — dismissing alarm");
        stopAlarmTone();
        alrm.ringing      = false;
        alrm.snoozeActive = true;
        alrm.snoozeUntil  = millis() + 5000;
        writeSetAlarm(false);   // write false; snooze will re-enable after 5s
        bothWerePressed = false;
        bothPressedAt   = 0;
      }
    }
  } else {
    bothWerePressed = false;
    bothPressedAt   = 0;
  }
}

// ============================================================
// Wi-Fi
// ============================================================
bool connectWiFiFromConfig() {
  if (cfg.wifiCount == 0) return false;
  int start = (cfg.lastWifiIndex >= 0) ? cfg.lastWifiIndex : 0;
  for (int t = 0; t < cfg.wifiCount; t++) {
    int i = (start + t) % cfg.wifiCount;
    char line2[40]; snprintf(line2, sizeof(line2), "%.38s", cfg.wifi[i].ssid);
    showBootStatus("Connecting WiFi:", line2);
    Serial.printf("Trying: %s\n", cfg.wifi[i].ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi[i].ssid, cfg.wifi[i].pass);
    unsigned long s = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - s < 15000) {
      delay(300); Serial.print('.');
    }
    if (WiFi.status() == WL_CONNECTED) {
      cfg.lastWifiIndex = i; saveConfig();
      g_wifiConnected = true;
      Serial.printf("\nConnected: IP=%s Heap=%u\n",
        WiFi.localIP().toString().c_str(), (unsigned)ESP.getFreeHeap());
      return true;
    }
    Serial.println("\nFailed, trying next...");
  }
  return false;
}

// ============================================================
// Firebase Init
// ============================================================
bool startFirebase() {
  if (cfg.apiKey[0]==0 || cfg.fbEmail[0]==0 || cfg.fbPass[0]==0) return false;

  char line2[40]; snprintf(line2, sizeof(line2), "%.38s", cfg.fbEmail);
  showBootStatus("Connecting backend:", line2);

  ssl_client.setInsecure();  ssl_client.setTimeout(5000);
  ssl_client2.setInsecure(); ssl_client2.setTimeout(5000);

  if (user_auth) { delete user_auth; user_auth = nullptr; }
  user_auth = new UserAuth(cfg.apiKey, cfg.fbEmail, cfg.fbPass);

  initializeApp(aClient, app, getAuth(*user_auth), asyncCB, "authTask");

  unsigned long s = millis();
  while (!app.ready() && millis() - s < 20000) { app.loop(); delay(50); }
  if (!app.ready()) { Serial.println("Auth timeout."); return false; }

  strncpy(g_uid, app.getUid().c_str(), sizeof(g_uid) - 1);
  if (g_uid[0] == 0) { Serial.println("UID empty."); return false; }
  Serial.printf("Auth OK. UID=%s Heap=%u\n", g_uid, (unsigned)ESP.getFreeHeap());

  g_firebaseReady = true;

  app.getApp<RealtimeDatabase>(Database);  Database.url(cfg.dbUrl);
  app.getApp<RealtimeDatabase>(Database2); Database2.url(cfg.dbUrl);

  snprintf(g_targetPath, sizeof(g_targetPath),
           "/users/%s/devices/%s", g_uid, cfg.deviceName);

  Database.get(aClient, g_targetPath, asyncCB, true);
  Serial.printf("Stream open: %s Heap=%u\n", g_targetPath, (unsigned)ESP.getFreeHeap());

  currentMode = MODE_FIREBASE;
  return true;
}

// ============================================================
// Async Callback
// ============================================================
void asyncCB(AsyncResult& aResult) {

  Serial.printf("asyncCB uid='%s' avail=%d isErr=%d isEvent=%d\n",
    aResult.uid().c_str(), (int)aResult.available(),
    (int)aResult.isError(), (int)aResult.isEvent());

  if (aResult.isEvent())
    Firebase.printf("Event: %s %s code:%d\n",
      aResult.uid().c_str(),
      aResult.appEvent().message().c_str(),
      aResult.appEvent().code());

  if (aResult.isDebug())
    Firebase.printf("Debug: %s\n", aResult.debug().c_str());

  if (aResult.isError()) {
    Firebase.printf("Error: %s code:%d\n",
      aResult.error().message().c_str(), aResult.error().code());
    g_streamEstablished = false;
    if (g_targetPath[0])
      Database.get(aClient, g_targetPath, asyncCB, true);
    return;
  }

  if (!aResult.available()) return;

  // ---- Handshake write result ----
  if (strcmp(aResult.uid().c_str(), "handshakeTask") == 0) {
    g_handshakePending = false;
    if (aResult.isError()) {
      Serial.printf("Handshake FAILED code:%d reason:%s\n",
        aResult.error().code(), aResult.error().message().c_str());
      g_handshakeDone = false;
    } else if (aResult.available()) {
      Serial.printf("Handshake OK. Heap=%u\n", (unsigned)ESP.getFreeHeap());
      g_handshakeDone = true;
    } else {
      Serial.println("Handshake avail=0 — retry on reconnect");
      g_handshakeDone = false;
    }
    Serial.println("Reopening stream after handshake");
    Database.get(aClient, g_targetPath, asyncCB, true);
    return;
  }

  // ---- Stream data ----
  RealtimeDatabaseResult& RTDB = aResult.to<RealtimeDatabaseResult>();
  const char* ev  = RTDB.event().c_str();
  const char* raw = RTDB.to<const char*>();

  Serial.printf("isStream=%d event='%s' raw='%.60s'\n",
    (int)RTDB.isStream(), ev ? ev : "null", raw ? raw : "null");

  if (!RTDB.isStream()) return;

  // Ignore scalar write-echo (e.g. "1768711524.0", "true", "108")
  if (!raw || raw[0] != '{') {
    Serial.printf("Ignoring non-object stream value: %.30s\n", raw ? raw : "null");
    return;
  }

  // Event string empty on first packet — raw JSON object is the signal
  const char* effectiveEv = ev;
  if (ev[0] == 0 && raw[0] == '{') {
    effectiveEv = "patch";
    Serial.println("Event empty, raw JSON object — treating as patch");
  }

  if (strcmp(effectiveEv,"put")!=0 && strcmp(effectiveEv,"patch")!=0) return;

  g_streamEstablished = true;

  Firebase.printf("event:%s Raw:%s heap:%u\n",
                  effectiveEv, raw, (unsigned)ESP.getFreeHeap());

  static StaticJsonDocument<128> envDoc;
  static StaticJsonDocument<512> lampDoc;
  envDoc.clear(); lampDoc.clear();

  const char* dataStr = raw;
  DeserializationError err = deserializeJson(envDoc, raw);
  if (!err && envDoc.containsKey("data")) {
    if (envDoc["data"].isNull()) return;
    size_t written = serializeJson(envDoc["data"], g_scratch, sizeof(g_scratch));
    if (written == 0) return;
    dataStr = g_scratch;
  }

  err = deserializeJson(lampDoc, dataStr);
  if (err) { Firebase.printf("JSON err: %s\n", err.c_str()); return; }

  // Build patch notification before applying
  bool isPatch = (strcmp(effectiveEv,"patch")==0);
  if (isPatch) {
    describePatch(lampDoc, g_lampUpdateMsg, sizeof(g_lampUpdateMsg));
    //applyJsonToLamp(lampDoc, smartLamp);
    g_lampUpdateUntil = millis() + 1000;
  }

  if (strcmp(effectiveEv,"put")==0) smartLamp = mySmartLamp();
  applyJsonToLamp(lampDoc, smartLamp);
  setAllPixelsToLampColor(smartLamp);
  setFpsBasedOnState();
  applyLamp(smartLamp);
  //describePatch(lampDoc, g_lampUpdateMsg, sizeof(g_lampUpdateMsg));
  //g_lampUpdateUntil = millis() + 1000;

  Firebase.printf("Lamp updated. Heap=%u MaxBlock=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)getMaxFreeBlock());

  if (!g_handshakeDone && !g_handshakePending) {
    Serial.println("Triggering handshake from stream data");
    patchHandshake();
  }
}

void processData(AsyncResult& aResult) {
  if (!aResult.isResult()) return;
  if (aResult.isEvent())
    Firebase.printf("Event task: %s msg: %s code: %d\n",
      aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());
  if (aResult.isDebug())
    Firebase.printf("Debug task: %s msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
  if (aResult.isError())
    Firebase.printf("Error task: %s msg: %s code: %d\n",
      aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
  if (aResult.available())
    Firebase.printf("task: %s payload: %s\n", aResult.uid().c_str(), aResult.c_str());
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(50);
  #ifdef LOG_SERIAL
  Serial.printf("\nBoot. Heap=%u MaxBlock=%u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)getMaxFreeBlock());
  #endif
  
  pinMode(PORTAL_TRIGGER_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  strip.Begin();
  strip.Show();

  // Init OLED — continue even if not found
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 not found — display disabled");
  }
  display.setTextWrap(false);

  // Boot screen + startup jingle
  showBootScreen();
  playStartupTone();
  delay(500);

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  loadConfig();
  Serial.printf("Post-load. Heap=%u\n", (unsigned)ESP.getFreeHeap());

  bool forcePortal = LittleFS.exists("/portal.flag");
  if (forcePortal) LittleFS.remove("/portal.flag");

  if (forcePortal || cfg.wifiCount == 0 || cfg.apiKey[0] == 0) {
    Serial.println("No config — portal");
    startPortal(); return;
  }

  if (!connectWiFiFromConfig()) {
    Serial.println("WiFi failed — portal");
    startPortal(); return;
  }

  if (!startFirebase()) {
    Serial.println("Firebase failed — portal");
    startPortal(); return;
  }

  setupTime();

  // All good — show success screen + tone
  showBootStatus("Connected!", "Auth OK");
  playSuccessTone();
  delay(800);

  prevWiFi  = WiFi.status();
  prevReady = app.ready();

  setAllPixelsToLampColor(smartLamp);
  setFpsBasedOnState();
  applyLamp(smartLamp);

  if (MDNS.begin("smartlamp")) Serial.println("mDNS started");

  Serial.printf("Ready. Heap=%u MaxBlock=%u\n  http://%s/status\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)getMaxFreeBlock(),
                WiFi.localIP().toString().c_str());
}

// ============================================================
// Loop
// ============================================================
unsigned long lastDisplayMs = 0;

void loop() {
  // Hold BOOT button 3s → portal
  static unsigned long btnHeld = 0;
  if (digitalRead(PORTAL_TRIGGER_PIN) == LOW) {
    if (!btnHeld) btnHeld = millis();
    if (millis() - btnHeld > 3000) {
      File f = LittleFS.open("/portal.flag","w");
      if (f) { f.print("1"); f.close(); }
      ESP.restart();
    }
  } else btnHeld = 0;

  if (currentMode == MODE_PORTAL) {
    if (dns)    dns->processNextRequest();
    if (server) server->handleClient();
    if (portalActive && millis() - portalStart > 180000UL) ESP.restart();
    return;
  }

  // ---- Firebase mode ----
  app.loop();
  Database.loop();

  handleTouchButtons();
  checkAlarm();

  // Pattern engine
  if (millis() - lastFrameMillis >= (unsigned long)frameUpdatesIntervalMS) {
    lastFrameMillis = millis();
    updatePatternEngine(smartLamp);
  }

  // Display refresh every 500ms (clock ticks on seconds anyway)
  if (millis() - lastDisplayMs >= 500) {
    lastDisplayMs = millis();
    g_wifiConnected = (WiFi.status() == WL_CONNECTED);
    g_firebaseReady = app.ready();
    showClockScreen();
  }

  // Reconnect detection — reset handshake so it fires again on next stream data
  wl_status_t nowWiFi  = WiFi.status();
  bool        nowReady = app.ready();
  if ((prevWiFi != WL_CONNECTED && nowWiFi == WL_CONNECTED) ||
      (!prevReady && nowReady)) {
    Serial.println("Reconnected — resetting handshake flags");
    g_handshakeDone     = false;
    g_handshakePending  = false;
    g_streamEstablished = false;
  }
  prevWiFi = nowWiFi;
  prevReady = nowReady;

  // Heap watchdog
  if (ESP.getFreeHeap() < 12000) {
    Serial.printf("Critical heap %u — rebooting\n", (unsigned)ESP.getFreeHeap());
    delay(100); ESP.restart();
  }

  delay(5);
}