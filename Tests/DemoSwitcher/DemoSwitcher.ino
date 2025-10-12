#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <time.h>

// =======================
// Waveshare ESP32-S3 1.47" LCD pins
// =======================
#define TFT_MOSI 45
#define TFT_SCLK 40
#define TFT_CS   42
#define TFT_DC   41
#define TFT_RST  39
#define TFT_BL   48

// BOOT button (GPIO0, active LOW)
#define BTN_BOOT 0

// =======================
// Display setup (ST7789 172x320, offsets for 1.47")
// =======================
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, TFT_RST,
  1 /* rotation (try 0-3) */, true /* IPS */,
  172, 320,
  34, 0,   // col offset 1, row offset 1
  34, 0    // col offset 2, row offset 2
);

// =======================
// Wi-Fi / NTP
// =======================
const char* WIFI_SSID = "Nomad6";
const char* WIFI_PASS = "JesusLives106";
// Central Time with DST
const char* TZ_STRING = "CST6CDT,M3.2.0/2,M11.1.0/2";

// =======================
// Mode state
// =======================
enum Mode { MODE_HELLO = 0, MODE_LIGHT, MODE_INFO, MODE_CLOCK, MODE_COUNT };
int currentMode = MODE_HELLO;

// Button debounce
bool lastBtn = true;           // pulled-up (true = not pressed)
uint32_t lastBtnChange = 0;
const uint16_t DEBOUNCE_MS = 40;

// Light test state
uint8_t colorIndex = 0;
uint32_t lastLightStep = 0;
const uint16_t LIGHT_STEP_MS = 600;

// Clock state
bool wifiReady = false;
bool timeSynced = false;
uint32_t lastClockDraw = 0;
String lastTimeStr;

// =======================
// Helpers
// =======================
bool checkButtonPressed() {
  bool reading = digitalRead(BTN_BOOT);  // HIGH = not pressed, LOW = pressed
  if (reading != lastBtn) {
    lastBtnChange = millis();
    lastBtn = reading;
  }
  if ((millis() - lastBtnChange) > DEBOUNCE_MS) {
    static bool prevStable = true;
    bool stable = reading;
    bool pressed = (prevStable == true && stable == false); // falling edge
    prevStable = stable;
    return pressed;
  }
  return false;
}

void drawHeader(const char* title) {
  gfx->fillRect(0, 0, 320, 18, DARKGREY);
  gfx->setCursor(6, 4);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(1);
  gfx->print(title);
  gfx->setCursor(220, 4);
  gfx->print("BOOT=Next");
}

// =======================
// Mode screens
// =======================
void startHello() {
  gfx->fillScreen(BLACK);
  drawHeader("Hello World");
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(20, 50);
  gfx->print("Hello, world!");
  gfx->setTextSize(1);
  gfx->setCursor(20, 90);
  gfx->print("Waveshare ESP32-S3 1.47\"");
}

void runHello() {}

// --- Light Test (no PWM) ---
void startLight() {
  gfx->fillScreen(BLACK);
  drawHeader("LCD Light Test");
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);      // backlight ON
  colorIndex = 0;
  lastLightStep = 0;
}

void runLight() {
  uint32_t now = millis();
  if (now - lastLightStep >= LIGHT_STEP_MS) {
    lastLightStep = now;
    uint16_t color = BLACK;
    const char* name = "";

    switch (colorIndex) {
      case 0: color = RED;     name = "RED"; break;
      case 1: color = GREEN;   name = "GREEN"; break;
      case 2: color = BLUE;    name = "BLUE"; break;
      case 3: color = YELLOW;  name = "YELLOW"; break;
      case 4: color = CYAN;    name = "CYAN"; break;
      case 5: color = MAGENTA; name = "MAGENTA"; break;
      case 6: color = WHITE;   name = "WHITE"; break;
      default: color = BLACK;  name = "BLACK"; break;
    }

    gfx->fillRect(0, 18, 320, 154, color); // below header
    gfx->fillRect(0, 172, 320, 172, BLACK);
    gfx->setTextColor(WHITE, BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(20, 190);
    gfx->printf("Color: %s", name);

    colorIndex = (colorIndex + 1) % 8;
  }
}

void startInfo() {
  gfx->fillScreen(BLACK);
  drawHeader("Hardware Info");
  gfx->setTextColor(WHITE);
  gfx->setTextSize(1);

  int y = 28;
  auto line = [&](const char* label, const String& val) {
    gfx->setCursor(8, y);
    gfx->printf("%s: %s", label, val.c_str());
    y += 14;
  };

  line("Chip", String(ESP.getChipModel()));
  line("Revision", String(ESP.getChipRevision()));
  line("CPU MHz", String(ESP.getCpuFreqMHz()));
  line("Flash Size", String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB");
  line("PSRAM", psramFound() ? (String(ESP.getPsramSize() / (1024 * 1024)) + " MB") : "No");
  line("Heap (free)", String(ESP.getFreeHeap() / 1024) + " KB");

  uint64_t mac = ESP.getEfuseMac();
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           (uint8_t)(mac >> 40), (uint8_t)(mac >> 32), (uint8_t)(mac >> 24),
           (uint8_t)(mac >> 16), (uint8_t)(mac >> 8), (uint8_t)(mac));
  line("MAC", String(macStr));
}
void runInfo() {}

void drawClockFace(const char* topLine) {
  gfx->fillScreen(BLACK);
  drawHeader("Clock (NTP)");
  gfx->setTextColor(WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(8, 28);
  gfx->print(topLine);
}

bool connectWiFiWithTimeout(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  uint8_t dots = 0;
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    if (checkButtonPressed()) return false; // allow skipping
    delay(100);
    gfx->fillRect(8, 44, 304, 14, BLACK);
    gfx->setCursor(8, 44);
    gfx->print("Connecting");
    for (uint8_t i = 0; i < dots; i++) gfx->print(".");
    dots = (dots + 1) % 4;
  }
  return false;
}

bool syncTimeWithTimeout(uint32_t timeoutMs) {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", TZ_STRING, 1);
  tzset();

  uint32_t start = millis();
  struct tm t;
  while (millis() - start < timeoutMs) {
    if (checkButtonPressed()) return false;
    if (getLocalTime(&t, 500)) return true;
  }
  return false;
}

void startClock() {
  wifiReady = false;
  timeSynced = false;
  lastClockDraw = 0;
  lastTimeStr = "";
  drawClockFace("WiFi + NTP setup...");

  if (String(WIFI_SSID).length() == 0) {
    gfx->setCursor(8, 44);
    gfx->print("Set WIFI_SSID/WIFI_PASS in code.");
    return;
  }

  if (connectWiFiWithTimeout(8000)) {
    wifiReady = true;
    gfx->fillRect(8, 44, 304, 14, BLACK);
    gfx->setCursor(8, 44);
    gfx->print("WiFi connected. Syncing time...");
    if (syncTimeWithTimeout(6000)) {
      timeSynced = true;
      gfx->fillRect(8, 44, 304, 14, BLACK);
    } else {
      gfx->fillRect(8, 44, 304, 14, BLACK);
      gfx->setCursor(8, 44);
      gfx->print("NTP failed.");
    }
  } else {
    gfx->fillRect(8, 44, 304, 14, BLACK);
    gfx->setCursor(8, 44);
    gfx->print("WiFi failed or skipped.");
  }
}

void runClock() {
  if (!wifiReady || !timeSynced) return;

  uint32_t now = millis();
  if (now - lastClockDraw >= 200) {
    lastClockDraw = now;

    struct tm t;
    if (!getLocalTime(&t, 100)) return;

    char buf[40];
    strftime(buf, sizeof(buf), "%I:%M:%S %p", &t);
    String timeStr = String(buf);

    if (timeStr != lastTimeStr) {
      gfx->fillRect(0, 64, 320, 70, BLACK);
      gfx->setTextColor(WHITE);
      gfx->setTextSize(3);
      gfx->setCursor(24, 80);
      gfx->print(timeStr);

      gfx->setTextSize(1);
      char dbuf[40];
      strftime(dbuf, sizeof(dbuf), "%a %b %d, %Y", &t);
      gfx->fillRect(0, 140, 320, 20, BLACK);
      gfx->setCursor(24, 144);
      gfx->print(dbuf);

      lastTimeStr = timeStr;
    }
  }
}

// =======================
// Mode control
// =======================
void startMode(Mode m) {
  switch (m) {
    case MODE_HELLO: startHello(); break;
    case MODE_LIGHT: startLight(); break;
    case MODE_INFO:  startInfo();  break;
    case MODE_CLOCK: startClock(); break;
    default: break;
  }
}
void runMode(Mode m) {
  switch (m) {
    case MODE_HELLO: runHello(); break;
    case MODE_LIGHT: runLight(); break;
    case MODE_INFO:  runInfo();  break;
    case MODE_CLOCK: runClock(); break;
    default: break;
  }
}
void nextMode() {
  currentMode = (currentMode + 1) % MODE_COUNT;
  startMode((Mode)currentMode);
}

// =======================
// Setup / Loop
// =======================
void setup() {
  pinMode(BTN_BOOT, INPUT_PULLUP);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);   // backlight ON

  gfx->begin();
  startMode((Mode)currentMode);
}

void loop() {
  if (checkButtonPressed()) nextMode();
  runMode((Mode)currentMode);
  delay(5);
}
