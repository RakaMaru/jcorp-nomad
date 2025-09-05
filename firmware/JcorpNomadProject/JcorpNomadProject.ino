//Jcorp Nomad Project
#include <Arduino.h>
#define FF_USE_FASTSEEK 1
#define SD_FREQ_KHZ 10000  // ✱✱ VERY IMPORTANT SETTING ✱✱ \
                           // This controls how fast reads from your SD Card can go, If you have a name brand fancy card you can go faster with better results. Check what your card recomends. \
                           // 10 000 kHz (10 MHz) = safest \
                           // 12000 kHz (12 MHz) = good \
                           // 20000 kHz (20 MHz) = fastest
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include "SD_MMC.h"
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <map>
#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "ui.h"
#include "RGB_lamp.h"
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include "usb_mode.h"
#include "boot_mode.h"  // custom library for firmware switching

void launch_usb_mode() {
  extern void usb_setup();
  extern void usb_loop();
  // Initialize only the USB‑MSC path
  usb_setup();

  // Then run only that path forever
  for (;;) {
    usb_loop();
    // optionally: delay(0); yield();
  }
}

#define BOOT_BUTTON_PIN 0
int screenBrightness = 100;  // 0-100, default full brightness
void handleConnector(AsyncWebServerRequest *request);
unsigned long lastTempReading = 0;
float currentTempC = 0.0;
static bool sdScanned = false;
const uint32_t SD_SCAN_DELAY = 5000;  // milliseconds after boot
// ==================== CONFIGURATION ====================

// Max number of devices that can connect at once
#define MAX_CLIENTS 4  // I recomend 4, If you want to try more knock yourself out!

struct AdminSettings {
  String rgbMode = "off";
  String rgbColor = "#ff0000";
  String adminPassword = "";
  String wifiSSID = "Jcorp_Nomad";
  String wifiPassword = "password";
  int brightness = 230;
  bool autoGenerateMedia = false;
};

AdminSettings settings;
const char *SETTINGS_PATH = "/config/settings.json";
// =================== scary config ====================
// SD card pinout for Waveshare ESP32-S3
#define SD_CLK_PIN 14
#define SD_CMD_PIN 15
#define SD_D0_PIN 16
#define SD_D1_PIN 18
#define SD_D2_PIN 17
#define SD_D3_PIN 21
// ---------------URL Encode -------------
String urlencode(String str) {
  String encoded = "";
  char c;
  char code0, code1;
  char code[] = "0123456789ABCDEF";

  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      code0 = code[(c >> 4) & 0xF];
      code1 = code[c & 0xF];
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

// Settings Setup:
bool loadSettings() {
  if (!SD_MMC.exists(SETTINGS_PATH)) {
    Serial.println("Settings file not found. Generating default.");
    return saveSettings();  // Save defaults
  }

  File file = SD_MMC.open(SETTINGS_PATH);
  if (!file || file.isDirectory()) {
    Serial.println("Failed to open settings file.");
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Failed to parse settings JSON.");
    return false;
  }

  settings.rgbMode = doc["rgbMode"] | "off";
  settings.rgbColor = doc["rgbColor"] | "#ff0000";
  settings.adminPassword = doc["adminPassword"] | "";
  settings.wifiSSID = doc["wifiSSID"] | "Jcorp_Nomad";
  settings.wifiPassword = doc["wifiPassword"] | "password";
  settings.brightness = doc["brightness"] | 230;
  settings.autoGenerateMedia = doc["autoGenerateMedia"] | false;

  return true;
}

bool saveSettings() {
  SD_MMC.mkdir("/config");  // Ensure directory exists

  File file = SD_MMC.open(SETTINGS_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open settings file for writing.");
    return false;
  }

  StaticJsonDocument<512> doc;
  doc["rgbMode"] = settings.rgbMode;
  doc["rgbColor"] = settings.rgbColor;
  doc["adminPassword"] = settings.adminPassword;
  doc["wifiSSID"] = settings.wifiSSID;
  doc["wifiPassword"] = settings.wifiPassword;
  doc["brightness"] = settings.brightness;
  doc["autoGenerateMedia"] = settings.autoGenerateMedia;

  bool success = serializeJson(doc, file) > 0;
  file.close();
  return success;
}

// --------------- Media Generation Stuff ------------
bool isAlwaysGenerateEnabled() {
  return SD_MMC.exists("/always_generate.flag");
}

void enableAlwaysGenerate() {
  File f = SD_MMC.open("/always_generate.flag", FILE_WRITE);
  if (f) {
    f.print("1");
    f.close();
  }
}

void disableAlwaysGenerate() {
  SD_MMC.remove("/always_generate.flag");
}

bool isOneTimeGenerateRequested() {
  return SD_MMC.exists("/generate_once.flag");
}

void requestOneTimeGenerate() {
  File f = SD_MMC.open("/generate_once.flag", FILE_WRITE);
  if (f) {
    f.print("1");
    f.close();
  }
}

void clearOneTimeGenerate() {
  SD_MMC.remove("/generate_once.flag");
}
//------------------- delete recursive for admin -------------
bool deleteRecursive(String path) {
  File entry = SD_MMC.open(path);
  if (!entry) return false;

  if (!entry.isDirectory()) {
    entry.close();
    return SD_MMC.remove(path);
  }

  File child;
  while ((child = entry.openNextFile())) {
    String childPath = String(path) + "/" + child.name();
    deleteRecursive(childPath);
    child.close();
  }

  entry.close();
  return SD_MMC.rmdir(path);
}

// ───────────────── SD‑recovery globals ───────────────
volatile bool sdErrorFlag = false;
unsigned long sdErrorCooldownUntil = 0;

bool tryRecoverSDCard() {
  Serial.println("[SD] Attempting recovery…");
  SD_MMC.end();  // unmount
  delay(1000);   // give hardware a breather

  if (!SD_MMC.begin("/sdcard", true, false, SD_FREQ_KHZ, 12)) {
    Serial.println("[SD] Recovery failed.");
    return false;
  }
  Serial.println("[SD] Recovery OK.");
  return true;
}

String rfc3339Now() {
  return "2025-07-12T12:00:00Z";  // Hard-coded UTC timestamp, or it gets mad
}

// Captive portal DNS setup
const byte DNS_PORT = 53;
DNSServer dnsServer;
AsyncWebServer server(80);  // Web server on port 80
std::map<AsyncWebServerRequest *, File> activeUploads;
int connectedClients = 0;

// LED Mode and Color Helper Wrappers
uint8_t currentLEDMode = 0;  // 0=off, 1=rainbow, 2=solid color
uint8_t solidR = 0, solidG = 0, solidB = 0;
void RGB_SetColor(uint8_t r, uint8_t g, uint8_t b) {
  solidR = r;
  solidG = g;
  solidB = b;
  currentLEDMode = 2;
  Set_Color(g, r, b);
}
extern lv_obj_t *ui_wifi;
extern lv_obj_t *ui_SDcard;
bool lastWifiStatus = false;
bool lastSDStatus = false;

//Globals for SD scan
unsigned long lastUpdateTime = 0;
unsigned long lastSDScanTime = 0;
const unsigned long SD_SCAN_INTERVAL = 60000;  // 60 seconds
uint64_t cachedUsedBytes = 0;
uint64_t cachedTotalBytes = 0;
unsigned long lastScanTime = 0;

// Update the UI with the number of connected users
void updateUI(int userCount) {
  char buffer[10];
  snprintf(buffer, sizeof(buffer), "%d", userCount);
  lv_label_set_text(ui_userlabel, buffer);
}

void updateToggleStatus() {
  bool currentWifiStatus = WiFi.softAPIP();
  if (currentWifiStatus != lastWifiStatus) {
    if (currentWifiStatus) {
      lv_obj_add_state(ui_wifi, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(ui_wifi, LV_STATE_CHECKED);
      Serial.println("[Status] WiFi AP failure detected.");
    }
    lastWifiStatus = currentWifiStatus;
  }

  bool currentSDStatus = SD_MMC.cardType() != CARD_NONE;  // ✱✱ NEW / CHANGED ✱✱
  if (currentSDStatus != lastSDStatus) {
    if (currentSDStatus) {
      lv_obj_add_state(ui_SDcard, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(ui_SDcard, LV_STATE_CHECKED);
      Serial.println("[Status] SD card failure detected.");
    }
    lastSDStatus = currentSDStatus;
  }
}

// Stream a chunk of text to save RAM
void opdsWrite(AsyncResponseStream *s, const String &chunk) {
  s->print(chunk);
}

//(OPDS thing)
String xmlEscape(const String &in) {  // we already added this
  String out;
  for (char c : in) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      default: out += c; break;
    }
  }
  return out;
}

String slugify(const String &in) {  // new helper
  String out;
  for (char c : in) {
    if (isalnum(c)) out += (char)tolower(c);
    else if (c == ' ' || c == '_' || c == '-') out += '-';
    // every other char is dropped
  }
  return out;
}

// Get a count of currently connected WiFi clients
void updateClientCount() {
  wifi_sta_list_t wifi_sta_list;
  if (esp_wifi_ap_get_sta_list(&wifi_sta_list) == ESP_OK) {
    connectedClients = wifi_sta_list.num;
    updateUI(connectedClients);
  }
}

// Utility: extract base name (no path, no extension)
String getFileBaseName(const String &name) {
  String base = name.substring(name.lastIndexOf('/') + 1);
  int dotIndex = base.lastIndexOf('.');
  if (dotIndex != -1) base = base.substring(0, dotIndex);
  return base;
}
bool isValidExtension(const String &filename, const std::vector<String> &exts) {
  String nameLower = filename;
  nameLower.toLowerCase();
  for (const auto &ext : exts) {
    if (nameLower.endsWith(ext)) return true;
  }
  return false;
}

void generateMediaJSON() {
  // ─── 1) Open the JSON file ───
  File jsonFile = SD_MMC.open("/media.json", FILE_WRITE);
  if (!jsonFile) {
    Serial.println("[MediaGen] ERROR: cannot open media.json for writing");
    return;
  }

  // ─── 2) Write opening brace ───
  jsonFile.println("{");

  // ─── 3) Initialize LVGL status screen ───
  lv_obj_clear_flag(ui_MediaGen, LV_OBJ_FLAG_HIDDEN);
  String logBuf = "Generating media.json...\n";
  int logCount = 0;
  const int LOG_UPDATE_INTERVAL = 20;
  lv_textarea_set_text(ui_MediaGen, logBuf.c_str());
  lv_timer_handler();

  // Helper lambda to flush log
  auto flushLog = [&]() {
    lv_textarea_set_text(ui_MediaGen, logBuf.c_str());
    lv_textarea_set_cursor_pos(ui_MediaGen, logBuf.length());
    lv_timer_handler();
    logBuf = "";
    logCount = 0;
  };

  // ─── 4) Movies ───
  jsonFile.println("  \"movies\": [");

  File moviesDir = SD_MMC.open("/Movies");
  bool firstMovie = true;
  std::vector<String> movieExts = {
    ".mp4", ".mkv", ".avi", ".mov", ".webm", ".m4v", ".wmv", ".flv",
    ".mpg", ".mpeg", ".ts", ".3gp"
  };

  // Safety: moviesDir might fail to open
  if (moviesDir) {
    while (true) {
      File file = moviesDir.openNextFile();
      if (!file) break;  // no more entries

      if (!file.isDirectory()) {
        String fname = String(file.name());  // e.g., "MovieName.mp4" or "/Movies/MovieName.mp4"
        // Determine extension safely
        int dotPos = fname.lastIndexOf('.');
        String ext = (dotPos >= 0) ? fname.substring(dotPos) : String("");
        ext.toLowerCase();

        if (isValidExtension(fname, movieExts)) {
          // Base name without path and ext
          String base = getFileBaseName(fname);

          if (!firstMovie) jsonFile.println(",");
          firstMovie = false;

          // Optional cover beside the movie file (real file on SD)
          String coverDiskPath = "/Movies/" + base + ".jpg";
          // URL/path we’ll put in JSON (your UI expects lower-case "movies/" here)
          String coverJsonPath = SD_MMC.exists(coverDiskPath) ? ("movies/" + base + ".jpg")
                                                              : "placeholder.jpg";

          jsonFile.print("    { \"name\": \"" + base + "\", "
                                                       "\"cover\": \""
                         + coverJsonPath + "\", "
                                           "\"file\": \"Movies/"
                         + base + ext + "\" }");

          logBuf += "- " + fname + "\n";
          if (++logCount % LOG_UPDATE_INTERVAL == 0) flushLog();
        }
      }

      file.close();  // close as we go
      yield();
    }
    moviesDir.close();
  }

  jsonFile.println();
  jsonFile.println("  ],");
  flushLog();

  // ─── 5) Shows ───
  jsonFile.println("  \"shows\": [");
  File showsRoot = SD_MMC.open("/Shows");
  bool firstShow = true;
  while (File folder = showsRoot.openNextFile()) {
    yield();
    if (folder.isDirectory()) {
      if (!firstShow) jsonFile.println(",");
      firstShow = false;
      String showName = String(folder.name());

      String cover = SD_MMC.exists("/Shows/" + showName + ".jpg") ? "Shows/" + showName + ".jpg" : "placeholder.jpg";


      jsonFile.println("    {\"name\": \"" + showName + "\", \"cover\": \"" + cover + "\", \"episodes\": [");

      logBuf += "Show: " + showName + "\n";
      if (++logCount % LOG_UPDATE_INTERVAL == 0) flushLog();

      File epDir = SD_MMC.open("/Shows/" + showName);
      bool firstEp = true;
      while (File ep = epDir.openNextFile()) {
        yield();
        String epName = String(ep.name());
        String epExt = epName.substring(epName.lastIndexOf('.'));
        epExt.toLowerCase();
        if (!ep.isDirectory() && isValidExtension(epName, movieExts)) {
          if (!firstEp) jsonFile.println(",");
          firstEp = false;
          String epBase = getFileBaseName(epName);
          jsonFile.print("      { \"name\": \"" + epBase + "\", \"file\": \"Shows/" + showName + "/" + epBase + epExt + "\" }");

          logBuf += "  * " + epName + "\n";
          if (++logCount % LOG_UPDATE_INTERVAL == 0) flushLog();
        }
      }
      jsonFile.println();
      jsonFile.println("    ]}");
    }
  }

  jsonFile.println();
  jsonFile.println("  ],");
  flushLog();

  // ─── 6) Books ───
  jsonFile.println("  \"books\": [");
  File booksDir = SD_MMC.open("/Books");
  bool firstBook = true;
  std::vector<String> bookExts = { ".pdf", ".epub", ".mobi", ".azw3", ".txt" };
  while (File file = booksDir.openNextFile()) {
    yield();
    String fname = String(file.name());
    String ext = fname.substring(fname.lastIndexOf('.'));
    if (!file.isDirectory() && isValidExtension(fname, bookExts)) {
      if (!firstBook) jsonFile.println(",");
      firstBook = false;
      String base = getFileBaseName(fname);
      String cover = SD_MMC.exists("/Books/" + base + ".jpg") ? "books/" + base + ".jpg" : "placeholder.jpg";

      jsonFile.print("    { \"name\": \"" + base + "\", \"cover\": \"" + cover + "\", \"file\": \"Books/" + base + ext + "\" }");

      logBuf += "- " + fname + "\n";
      if (++logCount % LOG_UPDATE_INTERVAL == 0) flushLog();
    }
  }

  jsonFile.println();
  jsonFile.println("  ],");
  flushLog();

  // ─── 7) Music ───
  jsonFile.println("  \"music\": [");
  File musicDir = SD_MMC.open("/Music");
  bool firstMusic = true;
  std::vector<String> musicExts = { ".mp3", ".wav", ".flac" };
  while (File file = musicDir.openNextFile()) {
    yield();
    String fname = String(file.name());
    String ext = fname.substring(fname.lastIndexOf('.'));
    ext.toLowerCase();
    if (!file.isDirectory() && isValidExtension(fname, musicExts)) {
      if (!firstMusic) jsonFile.println(",");
      firstMusic = false;
      String base = getFileBaseName(fname);

      String cover = SD_MMC.exists("/Music/" + base + ".jpg") ? "Music/" + base + ".jpg" : "placeholder.jpg";

      jsonFile.print("    { \"name\": \"" + base + "\", \"cover\": \"" + cover + "\", \"file\": \"Music/" + base + ext + "\" }");

      logBuf += "- " + fname + "\n";
      if (++logCount % LOG_UPDATE_INTERVAL == 0) flushLog();
    }
  }

  jsonFile.println();
  jsonFile.println("  ],");
  flushLog();

  // ─── 8) Playlists ───
  jsonFile.println("  \"playlists\": {");
  File musicRoot = SD_MMC.open("/Music");
  bool firstPl = true;
  while (File folder = musicRoot.openNextFile()) {
    yield();
    if (folder.isDirectory()) {
      if (!firstPl) jsonFile.println(",");
      firstPl = false;
      String plName = String(folder.name());
      jsonFile.println("    \"" + plName + "\": [");

      logBuf += "Playlist: " + plName + "\n";
      if (++logCount % LOG_UPDATE_INTERVAL == 0) flushLog();

      std::vector<String> tracks;
      std::function<void(File, const String &)> collectTracks =
        [&](File dir, const String &rel) {
          while (File t = dir.openNextFile()) {
            yield();
            String name = rel + "/" + String(t.name());
            if (t.isDirectory()) collectTracks(SD_MMC.open("/Music/" + name), name);
            else if (isValidExtension(name, musicExts)) tracks.push_back(name);
          }
        };
      collectTracks(SD_MMC.open("/Music/" + plName), plName);

      for (size_t i = 0; i < tracks.size(); ++i) {
        String tname = tracks[i].substring(tracks[i].lastIndexOf('/') + 1);
        String base2 = getFileBaseName(tname);

        String cover2 = SD_MMC.exists("/Music/" + base2 + ".jpg") ? "Music/" + base2 + ".jpg" : "placeholder.jpg";

        jsonFile.print("      { \"name\": \"" + base2 + "\", \"cover\": \"" + cover2 + "\", \"file\": \"Music/" + tracks[i] + "\" }");
        if (i < tracks.size() - 1) jsonFile.println(",");

        logBuf += "  * " + tname + "\n";
        if (++logCount % LOG_UPDATE_INTERVAL == 0) flushLog();
      }
      jsonFile.println();
      jsonFile.println("    ]");
    }
  }

  jsonFile.println("  },");
  flushLog();

  // ─── 9) Gallery ───
  jsonFile.println("  \"gallery\": [");
  File galleryDir = SD_MMC.open("/Gallery");
  bool firstGal = true;
  std::vector<String> mediaExts = { ".jpg", ".jpeg", ".png", ".bmp", ".gif", ".webp", ".tiff", ".mp4", ".webm", ".mov", ".avi", ".mkv", ".flv" };

  while (File f = galleryDir.openNextFile()) {
    yield();
    String fname = String(f.name());
    String ext2 = fname.substring(fname.lastIndexOf('.'));
    ext2.toLowerCase();
    if (!f.isDirectory() && isValidExtension(fname, mediaExts)) {
      if (!firstGal) jsonFile.println(",");
      firstGal = false;
      String base = getFileBaseName(fname);
      jsonFile.print("    { \"name\": \"" + base + "\", \"file\": \"Gallery/" + fname + "\" }");

      logBuf += "- " + fname + "\n";
      if (++logCount % LOG_UPDATE_INTERVAL == 0) flushLog();
    }
  }

  jsonFile.println();
  jsonFile.println("  ],");
  flushLog();

  // ─── 10) Files ───
  jsonFile.println("  \"files\": [");
  File filesDir = SD_MMC.open("/Files");
  bool firstFile = true;
  while (File f = filesDir.openNextFile()) {
    yield();
    String fname = String(f.name());
    if (!f.isDirectory()) {
      if (!firstFile) jsonFile.println(",");
      firstFile = false;
      jsonFile.print("    { \"name\": \"" + fname + "\", \"file\": \"Files/" + fname + "\" }");

      logBuf += "- " + fname + "\n";
      if (++logCount % LOG_UPDATE_INTERVAL == 0) flushLog();
    }
  }

  jsonFile.println();
  jsonFile.println("  ]");
  flushLog();

  // ─── 11) Close JSON and finish ───
  jsonFile.println("}");
  jsonFile.close();

  Serial.println("[MediaGen] media.json created successfully.");
  lv_obj_add_flag(ui_MediaGen, LV_OBJ_FLAG_HIDDEN);
}

String absURL(const String &path) {
  return "http://" + WiFi.softAPIP().toString() + path;
}

void handleOPDSRoot(AsyncWebServerRequest *request) {
  AsyncResponseStream *res = request->beginResponseStream(
    "application/atom+xml;profile=opds-catalog;kind=navigation");

  res->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  res->addHeader("Pragma", "no-cache");
  res->addHeader("Expires", "0");
  res->addHeader("Surrogate-Control", "no-store");

  opdsWrite(res, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                 "<feed xmlns=\"http://www.w3.org/2005/Atom\" "
                 "xmlns:opds=\"http://opds-spec.org/2010/catalog\">\n");

  opdsWrite(res, "  <id>urn:uuid:nomad-opds-root</id>\n"
                 "  <title>Nomad OPDS Catalog</title>\n"
                 "  <updated>2025-07-12T12:00:00Z</updated>\n"
                 "  <author><name>Nomad Server</name></author>\n");

  // Add required navigation links
  opdsWrite(res, "  <link rel=\"self\" href=\"" + absURL("/opds/root.xml") + "\" "
                                                                             "type=\"application/atom+xml;profile=opds-catalog;kind=navigation\"/>\n");
  opdsWrite(res, "  <link rel=\"start\" href=\"" + absURL("/opds/root.xml") + "\" "
                                                                              "type=\"application/atom+xml;profile=opds-catalog;kind=navigation\"/>\n");

  opdsWrite(res, "  <entry>\n"
                 "    <title>All Books</title>\n"
                 "    <id>urn:uuid:nomad-opds-books</id>\n"
                 "    <updated>2025-07-12T12:00:00Z</updated>\n"
                 "    <link rel=\"http://opds-spec.org/catalog\" "
                 "type=\"application/atom+xml;profile=opds-catalog;kind=acquisition\" "
                 "href=\""
                   + absURL("/opds/books.xml") + "\"/>\n"
                                                 "  </entry>\n");

  opdsWrite(res, "</feed>");
  request->send(res);
}

void handleOPDSBooks(AsyncWebServerRequest *request) {
  Serial.println("[OPDS] === handleOPDSBooks() called ===");
  AsyncResponseStream *res =
    request->beginResponseStream(
      "application/atom+xml;profile=opds-catalog;kind=acquisition");

  res->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  res->addHeader("Pragma", "no-cache");
  res->addHeader("Expires", "0");
  res->addHeader("Surrogate-Control", "no-store");

  opdsWrite(res, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                 "<feed xmlns=\"http://www.w3.org/2005/Atom\" "
                 "xmlns:opds=\"http://opds-spec.org/2010/catalog\">\n");
  opdsWrite(res,
            "  <id>urn:uuid:nomad-opds-books</id>\n"
            "  <title>All Books</title>\n"
            "  <updated>"
              + rfc3339Now() + "</updated>\n"
                               "  <link rel=\"self\"  href=\""
              + absURL("/opds/books.xml") + "\" "
                                            "type=\"application/atom+xml;profile=opds-catalog;kind=acquisition\"/>\n"
                                            "  <link rel=\"start\" href=\""
              + absURL("/opds/root.xml") + "\" "
                                           "type=\"application/atom+xml;profile=opds-catalog;kind=navigation\"/>\n");

  File dir = SD_MMC.open("/Books");
  Serial.println("[OPDS] Opened /Books directory");

  if (!dir || !dir.isDirectory()) {
    Serial.println("[OPDS] /Books directory not found or is not a directory!");
  }
  while (dir && dir.isDirectory()) {
    File file = dir.openNextFile();
    if (!file) {
      Serial.println("[OPDS] No more files in /Books");
      break;
    }

    Serial.println(String("[OPDS] Found file: ") + file.name());

    if (file.isDirectory()) {
      Serial.println("[OPDS] Skipping directory");
      continue;
    }

    String fn = file.name();
    String fnLower = fn;
    fnLower.toLowerCase();

    if (!(fnLower.endsWith(".epub") || fnLower.endsWith(".pdf"))) {
      Serial.println("[OPDS] Skipping non-ebook file: " + fn);
      continue;
    }

    Serial.println("[OPDS] Processing book file: " + fn);

    String base = fn.substring(fn.lastIndexOf('/') + 1);
    base = base.substring(0, base.lastIndexOf('.'));
    String safeTitle = xmlEscape(base);
    String safeId = "urn:uuid:nomad-book-" + slugify(base);
    String mime = fn.endsWith(".epub") ? "application/epub+zip" : "application/pdf";

    // Cover (falls back to placeholder)
    String coverPath = SD_MMC.exists("/Books/" + base + ".jpg") ? "Books/" + base + ".jpg" : "placeholder.jpg";

    // Compute real download path
    String ext = fnLower.endsWith(".pdf") ? ".pdf" : ".epub";
    String dlPath = "/Books/" + urlencode(base) + ext;

    opdsWrite(res, "  <entry>\n"
                   "    <title>"
                     + safeTitle + "</title>\n"
                                   "    <id>"
                     + safeId + "</id>\n"
                                "    <updated>"
                     + rfc3339Now() + "</updated>\n"
                                      "    <link rel=\"http://opds-spec.org/image/thumbnail\" "
                                      "type=\"image/jpeg\" href=\""
                     + absURL("/" + urlencode(coverPath)) + "\"/>\n"
                                                            "    <link rel=\"http://opds-spec.org/acquisition\" "
                                                            "type=\""
                     + mime + "\" "
                              "href=\""
                     + absURL(dlPath) + "\"/>\n"
                                        "  </entry>\n");
  }
  if (dir) {
    dir.close();
    Serial.println("[OPDS] Closed /Books directory");
  }

  opdsWrite(res, "</feed>");
  request->send(res);
}

// ==================== MEDIA STREAM HANDLER ====================
// Handles video/audio streaming via range requests
void handleRangeRequest(AsyncWebServerRequest *request) {

  /* ----- validate path ----- */
  if (!request->hasParam("file")) {
    request->send(400, "text/plain", "File parameter missing");
    return;
  }

  String filePath = request->getParam("file")->value();
  if (!SD_MMC.exists(filePath)) {
    request->send(404, "text/plain", "File not found");
    return;
  }

  /* ----- open file with recovery guard ----- */
  File file = SD_MMC.open(filePath, "r");
  if (!file) {  // ✱✱ NEW / RECOVERY ✱✱
    Serial.println("[SD] open() failed — trigger recovery");
    sdErrorFlag = true;                      // flag main loop
    sdErrorCooldownUntil = millis() + 5000;  // pause 5 s
    request->send(503, "text/plain",
                  "SD error — retrying shortly");  // browser will auto‑retry
    return;
  }

  size_t fileSize = file.size();

  /* ----- debug ----- */
  Serial.println("=== Range Request Debug ===");
  Serial.println("File requested: " + filePath);
  Serial.println("File size: " + String(fileSize));

  /* ----- HEAD support ----- */
  if (request->method() == HTTP_HEAD) {
    Serial.println("HEAD request received");
    AsyncWebServerResponse *headResponse =
      request->beginResponse(200, "application/octet-stream", "");
    headResponse->addHeader("Content-Length", String(fileSize));
    request->send(headResponse);
    file.close();
    return;
  }

  /* ----- parse Range header ----- */
  String rangeHeader = request->header("Range");
  Serial.println("Raw Range header: " + rangeHeader);

  size_t startByte = 0, endByte = fileSize - 1;
  if (rangeHeader.startsWith("bytes=")) {
    int dashIndex = rangeHeader.indexOf('-');
    startByte = rangeHeader.substring(6, dashIndex).toInt();
    if (dashIndex + 1 < rangeHeader.length()) {
      endByte = rangeHeader.substring(dashIndex + 1).toInt();
    }
  }
  if (endByte >= fileSize) endByte = fileSize - 1;
  size_t contentLength = endByte - startByte + 1;

  Serial.println("Computed startByte: " + String(startByte));
  Serial.println("Computed endByte: " + String(endByte));
  Serial.println("Content length: " + String(contentLength));
  Serial.println("============================");

  /* ----- create async response ----- */
  String mimeType = "application/octet-stream";  // fallback

  if (filePath.endsWith(".epub")) mimeType = "application/epub+zip";
  else if (filePath.endsWith(".pdf")) mimeType = "application/pdf";
  else if (filePath.endsWith(".mp3")) mimeType = "audio/mpeg";
  else if (filePath.endsWith(".flac")) mimeType = "audio/flac";
  else if (filePath.endsWith(".wav")) mimeType = "audio/wav";
  else if (filePath.endsWith(".ogg")) mimeType = "audio/ogg";
  else if (filePath.endsWith(".aac")) mimeType = "audio/aac";
  else if (filePath.endsWith(".m4a")) mimeType = "audio/mp4";  // or audio/x-m4a
  else if (filePath.endsWith(".mp4")) mimeType = "video/mp4";
  else if (filePath.endsWith(".webm")) mimeType = "video/webm";
  else if (filePath.endsWith(".m4v")) mimeType = "video/x-m4v";
  else if (filePath.endsWith(".jpg") || filePath.endsWith(".jpeg")) mimeType = "image/jpeg";
  else if (filePath.endsWith(".png")) mimeType = "image/png";
  else if (filePath.endsWith(".cbz")) mimeType = "application/vnd.comicbook+zip";
  else if (filePath.endsWith(".cbr")) mimeType = "application/vnd.comicbook-rar";
  AsyncWebServerResponse *response = request->beginResponse(
    mimeType,       // content type
    contentLength,  // content length
    [&, file, startByte, endByte, contentLength](uint8_t *buffer, size_t maxLen, size_t index) mutable -> size_t {
      /* seek & read */
      file.seek(startByte + index);
      size_t bytesToRead = min(maxLen, endByte - (startByte + index) + 1);
      size_t bytesRead = file.read(buffer, bytesToRead);

      /* ----- detect read failure ----- */
      if (bytesRead == 0) {
        Serial.println("[SD] read() failed — recovery");
        file.close();
        sdErrorFlag = true;
        sdErrorCooldownUntil = millis() + 5000;
        return 0;  // aborts this chunk; client retries
      }

      /* close when finished */
      if (index + bytesRead >= contentLength) {
        file.close();
      }
      return bytesRead;
    });

  response->addHeader("Accept-Ranges", "bytes");
  response->addHeader("Content-Range", "bytes " + String(startByte) + "-" + String(endByte) + "/" + String(fileSize));
  // strict no-cache for streaming
  response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "0");
  response->addHeader("Surrogate-Control", "no-store");
  response->setCode(206);  // Partial Content
  request->send(response);
}
void handleListFiles(AsyncWebServerRequest *request) {
  if (!request->hasParam("dir")) {
    request->send(400, "application/json", "{\"error\":\"Missing 'dir' parameter\"}");
    return;
  }
  String dir = request->getParam("dir")->value();

  if (!SD_MMC.exists(dir)) {
    request->send(404, "application/json", "{\"error\":\"Directory not found\"}");
    return;
  }

  File directory = SD_MMC.open(dir);
  if (!directory || !directory.isDirectory()) {
    request->send(404, "application/json", "{\"error\":\"Not a directory\"}");
    return;
  }

  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();

  File file = directory.openNextFile();
  while (file) {
    JsonObject f = arr.createNestedObject();

    String filename = String(file.name());
    // Strip dir prefix
    if (filename.startsWith(dir)) {
      filename = filename.substring(dir.length());
      if (filename.startsWith("/")) filename = filename.substring(1);
    }

    if (file.isDirectory()) {
      // Append slash to indicate directory
      f["name"] = filename + "/";
      f["size"] = 0;
      f["isDir"] = true;
    } else {
      f["name"] = filename;
      f["size"] = file.size();
      f["isDir"] = false;
    }
    file = directory.openNextFile();
  }
  directory.close();

  String response;
  serializeJson(arr, response);
  request->send(200, "application/json", response);
}

void handleFileUpload(AsyncWebServerRequest *request) {
  if (!request->hasParam("dir", true)) {
    request->send(400, "application/json", "{\"error\":\"Missing 'dir' form field\"}");
    return;
  }
  String dir = request->getParam("dir", true)->value();

  if (!SD_MMC.exists(dir) || !SD_MMC.open(dir).isDirectory()) {
    request->send(404, "application/json", "{\"error\":\"Directory not found\"}");
    return;
  }

  request->send(200, "application/json", "{\"status\":\"Ready to upload\"}");
}
void handleRename(AsyncWebServerRequest *request) {
  if (!request->hasParam("oldname", true) || !request->hasParam("newname", true)) {
    request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
    return;
  }

  String oldName = request->getParam("oldname", true)->value();
  String newName = request->getParam("newname", true)->value();

  if (!SD_MMC.exists(oldName)) {
    request->send(404, "application/json", "{\"error\":\"Original file not found\"}");
    return;
  }

  if (SD_MMC.exists(newName)) {
    request->send(409, "application/json", "{\"error\":\"Target file already exists\"}");
    return;
  }

  if (SD_MMC.rename(oldName, newName)) {
    request->send(200, "application/json", "{\"status\":\"Rename successful\"}");
  } else {
    request->send(500, "application/json", "{\"error\":\"Rename failed\"}");
  }
}
void handleDelete(AsyncWebServerRequest *request) {
  if (!request->hasParam("filename", true)) {
    request->send(400, "application/json", "{\"error\":\"Missing 'filename' parameter\"}");
    return;
  }

  String filename = request->getParam("filename", true)->value();

  if (!SD_MMC.exists(filename)) {
    request->send(404, "application/json", "{\"error\":\"File not found\"}");
    return;
  }

  bool success = false;
  File f = SD_MMC.open(filename);
  if (f && f.isDirectory()) {
    success = SD_MMC.rmdir(filename);
  } else {
    success = SD_MMC.remove(filename);
  }

  if (success) {
    request->send(200, "application/json", "{\"status\":\"Delete successful\"}");
  } else {
    request->send(500, "application/json", "{\"error\":\"Delete failed\"}");
  }
}

std::map<AsyncWebServerRequest *, String> uploadPaths;
File uploadFile;

void onUploadHandler(AsyncWebServerRequest *request, const String &filename, size_t index,
                     uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    String dir = "/";
    if (request->hasParam("dir", true)) {
      dir = request->getParam("dir", true)->value();
    }
    if (!dir.startsWith("/")) dir = "/" + dir;
    if (dir.endsWith("/")) dir.remove(dir.length() - 1);

    String fullPath = dir + "/" + filename;

    if (SD_MMC.exists(fullPath)) {
      Serial.println("[Upload] Duplicate file blocked: " + fullPath);
      request->send(409, "application/json", "{\"error\":\"File already exists\"}");
      return;
    }

    int slashPos = fullPath.lastIndexOf('/');
    if (slashPos != -1) {
      String parent = fullPath.substring(0, slashPos);
      SD_MMC.mkdir(parent);
    }

    Serial.println("[Upload] Starting upload to: " + fullPath);
    uploadFile = SD_MMC.open(fullPath, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("[Upload] Failed to open: " + fullPath);
      return;
    }
    uploadPaths[request] = fullPath;
  }

  if (uploadFile) {
    uploadFile.write(data, len);
  }

  if (final) {
    Serial.println("[Upload] Complete: " + uploadPaths[request]);
    uploadFile.close();
    uploadPaths.erase(request);
    request->send(200, "application/json", "{\"status\":\"Upload complete\"}");
  }
}

void createSimpleUploadHandler(const String &mediaFolder, const char *endpoint) {
  server.on(
    endpoint, HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(200, "application/json", "{\"status\":\"Upload finished\"}");
    },
    [mediaFolder](AsyncWebServerRequest *request, const String &filename, size_t index,
                  uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        String fullPath = "/" + mediaFolder + "/" + filename;
        Serial.println("[Upload] Starting upload to: " + fullPath);
        File f = SD_MMC.open(fullPath, FILE_WRITE);
        if (!f) {
          Serial.println("[Upload] Failed to open file for writing");
          return;
        }
        activeUploads[request] = f;
      }

      if (activeUploads.count(request)) {
        activeUploads[request].write(data, len);
        Serial.printf("[Upload] Written %u bytes to %s\n", len, filename.c_str());
      }

      if (final && activeUploads.count(request)) {
        Serial.println("[Upload] Upload complete for: " + filename);
        activeUploads[request].close();
        activeUploads.erase(request);
      }
    });
}
void scanSDCardUsage() {
  cachedUsedBytes = 0;
  cachedTotalBytes = SD_MMC.cardSize();

  std::function<void(File)> sumDirectory = [&](File dir) {
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;

      if (entry.isDirectory()) {
        sumDirectory(entry);
      } else {
        cachedUsedBytes += entry.size();
      }
      entry.close();
    }
  };

  File root = SD_MMC.open("/");
  if (root && root.isDirectory()) {
    sumDirectory(root);
    root.close();
  }

  lastScanTime = millis();
}

void handleSDInfo(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(256);

  // Example: Get total and used bytes from SD_MMC or cached values
  uint64_t totalBytes = cachedTotalBytes > 0 ? cachedTotalBytes : (64ULL * 1024 * 1024 * 1024);
  uint64_t usedBytes = cachedUsedBytes;  // You should update this periodically

  doc["total"] = totalBytes;
  doc["used"] = usedBytes;

  String output;
  serializeJson(doc, output);

  request->send(200, "application/json", output);
}

void updateSDBAR() {
  uint64_t totalBytes = SD_MMC.totalBytes();
  uint64_t usedBytes = SD_MMC.usedBytes();

  if (totalBytes > 0) {
    int usage = (usedBytes * 100) / totalBytes;
    lv_bar_set_value(ui_sdbar, usage, LV_ANIM_OFF);
  } else {
    lv_bar_set_value(ui_sdbar, 0, LV_ANIM_OFF);
  }
}

bool checkGenerateFlagFile() {
  if (SD_MMC.exists("/.generate_flag")) {
    Serial.println("[BOOT] Found /.generate_flag, will generate media.json");
    SD_MMC.remove("/.generate_flag");
    return true;
  }
  return false;
}

void handleConnector(AsyncWebServerRequest *request) {
  // 1) Get 'dir' from POST body
  String dir = "/";
  if (request->hasParam("dir", true)) {
    dir = request->getParam("dir", true)->value();
  }
  if (!dir.endsWith("/")) dir += "/";

  // 2) Open the directory
  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) {
    request->send(400, "text/plain", "Invalid directory");
    return;
  }

  // 3) Build the HTML <ul> tree
  String html = "<ul class=\"jqueryFileTree\" style=\"display: none;\">";
  root.rewindDirectory();
  File entry;
  while ((entry = root.openNextFile())) {
    String name = entry.name();
    if (entry.isDirectory()) {
      html += "<li class=\"directory collapsed\">"
              "<a href=\"#\" rel=\""
              + dir + name + "/\">" + name + "</a>"
                                             "</li>";
    } else {
      int dot = name.lastIndexOf('.');
      String ext = dot > 0 ? name.substring(dot + 1) : "";
      html += "<li class=\"file ext_" + ext + "\">"
                                              "<a href=\"#\" rel=\""
              + dir + name + "\">" + name + "</a>"
                                            "</li>";
    }
    entry.close();
  }
  html += "</ul>";

  // 4) Respond
  request->send(200, "text/html", html);
}
void handleMkdir(AsyncWebServerRequest *request) {
  if (!request->hasParam("dir", true)) {
    request->send(400, "application/json", "{\"error\":\"Missing 'dir' parameter\"}");
    return;
  }

  String dirPath = request->getParam("dir", true)->value();

  if (dirPath == "/" || dirPath == "") {
    request->send(400, "application/json", "{\"error\":\"Invalid directory path\"}");
    return;
  }

  // Normalize path
  if (!dirPath.startsWith("/")) dirPath = "/" + dirPath;

  if (SD_MMC.exists(dirPath)) {
    request->send(400, "application/json", "{\"error\":\"Directory already exists\"}");
    return;
  }

  if (SD_MMC.mkdir(dirPath)) {
    request->send(200, "application/json", "{\"success\":\"Directory created\"}");
  } else {
    request->send(500, "application/json", "{\"error\":\"Failed to create directory\"}");
  }
}
void applyRGBSettings() {
  if (settings.rgbMode == "off") {
    RGB_SetMode(0);  // Off
  } else if (settings.rgbMode == "solid") {
    if (settings.rgbColor.length() == 7 && settings.rgbColor.charAt(0) == '#') {
      // Parse "#RRGGBB"
      char rs[3] = { settings.rgbColor.charAt(1), settings.rgbColor.charAt(2), 0 };
      char gs[3] = { settings.rgbColor.charAt(3), settings.rgbColor.charAt(4), 0 };
      char bs[3] = { settings.rgbColor.charAt(5), settings.rgbColor.charAt(6), 0 };

      solidR = strtol(rs, NULL, 16);
      solidG = strtol(gs, NULL, 16);
      solidB = strtol(bs, NULL, 16);

      RGB_SetMode(2);  // Solid color mode
    } else {
      Serial.println("[RGB] Invalid color format in settings.rgbColor");
    }
  } else if (settings.rgbMode == "rainbow") {
    RGB_SetMode(1);  // Rainbow mode
  }
}
void applyWiFiSettings() {
  Serial.print("Starting WiFi with SSID: ");
  Serial.println(settings.wifiSSID);
  WiFi.softAP(settings.wifiSSID.c_str(), settings.wifiPassword.c_str());
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
}
// Return number of connected stations on the softAP
int getConnectedUserCount() {
  // WiFi.softAPgetStationNum() returns uint8_t number of clients
  return WiFi.softAPgetStationNum();
}
void sdScanTask(void *pvParameters) {
  scanSDCardUsage();
  lv_timer_handler();
  updateSDBAR();
  vTaskDelete(NULL);
}

// ------------- Main Setup -------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("=== Booting Nomad (debug) ===");
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  if (get_boot_mode() == USB_MODE) {
    clear_boot_mode();  // next boot will go back to MEDIA
    delay(500);
    Serial.println(">>> USB mode: mounting SD & starting MSC");
    LCD_Init();
    Lvgl_Init();
    ui_init();
    btStop();  //Stops bluetooth (dont need)
    lv_scr_load(ui_Screen1);
    lv_obj_clear_flag(ui_MediaGen, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(ui_MediaGen, "USB Mass-Storage Mode");
    lv_timer_handler();

    launch_usb_mode();
    return;
  }

  LCD_Init();
  Lvgl_Init();
  ui_init();

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-S3 Captive Portal & SDMMC Server ===");

  // Start WiFi Access Point
  WiFi.softAP(settings.wifiSSID.c_str(), settings.wifiPassword.c_str());
  Serial.println("WiFi AP started...");


  // Initialize SD card
  Serial.println("Initializing SD Card...");
  if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN)) {
    Serial.println("ERROR: SDMMC Pin configuration failed!");
    return;
  }

  if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 12)) {
    Serial.println("ERROR: SDMMC Card initialization failed.");
    return;
  }

  Serial.println("SD Card initialized successfully!");
  Serial.println("Loading Settings...");
  loadSettings();
  Serial.printf("[SETTINGS] autoGenerateMedia = %s\n", settings.autoGenerateMedia ? "true" : "false");
  applyWiFiSettings();
  applyRGBSettings();
  lv_label_set_text(ui_ssidlabel, settings.wifiSSID.c_str());
  Serial.print("settings.brightness = ");
  Serial.println(settings.brightness);
  bool generateOnce = SD_MMC.exists("/generate_once.flag");
  bool shouldGenerate = (settings.autoGenerateMedia || generateOnce);

  // Log state
  Serial.print("[BOOT] autoGenerateMedia (from settings) = ");
  Serial.println(settings.autoGenerateMedia ? "true" : "false");
  Serial.print("[BOOT] generate_once.flag = ");
  Serial.println(generateOnce ? "true" : "false");

  // Remove /boot_done.flag if cold boot or one-time requested
  esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_SW || generateOnce) {
    SD_MMC.remove("/boot_done.flag");
  }

  // Generate if needed
  if (shouldGenerate && !SD_MMC.exists("/boot_done.flag")) {
    Serial.println("[BOOT] Media generation required. Generating media.json...");
    generateMediaJSON();

    File flagFile = SD_MMC.open("/boot_done.flag", FILE_WRITE);
    if (flagFile) {
      flagFile.println("done");
      flagFile.close();
    }
    Serial.println("[BOOT] media.json generated. Boot will continue.");

    if (generateOnce) {
      clearOneTimeGenerate();
      Serial.println("[BOOT] Cleared one-time generation trigger.");
    }
  } else {
    Serial.println("[BOOT] Skipping media.json generation.");
  }

  Set_Backlight(settings.brightness);  // now using loaded value
  updateSDBAR();
  xTaskCreatePinnedToCore(
    sdScanTask,  // function
    "SDScan",    // name
    8 * 1024,    // stack size
    NULL,        // params
    1,           // priority (1 = low)
    NULL,        // handle (not needed)
    0            // run on core 0
  );

  createSimpleUploadHandler("Movies", "/upload-movie");
  createSimpleUploadHandler("Music", "/upload-music");
  createSimpleUploadHandler("Books", "/upload-book");

  delay(2000);

  attachInterrupt(
    BOOT_BUTTON_PIN, []() {
      set_boot_mode(USB_MODE);
      ESP.restart();
    },
    FALLING);
  // Start Captive DNS redirection
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  //OPDS Endpoint
  server.on("/opds/root.xml", HTTP_GET, handleOPDSRoot);
  server.on("/opds/books.xml", HTTP_GET, handleOPDSBooks);
  //.m3u playlist endpoint
  server.on("/playlist.m3u", HTTP_GET, [](AsyncWebServerRequest *request) {
    String playlist = "#EXTM3U\n";
    server.on("/nomad.m3u", HTTP_GET, [](AsyncWebServerRequest *request) {
      // internally call the same code or just redirect
      request->redirect("/playlist.m3u");
    });

    // === MOVIES ===
    playlist += "# === MOVIES ===\n";
    File movieDir = SD_MMC.open("/Movies");
    if (movieDir && movieDir.isDirectory()) {
      File file = movieDir.openNextFile();
      while (file) {
        String name = file.name();
        if (!file.isDirectory() && (name.endsWith(".mp4") || name.endsWith(".mkv"))) {
          String fullPath = String("/Movies/") + file.name();
          playlist += "#EXTINF:-1," + String(file.name()) + "\n";
          playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
        }
        file = movieDir.openNextFile();
      }
    }

    // === SHOWS ===
    playlist += "# === SHOWS ===\n";
    File showsRoot = SD_MMC.open("/Shows");
    if (showsRoot && showsRoot.isDirectory()) {
      File showFolder = showsRoot.openNextFile();
      while (showFolder) {
        if (showFolder.isDirectory()) {
          String showFolderName = String(showFolder.name());  // e.g. "GravityFalls"
          String fullShowPath = "/Shows/" + showFolderName;

          File episodeDir = SD_MMC.open(fullShowPath);
          if (episodeDir && episodeDir.isDirectory()) {
            File ep = episodeDir.openNextFile();
            while (ep) {
              String epName = ep.name();
              if (!ep.isDirectory() && (epName.endsWith(".mp4") || epName.endsWith(".mkv"))) {
                String fullPath = fullShowPath + "/" + epName;
                playlist += "#EXTINF:-1," + epName + "\n";
                playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
              }
              ep = episodeDir.openNextFile();
            }
          }
        }
        showFolder = showsRoot.openNextFile();
      }
    }

    //.m3u playlist endpoint
    server.on("/playlist.m3u", HTTP_GET, [](AsyncWebServerRequest *request) {
      String playlist = "#EXTM3U\n";
      server.on("/nomad.m3u", HTTP_GET, [](AsyncWebServerRequest *request) {
        // internally call the same code or just redirect
        request->redirect("/playlist.m3u");
      });

      // === MOVIES ===
      playlist += "# === MOVIES ===\n";
      File movieDir = SD_MMC.open("/Movies");
      if (movieDir && movieDir.isDirectory()) {
        File file = movieDir.openNextFile();
        while (file) {
          String name = file.name();
          if (!file.isDirectory() && (name.endsWith(".mp4") || name.endsWith(".mkv"))) {
            String fullPath = String("/Movies/") + file.name();
            playlist += "#EXTINF:-1," + String(file.name()) + "\n";
            playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
          }
          file = movieDir.openNextFile();
        }
      }

      // === SHOWS ===
      playlist += "# === SHOWS ===\n";
      File showsRoot = SD_MMC.open("/Shows");
      if (showsRoot && showsRoot.isDirectory()) {
        File showFolder = showsRoot.openNextFile();
        while (showFolder) {
          if (showFolder.isDirectory()) {
            String showFolderName = String(showFolder.name());  // e.g. "GravityFalls"
            String fullShowPath = "/Shows/" + showFolderName;

            File episodeDir = SD_MMC.open(fullShowPath);
            if (episodeDir && episodeDir.isDirectory()) {
              File ep = episodeDir.openNextFile();
              while (ep) {
                String epName = ep.name();
                if (!ep.isDirectory() && (epName.endsWith(".mp4") || epName.endsWith(".mkv"))) {
                  String fullPath = fullShowPath + "/" + epName;
                  playlist += "#EXTINF:-1," + epName + "\n";
                  playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
                }
                ep = episodeDir.openNextFile();
              }
            }
          }
          showFolder = showsRoot.openNextFile();
        }
      }

      // === MUSIC ===
      playlist += "# === MUSIC ===\n";
      File musicDir = SD_MMC.open("/Music");
      if (musicDir && musicDir.isDirectory()) {
        File file = musicDir.openNextFile();
        while (file) {
          String name = file.name();
          if (!file.isDirectory() && name.endsWith(".mp3")) {
            String fullPath = String("/Music/") + file.name();
            playlist += "#EXTINF:-1," + String(file.name()) + "\n";
            playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
          }
          file = musicDir.openNextFile();
        }
      }

      AsyncWebServerResponse *r = request->beginResponse(200, "audio/x-mpegurl", playlist);
      r->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
      r->addHeader("Pragma", "no-cache");
      r->addHeader("Expires", "0");
      r->addHeader("Surrogate-Control", "no-store");
      request->send(r);
    });

    //fAKE dlna dISCOVERY
    server.on("/dlna/device.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media Server</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>Nomad DLNA</modelName>
            <UDN>uuid:ESP32-DLNA-NOMAD</UDN>
          </device>
        </root>
      )rawliteral");
    });

    server.on("/ssdp/device-desc.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media Server</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>Nomad</modelName>
            <modelNumber>1</modelNumber>
            <UDN>uuid:ESP32-DLNA-FAKE-1234</UDN>
          </device>
        </root>
      )rawliteral");
    });

    server.on("/dlna/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media</friendlyName>
            <manufacturer>JCorp</manufacturer>
            <modelName>ESP32-Nomad</modelName>
            <UDN>uuid:nomad-dlna-esp32</UDN>
          </device>
        </root>
      )rawliteral");
    });

    server.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
      AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
      response->addHeader("Application-URL", "http://" + WiFi.softAPIP().toString() + "/dlna/");
      response->addHeader("Location", "/dlna/desc.xml");  // HTTP redirect target
      request->send(response);
    });

    // === MUSIC ===
    playlist += "# === MUSIC ===\n";
    File musicDir = SD_MMC.open("/Music");
    if (musicDir && musicDir.isDirectory()) {
      File file = musicDir.openNextFile();
      while (file) {
        String name = file.name();
        if (!file.isDirectory() && name.endsWith(".mp3")) {
          String fullPath = String("/Music/") + file.name();
          playlist += "#EXTINF:-1," + String(file.name()) + "\n";
          playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
        }
        file = musicDir.openNextFile();
      }
    }

    AsyncWebServerResponse *r = request->beginResponse(200, "audio/x-mpegurl", playlist);
    r->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    r->addHeader("Pragma", "no-cache");
    r->addHeader("Expires", "0");
    r->addHeader("Surrogate-Control", "no-store");
    request->send(r);
  });

  //fAKE dlna dISCOVERY
  server.on("/dlna/device.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media Server</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>Nomad DLNA</modelName>
            <UDN>uuid:ESP32-DLNA-NOMAD</UDN>
          </device>
        </root>
      )rawliteral");
  });

  server.on("/ssdp/device-desc.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media Server</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>Nomad</modelName>
            <modelNumber>1</modelNumber>
            <UDN>uuid:ESP32-DLNA-FAKE-1234</UDN>
          </device>
        </root>
      )rawliteral");
  });

  server.on("/dlna/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>ESP32-Nomad</modelName>
            <UDN>uuid:nomad-dlna-esp32</UDN>
          </device>
        </root>
      )rawliteral");
  });

  server.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
    response->addHeader("Application-URL", "http://" + WiFi.softAPIP().toString() + "/dlna/");
    response->addHeader("Location", "/dlna/desc.xml");  // HTTP redirect target
    request->send(response);
  });

  // Set LED mode: solid (0), rainbow (1), etc.
  server.on(
    "/led/onoff", HTTP_POST, [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      StaticJsonDocument<64> doc;
      DeserializationError err = deserializeJson(doc, data);
      if (err) {
        request->send(400, "text/plain", "Invalid JSON");
        return;
      }
      bool enabled = doc["enabled"];
      RGB_SetMode(enabled ? 1 : 0);
      request->send(200, "text/plain", "LED toggled");
    });

  // /led/rainbow - Start rainbow loop
  server.on("/led/rainbow", HTTP_POST, [](AsyncWebServerRequest *request) {
    RGB_SetMode(1);  // Rainbow loop
    request->send(200, "text/plain", "Rainbow mode activated");
  });

  // /led/color - Set solid color from JSON { color: "#rrggbb" }
  server.on(
    "/led/color", HTTP_POST, [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      StaticJsonDocument<64> doc;
      DeserializationError err = deserializeJson(doc, data);
      if (err) {
        request->send(400, "text/plain", "Invalid JSON");
        return;
      }

      const char *hex = doc["color"];
      if (!hex || strlen(hex) != 7 || hex[0] != '#') {
        request->send(400, "text/plain", "Invalid color format");
        return;
      }
      // Parse "#rrggbb"
      char rs[3] = { hex[1], hex[2], 0 };
      char gs[3] = { hex[3], hex[4], 0 };
      char bs[3] = { hex[5], hex[6], 0 };

      uint8_t r = strtol(rs, NULL, 16);
      uint8_t g = strtol(gs, NULL, 16);
      uint8_t b = strtol(bs, NULL, 16);
      RGB_SetColor(r, g, b);
      request->send(200, "text/plain", "Color set");
    });

  server.on("/sdinfo", HTTP_GET, handleSDInfo);
  server.on("/generate-media", HTTP_POST, [](AsyncWebServerRequest *request) {
    Serial.println("[ADMIN] /generate-media called");

    if (!SD_MMC.begin()) {
      request->send(500, "text/plain", "SD card not available.");
      Serial.println("[ADMIN] SD card not mounted.");
      return;
    }

    bool generateOnceFlagExists = SD_MMC.exists("/generate_once.flag");

    if (settings.autoGenerateMedia) {
      if (!generateOnceFlagExists) {
        // Always-generate mode: allow one extra reboot-based generation
        File f = SD_MMC.open("/generate_once.flag", FILE_WRITE);
        if (f) {
          f.print("1");
          f.close();
          Serial.println("[ADMIN] Created /generate_once.flag (settings.autoGenerateMedia ON)");
        } else {
          Serial.println("[ADMIN] Failed to create /generate_once.flag");
        }
      } else {
        Serial.println("[ADMIN] One-time flag already exists. No action taken.");
      }
    } else {
      // Auto-generation disabled: allow one-time override
      File f = SD_MMC.open("/generate_once.flag", FILE_WRITE);
      if (f) {
        f.print("1");
        f.close();
        Serial.println("[ADMIN] Created /generate_once.flag (settings.autoGenerateMedia OFF)");
      } else {
        Serial.println("[ADMIN] Failed to create /generate_once.flag");
      }
    }

    request->send(200, "text/plain", "Flag set. Rebooting to generate media...");
    delay(200);  // Let SD write settle
    ESP.restart();
  });

  // Captive Portal: Redirect all unknown requests
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->hasHeader("User-Agent")) {
      String userAgent = request->header("User-Agent");
      Serial.println("User-Agent: " + userAgent);

      if (userAgent.indexOf("iPhone") >= 0 || userAgent.indexOf("iPad") >= 0 || userAgent.indexOf("Macintosh") >= 0) {
        Serial.println("Apple device detected. Serving appleindex.html");
        request->send(SD_MMC, "/appleindex.html", "text/html");
        return;
      }
    }
    Serial.println("Android device. Serving index.html");
    request->send(SD_MMC, "/index.html", "text/html");
  });

  // Captive triggers for Apple & Android devices
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Apple captive portal request detected, serving appleindex.html");
    request->send(SD_MMC, "/appleindex.html", "text/html");
  });

  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Android/NORMAL captive portal request detected, serving index.html");
    request->send(SD_MMC, "/index.html", "text/html");
  });

  server.on("/dlna/desc.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media Server</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>Nomad</modelName>
            <UDN>uuid:ESP32-DLNA-FAKE-1234</UDN>
          </device>
        </root>
      )rawliteral");
  });

  server.on("/dlna/contentdir.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    String xml = "<?xml version=\"1.0\"?><ContentDirectory>";
    File root = SD_MMC.open("/Movies");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        if (!file.isDirectory()) {
          String name = file.name();
          xml += "<item>";
          xml += "<title>" + name + "</title>";
          xml += "<res protocolInfo=\"http-get:*:video/mp4:*\">";
          xml += "http://192.168.4.1/Movies/" + name + "</res>";
          xml += "</item>";
        }
        file = root.openNextFile();
      }
    }
    xml += "</ContentDirectory>";
    request->send(200, "text/xml", xml);
  });

  server.on("/listfiles", HTTP_GET, handleListFiles);
  server.serveStatic("/assets", SD_MMC, "/assets").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/assets/", SD_MMC, "/assets/").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  // Static HTML routes
  server.serveStatic("/movies.html", SD_MMC, "/movies.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/music.html", SD_MMC, "/music.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/books.html", SD_MMC, "/books.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/shows.html", SD_MMC, "/shows.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/admin.html", SD_MMC, "/admin.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/games.html", SD_MMC, "/games.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/maps.html", SD_MMC, "/maps.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/menu.html", SD_MMC, "/menu.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/movies", SD_MMC, "/movies.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/music", SD_MMC, "/music.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/books", SD_MMC, "/books.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/shows", SD_MMC, "/shows.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/admin", SD_MMC, "/admin.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/games", SD_MMC, "/games.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/maps", SD_MMC, "/maps.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/menu", SD_MMC, "/menu.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/gallery", SD_MMC, "/gallery.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/files", SD_MMC, "/files.html").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");

  // Serve root directory and default to index.html
  server.serveStatic("/", SD_MMC, "/")
    .setDefaultFile("index.html")
    .setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/Gallery", SD_MMC, "/Gallery")
    .setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/Files", SD_MMC, "/Files")
    .setCacheControl("no-store, no-cache, must-revalidate, max-age=0");

  server.on(
    "/upload", HTTP_POST,
    // Final response when upload is complete
    [](AsyncWebServerRequest *request) {
      // Response is handled during final chunk or error
    },
    // Upload handler
    [](AsyncWebServerRequest *request, const String &filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      static std::map<AsyncWebServerRequest *, File> uploads;

      // Begin upload
      if (index == 0) {
        String dir = "/";
        if (request->hasParam("dir", true)) {
          dir = request->getParam("dir", true)->value();
        }

        if (!dir.startsWith("/")) dir = "/" + dir;
        if (dir.endsWith("/")) dir.remove(dir.length() - 1);

        String fullPath = dir + "/" + filename;

        // Check for duplicate
        if (SD_MMC.exists(fullPath)) {
          Serial.println("[Upload] Duplicate file detected: " + fullPath);
          request->send(409, "application/json", "{\"error\":\"File already exists\"}");
          return;
        }

        // Ensure directory exists
        int slashPos = fullPath.lastIndexOf('/');
        if (slashPos != -1) {
          String folder = fullPath.substring(0, slashPos);
          if (!SD_MMC.exists(folder)) {
            SD_MMC.mkdir(folder);
          }
        }

        File f = SD_MMC.open(fullPath, FILE_WRITE);
        if (!f) {
          Serial.println("[Upload] Failed to open file: " + fullPath);
          request->send(500, "application/json", "{\"error\":\"Failed to open file\"}");
          return;
        }

        uploads[request] = f;
        Serial.println("[Upload] Started: " + fullPath);
      }

      // Continue writing data
      if (uploads.count(request)) {
        uploads[request].write(data, len);
      }

      // Finalize upload
      if (final && uploads.count(request)) {
        uploads[request].close();
        uploads.erase(request);
        Serial.println("[Upload] Finished");
        request->send(200, "application/json", "{\"status\":\"Upload successful\"}");
      }
    });

  server.on("/list-assets", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("dir")) {
      request->send(400, "application/json", "{\"error\":\"Missing dir\"}");
      return;
    }

    String dir = request->getParam("dir")->value();
    File d = SD_MMC.open(dir);
    if (!d || !d.isDirectory()) {
      request->send(404, "application/json", "{\"error\":\"Invalid dir\"}");
      return;
    }

    String output = "{\"files\":[";
    bool first = true;
    File f;
    while ((f = d.openNextFile())) {
      if (!first) output += ",";
      output += "\"" + String(f.name()).substring(String(dir).length()) + "\"";
      first = false;
      f.close();
    }
    output += "]}";
    request->send(200, "application/json", output);
  });

  server.on("/mkdir", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Require POST parameter "dirname"
    if (!request->hasParam("dirname", true)) {
      request->send(400, "text/plain", "Missing 'dirname' parameter");
      return;
    }

    // Get and sanitize input
    String dirName = request->getParam("dirname", true)->value();

    // Ensure it starts with a slash (absolute path)
    if (!dirName.startsWith("/")) {
      dirName = "/" + dirName;
    }

    // Prevent directory traversal (no "../")
    if (dirName.indexOf("..") >= 0) {
      request->send(400, "text/plain", "Invalid directory name");
      return;
    }

    // Remove trailing slash, if present
    if (dirName.endsWith("/")) {
      dirName.remove(dirName.length() - 1);
    }

    // Check if the path already exists
    if (SD_MMC.exists(dirName)) {
      request->send(409, "text/plain", "Directory already exists");
      return;
    }

    // Attempt to create the directory
    if (SD_MMC.mkdir(dirName)) {
      request->send(200, "text/plain", "OK");
    } else {
      request->send(500, "text/plain", "Failed to create directory");
    }
  });

  static std::map<AsyncWebServerRequest *, String> uploadPaths;
  server.on("/media", HTTP_GET, handleRangeRequest);  // THE MOST IMPORTANT ONE
  server.on("/rename", HTTP_POST, handleRename);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/connector", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              handleConnector(request);
            });
  server.on("/Books/*", HTTP_GET, [](AsyncWebServerRequest *request) {
    String path = request->url();  // e.g. /Books/MyBook.epub
    if (!SD_MMC.exists(path)) {
      request->send(404);
      return;
    }
    File file = SD_MMC.open(path, "r");
    if (!file) {
      request->send(500);
      return;
    }

    String mimeType = "application/octet-stream";
    if (path.endsWith(".epub")) mimeType = "application/epub+zip";

    AsyncWebServerResponse *response = request->beginResponse(mimeType, file.size(), [file](uint8_t *buffer, size_t maxLen, size_t index) mutable -> size_t {
      file.seek(index);
      size_t bytesRead = file.read(buffer, maxLen);
      if (bytesRead == 0) {
        file.close();
      }
      return bytesRead;
    });

    if (path.endsWith(".epub")) {
      String filename = path.substring(path.lastIndexOf('/') + 1);
      response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    }

    // Strict no-cache for book downloads
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    response->addHeader("Surrogate-Control", "no-store");

    request->send(response);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    Serial.println("[SAVE] Request received");

    // Check for required POST params
    if (!request->hasParam("filename", true) || !request->hasParam("content", true)) {
      Serial.println("[SAVE] Missing filename or content parameter");
      return request->send(400, "text/plain", "Missing filename or content");
    }

    // Get parameters (POST body)
    const AsyncWebParameter *pFile = request->getParam("filename", true);
    const AsyncWebParameter *pContent = request->getParam("content", true);

    String path = pFile->value();        // e.g. "/docs/config.json"
    String content = pContent->value();  // new file text

    Serial.printf("[SAVE] Filename: %s\n", path.c_str());
    Serial.printf("[SAVE] Content length: %d\n", content.length());

    // Open file for writing (overwrite)
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) {
      Serial.printf("[SAVE] Failed to open file: %s\n", path.c_str());
      return request->send(500, "text/plain", "Failed to open " + path);
    }

    size_t bytesWritten = f.print(content);
    f.close();

    Serial.printf("[SAVE] Bytes written: %d\n", bytesWritten);

    if (bytesWritten != content.length()) {
      Serial.println("[SAVE] Warning: bytes written does not match content length");
    }

    request->send(200, "text/plain", "OK");
  });

  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc;
    doc["rgbMode"] = settings.rgbMode;
    doc["rgbColor"] = settings.rgbColor;
    doc["wifiSSID"] = settings.wifiSSID;
    doc["wifiPassword"] = settings.wifiPassword;
    doc["brightness"] = settings.brightness;
    doc["autoGenerateMedia"] = settings.autoGenerateMedia;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // GET current settings
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc;
    doc["rgbMode"] = settings.rgbMode;
    doc["rgbColor"] = settings.rgbColor;
    doc["adminPassword"] = settings.adminPassword;
    doc["wifiSSID"] = settings.wifiSSID;
    doc["wifiPassword"] = settings.wifiPassword;
    doc["brightness"] = settings.brightness;
    doc["autoGenerateMedia"] = settings.autoGenerateMedia;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // POST to update settings
  server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("body", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }

    String body = request->getParam("body", true)->value();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
      request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    if (doc.containsKey("rgbMode")) settings.rgbMode = doc["rgbMode"].as<String>();
    if (doc.containsKey("rgbColor")) settings.rgbColor = doc["rgbColor"].as<String>();
    if (doc.containsKey("adminPassword")) settings.adminPassword = doc["adminPassword"].as<String>();
    if (doc.containsKey("wifiSSID")) settings.wifiSSID = doc["wifiSSID"].as<String>();
    if (doc.containsKey("wifiPassword")) settings.wifiPassword = doc["wifiPassword"].as<String>();
    if (doc.containsKey("brightness")) settings.brightness = doc["brightness"].as<int>();
    if (doc.containsKey("autoGenerateMedia")) settings.autoGenerateMedia = doc["autoGenerateMedia"].as<bool>();

    if (saveSettings()) {
      request->send(200, "application/json", "{\"status\":\"updated\"}");
    } else {
      request->send(500, "application/json", "{\"error\":\"Failed to save settings\"}");
    }
  });
  server.on("/admin-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Build JSON
    StaticJsonDocument<256> doc;
    doc["ssid"] = settings.wifiSSID;
    doc["wifiPassword"] = settings.wifiPassword;
    doc["users"] = getConnectedUserCount();

    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload);
  });

  // 2) Restart the device
  server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Rebooting...");
    delay(100);     // let the response go out
    ESP.restart();  // trigger a software restart
  });

  server.on("/cpu-temp", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", String("{\"temp\":") + currentTempC + "}");
  });

  server.on("/enterUsb", HTTP_POST, [](AsyncWebServerRequest *request) {
    Serial.println(">>> /enterUsb handler hit");
    request->send(200, "text/plain", "OK: entering USB MSC mode...");
    delay(200);
    set_boot_mode(USB_MODE);
    ESP.restart();
  });

  // ─── USB‑mode switch: jump to USB MSC on Boot‑button press ───
  attachInterrupt(
    BOOT_BUTTON_PIN, []() {
      set_boot_mode(USB_MODE);
      esp_restart();  // actually reboot into USB mode
    },
    FALLING);

  // Disable all caching globally
  DefaultHeaders::Instance().addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  DefaultHeaders::Instance().addHeader("Pragma", "no-cache");
  DefaultHeaders::Instance().addHeader("Expires", "0");

  // Start the web server
  server.begin();
  updateToggleStatus();  // Reflect initial WiFi and SD status
  Serial.println("Web Server started!");
}

// ==================== MAIN LOOP ====================

void loop() {
  dnsServer.processNextRequest();
  Timer_Loop();

  if (currentLEDMode == 1) {
    RGB_Lamp_Loop(2);
  }
  if (sdErrorFlag) {
    if (millis() > sdErrorCooldownUntil && tryRecoverSDCard()) {
      sdErrorFlag = false;  // we’re back in business
    } else {
      delay(5);  // keep feeding WDT while waiting
      return;    // skip rest of loop until recovery works
    }
  }
  // Update UI toggle status every second
  if (millis() - lastUpdateTime > 1000) {
    updateToggleStatus();
    lastUpdateTime = millis();
  }

  if (millis() - lastTempReading > 6000) {
    currentTempC = temperatureRead();
    lastTempReading = millis();
  }

  delay(5);  // Prevent watchdog starvation
  updateClientCount();
}

void RGB_SetMode(uint8_t mode) {
  currentLEDMode = mode;

  if (mode == 0) {
    // Turn off LED immediately
    Set_Color(0, 0, 0);
  } else if (mode == 2) {
    Set_Color(solidG, solidR, solidB);
  }
}
