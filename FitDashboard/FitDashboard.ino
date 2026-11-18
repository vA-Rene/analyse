/*
 * Project Name: Fit Dashboard
 * Designed For: TRMNL 7.5" (OG) DIY Kit by Seeed Studio
 *               (XIAO ESP32-S3 Plus, XIAO ePaper Display Board EE04, 800x480 b/w)
 *
 * Health data (Garmin Connect Personal API) and running data with weather
 * (Open-Meteo) on three screens:
 *   Screen 1  Overview: health left, running + weather right (default)
 *   Screen 2  Health details      Screen 3  Training details
 *
 * Firmware updates: over the air on every data fetch, see ota.h.
 *
 * Buttons: KEY1 = refresh data, KEY2 = next screen, KEY3 = previous screen,
 *          5x quick press on any key = setup portal (Wi-Fi, Garmin key, location).
 * Detail screens return to screen 1 after SCREEN_RETURN_MS (config.h).
 *
 * Arduino IDE: Board "XIAO_ESP32S3_PLUS", PSRAM "OPI PSRAM",
 *              Partition "16M Flash (3MB APP/9.9MB FATFS)".
 * Libraries:   Seeed_GFX, ArduinoJson 7, wolfssl (with HAVE_SNI, see https_tls13.h).
 * Upload: hold B, press R, release B; press R after the upload (light sleep).
 *
 * License: GPL-3.0-or-later. Based on the Vector eInk Dashboard by Huy Vector,
 * original work Copyright (C) 2026 Ameya Angadi. Wi-Fi, clock, battery, button
 * and light-sleep handling are taken over from that project.
 */

#include <wolfssl.h>   // must come first: TLS 1.3 for Garmin (see https_tls13.h)
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "driver.h"
#include "time.h"
#include "TFT_eSPI.h"
#include "config.h"
// OTA release builds (-DFITDASH_RELEASE, tools/ota_release.py) never contain secrets.h
#if __has_include("secrets.h") && !defined(FITDASH_RELEASE)
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifdef EPAPER_ENABLE
EPaper epaper;
#endif

#include "model.h"
#include "parse.h"
#include "metrics.h"
#include "ui.h"
#include "https_tls13.h"
#include "ota.h"

// =========================================================================
// Settings (setup portal values in Preferences override config/secrets)
// =========================================================================
#define STR_(x) #x
#define STR(x) STR_(x)

struct Settings {
  String ssid, password, garminKey, place;
  float lat, lon;
  int weeklyGoalKm, hrMax;
} cfg;

Preferences prefs;
WebServer configServer(80);
DNSServer dnsServer;

// --- Hardware pins (same as the Vector eInk Dashboard) ---
const int BUTTON_PREV = D4;    // KEY3: previous screen
const int BUTTON_NEXT = D2;    // KEY2: next screen
const int BUTTON_SELECT = D1;  // KEY1: refresh data
#define BATTERY_PIN 1
#define ADC_EN_PIN 6
const float CALIBRATION_FACTOR = 0.968;

// --- State ---
static DashboardData D;
int currentScreen = SCREEN_OVERVIEW;
bool needsScreenUpdate = false, needsDataRefresh = false;
unsigned long lastDataFetch = 0, lastScreenChange = 0, lastButtonPress = 0;
int lastMinute = -1, lastDay = -1;
uint8_t clockRefreshCount = 0;
bool lastStatePrev = HIGH, lastStateNext = HIGH, lastStateSel = HIGH;
int rapidPressCount = 0;
unsigned long rapidPressWindowStart = 0;
bool portalCountdownActive = false;
unsigned long portalCountdownStart = 0;
int lastCountdownSecond = -1;

String garminToken;
long garminTokenTime = 0;

void startConfigPortal();
void showMessage(const char* line1, const char* line2);

// =========================================================================
// Settings
// =========================================================================
void loadSettings() {
  // Store compiled-in secrets once, so later OTA releases (built without secrets.h) keep them.
  prefs.begin("fit_cfg", false);
  if (!prefs.isKey("ssid") && strlen(SECRET_WIFI_SSID)) {
    prefs.putString("ssid", SECRET_WIFI_SSID);
    prefs.putString("pass", SECRET_WIFI_PASSWORD);
  }
  if (!prefs.isKey("garminKey") && strlen(SECRET_GARMIN_API_KEY)) prefs.putString("garminKey", SECRET_GARMIN_API_KEY);
  prefs.end();
  prefs.begin("fit_cfg", true);
  cfg.ssid = prefs.getString("ssid", SECRET_WIFI_SSID);
  cfg.password = prefs.getString("pass", SECRET_WIFI_PASSWORD);
  cfg.garminKey = prefs.getString("garminKey", SECRET_GARMIN_API_KEY);
  cfg.place = prefs.getString("place", DEFAULT_PLACE_NAME);
  cfg.lat = prefs.getFloat("lat", DEFAULT_LATITUDE);
  cfg.lon = prefs.getFloat("lon", DEFAULT_LONGITUDE);
  cfg.weeklyGoalKm = prefs.getInt("goal", DEFAULT_WEEKLY_GOAL_KM);
  cfg.hrMax = prefs.getInt("hrMax", DEFAULT_HR_MAX);
  garminToken = prefs.getString("gToken", "");
  garminTokenTime = prefs.getLong("gTokenT", 0);
  prefs.end();
  // empty portal fields fall back to the compiled-in defaults
  if (cfg.ssid.length() == 0) cfg.ssid = SECRET_WIFI_SSID;
  if (cfg.garminKey.length() == 0) cfg.garminKey = SECRET_GARMIN_API_KEY;
  D.weeklyGoalKm = cfg.weeklyGoalKm;
  D.hrMax = cfg.hrMax;
}

// =========================================================================
// Wi-Fi, clock, battery (from the Vector eInk Dashboard)
// =========================================================================
void turnOffWiFi() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

bool connectWiFi(unsigned long timeoutMs = 20000) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (cfg.ssid.length() == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < timeoutMs) delay(250);
  if (WiFi.status() == WL_CONNECTED) return true;
  turnOffWiFi();
  return false;
}

bool syncClock(unsigned long timeoutMs = 10000) {
  configTzTime(TIMEZONE_POSIX, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  struct tm t;
  unsigned long started = millis();
  while (millis() - started < timeoutMs) {
    if (getLocalTime(&t, 1000) && t.tm_year + 1900 >= 2025) {
      prefs.begin("fit_cfg", false);
      prefs.putULong64("lastEpoch", (uint64_t)time(nullptr));
      prefs.end();
      return true;
    }
  }
  // fall back to the last known time so the screen still shows something sensible
  prefs.begin("fit_cfg", true);
  uint64_t saved = prefs.getULong64("lastEpoch", 0);
  prefs.end();
  if (saved > 1700000000ULL) {
    struct timeval tv = {(time_t)saved, 0};
    settimeofday(&tv, nullptr);
  }
  return false;
}

int batteryPercent() {
  digitalWrite(ADC_EN_PIN, HIGH);
  delay(10);
  long sum = 0;
  for (int i = 0; i < 30; i++) { sum += analogRead(BATTERY_PIN); delayMicroseconds(100); }
  digitalWrite(ADC_EN_PIN, LOW);
  float voltage = (sum / 30.0f / 4095.0f) * 3.6f * 2.0f * CALIBRATION_FACTOR;
  int p = (int)((voltage - 3.2f) * 100.0f / (4.2f - 3.2f));
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

void lowPowerIdle() {
  // Light sleep keeps RAM, time and the frame buffer; any button wakes the MCU.
  esp_sleep_enable_timer_wakeup(250000ULL);
  esp_light_sleep_start();
}

// =========================================================================
// HTTPS
// =========================================================================
// GET host+path over TLS 1.3 and parse the JSON answer (optionally filtered).
// Returns the HTTP status, -6 for invalid JSON, other negatives see httpsRequest().
int httpsGetJson(const char* host, const String& path, const String& auth, JsonDocument& doc, JsonDocument* filter) {
  HttpsResponse r = httpsRequest(host, "GET", path, auth);
  int status = r.status;
  if (status == 200) {
    DeserializationError err = filter ? deserializeJson(doc, r.body, r.bodyLen, DeserializationOption::Filter(*filter))
                                      : deserializeJson(doc, r.body, r.bodyLen);
    if (err) {
      Serial.printf("[HTTP] JSON error %s for %s%s\n", err.c_str(), host, path.c_str());
      status = -6;
    }
  } else {
    Serial.printf("[HTTP] %s%s -> %d\n", host, path.c_str(), status);
  }
  httpsFree(r);
  return status;
}

// =========================================================================
// Garmin Connect Personal API
// =========================================================================
const char* GARMIN_HOST = "personal.connectapi.garmin.com";

bool garminRefreshToken() {
  HttpsResponse r = httpsRequest(GARMIN_HOST, "POST", "/personalapitoken-service/personal-api/personal-api-key/token",
                                 "ApiKey " + cfg.garminKey);
  int status = r.status;
  String body = r.body ? String(r.body) : String();
  httpsFree(r);
  if (status != 200) {
    Serial.printf("[Garmin] token HTTP %d\n", status);
    if (status == 401 || status == 403) snprintf(D.garminStatus, sizeof(D.garminStatus), "API-Key ungültig (HTTP %d)", status);
    else if (status < 0) snprintf(D.garminStatus, sizeof(D.garminStatus), "Verbindung fehlgeschlagen (%d)", status);
    else snprintf(D.garminStatus, sizeof(D.garminStatus), "Token-Fehler (HTTP %d)", status);
    return false;
  }
  body.trim();
  if (body.startsWith("\"")) body = body.substring(1, body.length() - 1);
  garminToken = body;
  garminTokenTime = (long)time(nullptr);
  prefs.begin("fit_cfg", false);
  prefs.putString("gToken", garminToken);
  prefs.putLong("gTokenT", garminTokenTime);
  prefs.end();
  return true;
}

// GET a Garmin endpoint; renews the token once on 401.
int garminGet(const String& path, JsonDocument& doc, void (*makeFilter)(JsonDocument&)) {
  JsonDocument filter;
  if (makeFilter) makeFilter(filter);
  for (int attempt = 0; attempt < 2; attempt++) {
    bool tokenOld = (long)time(nullptr) - garminTokenTime > GARMIN_TOKEN_MAX_AGE_S;
    if (garminToken.length() == 0 || tokenOld || attempt == 1) {
      if (!garminRefreshToken()) return -401;
    }
    doc.clear();
    int status = httpsGetJson(GARMIN_HOST, path, "Bearer " + garminToken, doc, makeFilter ? &filter : nullptr);
    if (status != 401) return status;
    garminToken = "";
  }
  return 401;
}

String isoDate(long dayNum) {
  char buf[11];
  isoFromDayNumber(dayNum, buf);
  return String(buf);
}

void fetchGarmin() {
  D.garminStatus[0] = 0;
  if (cfg.garminKey.length() == 0) {
    snprintf(D.garminStatus, sizeof(D.garminStatus), "Kein API-Key (Setup-Portal)");
    return;
  }
  String today = isoDate(D.todayNum), first = isoDate(D.todayNum - (HEALTH_DAYS - 1));
  JsonDocument doc;
  int s;

  HealthToday fresh;
  s = garminGet("/usersummary-service/usersummary/daily?calendarDate=" + today, doc, filterUserSummary);
  if (s == 200) parseUserSummary(doc, fresh);
  else {
    if (!D.garminStatus[0]) snprintf(D.garminStatus, sizeof(D.garminStatus), "nicht erreichbar (%d)", s);
    return;  // keep the previous values
  }
  s = garminGet("/sleep-service/sleep/dailySleep?date=" + today, doc, filterDailySleep);
  if (s == 200) parseDailySleep(doc, fresh);
  s = garminGet("/wellness-service/wellness/bodyBattery/reports/daily?startDate=" + today + "&endDate=" + today + "&chunks=96",
                doc, filterBodyBattery);
  if (s == 200) parseBodyBattery(doc, fresh);
  D.today = fresh;

  resetHealthDays(D);
  s = garminGet("/usersummary-service/v2/dailysummaries?startDate=" + first + "&endDate=" + today, doc, filterDailySummaries);
  if (s == 200) parseDailySummaries(doc, D);
  s = garminGet("/sleep-service/sleep/dailySleepSummariesByDate?startDate=" + first + "&endDate=" + today, doc, filterSleepRange);
  if (s == 200) parseSleepRange(doc, D);

  s = garminGet("/activitylist-service/activities/search/activities?startDate=" + isoDate(D.todayNum - (LOAD_DAYS - 1)) +
                "&limit=100", doc, filterActivities);
  if (s == 200) parseActivities(doc, D);
  Serial.printf("[Garmin] ok: bb %d, sleep %.1f h, %d activities\n", D.today.bbNow, D.today.sleepH, D.actN);
}

// =========================================================================
// Open-Meteo
// =========================================================================
void fetchWeather() {
  D.weatherStatus[0] = 0;
  String path = "/v1/forecast?latitude=" + String(cfg.lat, 3) + "&longitude=" + String(cfg.lon, 3) + OPEN_METEO_QUERY;
  JsonDocument doc;
  int s = httpsGetJson("api.open-meteo.com", path, "", doc, nullptr);
  Weather w;
  if (s == 200 && parseWeather(doc, w)) D.wx = w;
  else snprintf(D.weatherStatus, sizeof(D.weatherStatus), "Open-Meteo HTTP %d", s);
}

// =========================================================================
// Data cycle
// =========================================================================
void computeNow() {
  struct tm t;
  if (getLocalTime(&t, 100)) {
    D.todayNum = dayNumber(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    D.nowMin = t.tm_hour * 60 + t.tm_min;
  }
  computeAll(D);
}

void fetchAllData() {
  lastDataFetch = millis();
  if (!connectWiFi()) {
    snprintf(D.garminStatus, sizeof(D.garminStatus), "kein WLAN");
    computeNow();
    return;
  }
  syncClock();
  struct tm t;
  if (getLocalTime(&t, 1000)) {
    D.todayNum = dayNumber(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    snprintf(D.updatedAt, sizeof(D.updatedAt), "%02d:%02d", t.tm_hour, t.tm_min);
  }
  fetchWeather();
  fetchGarmin();
  // the first fetch that reaches a server confirms a freshly installed firmware
  if (!D.weatherStatus[0] || !D.garminStatus[0]) otaConfirm();
  otaCheck(batteryPercent(), showMessage);
  turnOffWiFi();
  computeNow();
}

// =========================================================================
// Display
// =========================================================================
void showScreen(bool cleanCycle) {
#ifdef EPAPER_ENABLE
  if (cleanCycle) {  // black/white cycle against ghosting on page changes
    epaper.fillScreen(TFT_BLACK);
    epaper.update();
    epaper.fillScreen(TFT_WHITE);
    epaper.update();
  }
  struct tm now;
  getLocalTime(&now, 100);
  computeNow();  // recommendation and weather window depend on the time
  drawScreen(currentScreen, D, now, batteryPercent());
  epaper.update();
  clockRefreshCount = 0;
  lastDay = now.tm_mday;
#endif
}

void updateClockPartial(const struct tm& now) {
#ifdef EPAPER_ENABLE
  epaper.fillRect(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, TFT_BLACK);
  epaper.updataPartial(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
  delay(20);
  epaper.fillRect(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, TFT_WHITE);
  epaper.updataPartial(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
  delay(20);
  drawClock(now);
  epaper.updataPartial(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
#endif
}

void showMessage(const char* line1, const char* line2) {
#ifdef EPAPER_ENABLE
  epaper.fillScreen(TFT_WHITE);
  text(line1, 400, 220, 32, MC_DATUM);
  if (line2) text(line2, 400, 270, 24, MC_DATUM);
  epaper.update();
#endif
}

// =========================================================================
// Setup portal
// =========================================================================
String htmlEscape(const String& v) {
  String o;
  for (char c : v) {
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;"; else o += c;
  }
  return o;
}

String portalPage() {
  String p;
  p.reserve(4000);
  p += F("<!doctype html><html lang=de><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>Fit Dashboard Setup</title><style>body{font-family:sans-serif;max-width:480px;margin:20px auto;padding:0 16px}"
         "label{display:block;margin-top:14px;font-weight:600}input{width:100%;padding:8px;font-size:16px;box-sizing:border-box}"
         "button{margin-top:20px;padding:12px;width:100%;font-size:17px}small{color:#555}</style>"
         "<h1>Fit Dashboard</h1><p><small>Firmware " FW_VERSION " (Build " STR(FW_BUILD) ")</small></p><form method=post action=/save>");
  auto field = [&](const char* name, const char* label, const String& value, const char* type, const char* hint) {
    p += "<label for="; p += name; p += ">"; p += label; p += "</label><input id="; p += name; p += " name="; p += name;
    p += " type="; p += type; p += " value=\""; p += htmlEscape(value); p += "\">";
    if (hint) { p += "<small>"; p += hint; p += "</small>"; }
  };
  field("ssid", "WLAN-Name", cfg.ssid, "text", nullptr);
  field("pass", "WLAN-Passwort", cfg.password, "password", nullptr);
  field("garminKey", "Garmin Personal API Key", "", "password",
        cfg.garminKey.length() ? "Gespeichert. Leer lassen, um ihn zu behalten." : "Garmin Connect (Web) &rarr; Personal API Key");
  field("place", "Ort (Anzeige)", cfg.place, "text", nullptr);
  field("lat", "Breitengrad", String(cfg.lat, 4), "text", "z. B. 49.7913");
  field("lon", "Längengrad", String(cfg.lon, 4), "text", "z. B. 9.9534");
  field("goal", "Wochenziel Laufen (km)", String(cfg.weeklyGoalKm), "number", nullptr);
  field("hrMax", "Maximalpuls", String(cfg.hrMax), "number", "Für Belastung und Prognosen");
  p += F("<button type=submit>Speichern und neu starten</button></form></html>");
  return p;
}

void startConfigPortal() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Fit-Dashboard-Setup", "fitdash75");
  dnsServer.start(53, "*", WiFi.softAPIP());
  bool saved = false;
  configServer.on("/", HTTP_GET, []() {
    configServer.sendHeader("Cache-Control", "no-store");
    configServer.send(200, "text/html; charset=utf-8", portalPage());
  });
  configServer.on("/save", HTTP_POST, [&saved]() {
    prefs.begin("fit_cfg", false);
    prefs.putString("ssid", configServer.arg("ssid"));
    prefs.putString("pass", configServer.arg("pass"));
    if (configServer.arg("garminKey").length()) {
      prefs.putString("garminKey", configServer.arg("garminKey"));
      prefs.remove("gToken");
    }
    prefs.putString("place", configServer.arg("place"));
    prefs.putFloat("lat", configServer.arg("lat").toFloat());
    prefs.putFloat("lon", configServer.arg("lon").toFloat());
    prefs.putInt("goal", constrain(configServer.arg("goal").toInt(), 1, 300));
    prefs.putInt("hrMax", constrain(configServer.arg("hrMax").toInt(), 120, 230));
    prefs.end();
    configServer.send(200, "text/html; charset=utf-8", "<meta charset=utf-8><h2>Gespeichert. Das Display startet neu.</h2>");
    saved = true;
  });
  configServer.onNotFound([]() {
    configServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    configServer.send(302, "text/plain", "");
  });
  configServer.begin();
  showMessage("Setup: WLAN \"Fit-Dashboard-Setup\"", "Passwort fitdash75 • dann 192.168.4.1 öffnen");
  while (!saved) {
    dnsServer.processNextRequest();
    configServer.handleClient();
    delay(2);
  }
  delay(1200);
  ESP.restart();
}

void drawPortalCountdown(int secondsLeft) {
#ifdef EPAPER_ENABLE
  epaper.fillScreen(TFT_WHITE);
  text("Setup-Portal startet in", 400, 150, 32, MC_DATUM);
  char b[4]; snprintf(b, sizeof(b), "%d", secondsLeft);
  text(b, 400, 260, 56, MC_DATUM);
  text("Beliebige Taste zum Abbrechen", 400, 390, 18, MC_DATUM);
  epaper.update();
#endif
}

void updatePortalCountdown() {
  if (!portalCountdownActive) return;
  unsigned long elapsed = millis() - portalCountdownStart;
  int secondsLeft = 5 - (int)(elapsed / 1000);
  if (secondsLeft != lastCountdownSecond && secondsLeft > 0) {
    lastCountdownSecond = secondsLeft;
#ifdef EPAPER_ENABLE
    epaper.fillRect(320, 205, 160, 110, TFT_WHITE);
    char b[4]; snprintf(b, sizeof(b), "%d", secondsLeft);
    text(b, 400, 260, 56, MC_DATUM);
    epaper.updataPartial(320, 205, 160, 110);
#endif
  }
  if (elapsed >= 5000) {
    portalCountdownActive = false;
    startConfigPortal();
  }
}

// =========================================================================
// Buttons (edge detection, debounce, 5x quick press = setup portal)
// =========================================================================
void changeScreen(int direction) {
  currentScreen = (currentScreen + direction + SCREEN_COUNT) % SCREEN_COUNT;
  lastScreenChange = millis();
  needsScreenUpdate = true;
}

void handleButtons() {
  bool sPrev = digitalRead(BUTTON_PREV), sNext = digitalRead(BUTTON_NEXT), sSel = digitalRead(BUTTON_SELECT);
  bool edge = (lastStatePrev == HIGH && sPrev == LOW) || (lastStateNext == HIGH && sNext == LOW) ||
              (lastStateSel == HIGH && sSel == LOW);
  if (portalCountdownActive && edge && millis() - lastButtonPress > 200) {
    portalCountdownActive = false;
    rapidPressCount = 0;
    lastButtonPress = millis();
    needsScreenUpdate = true;
  } else if (edge && millis() - lastButtonPress > 200) {
    delay(80);
    sPrev = digitalRead(BUTTON_PREV); sNext = digitalRead(BUTTON_NEXT); sSel = digitalRead(BUTTON_SELECT);
    if (rapidPressCount == 0 || millis() - rapidPressWindowStart > 2500) {
      rapidPressCount = 1;
      rapidPressWindowStart = millis();
    } else rapidPressCount++;
    lastButtonPress = millis();
    if (rapidPressCount >= 5) {
      rapidPressCount = 0;
      portalCountdownActive = true;
      lastCountdownSecond = 5;
      drawPortalCountdown(5);
      portalCountdownStart = millis();
    } else if (sPrev == LOW) changeScreen(-1);
    else if (sNext == LOW) changeScreen(1);
    else if (sSel == LOW) needsDataRefresh = true;
  }
  lastStatePrev = sPrev; lastStateNext = sNext; lastStateSel = sSel;
}

// =========================================================================
// Arduino
// =========================================================================
void setup() {
  Serial.begin(115200);
  loadSettings();
  pinMode(BUTTON_PREV, INPUT_PULLUP);
  pinMode(BUTTON_NEXT, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  gpio_wakeup_enable((gpio_num_t)BUTTON_PREV, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BUTTON_NEXT, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)BUTTON_SELECT, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  pinMode(ADC_EN_PIN, OUTPUT);
  digitalWrite(ADC_EN_PIN, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
  setenv("TZ", TIMEZONE_POSIX, 1);
  tzset();

#ifdef EPAPER_ENABLE
  epaper.begin();
  if (cfg.ssid.length() == 0) startConfigPortal();
  showMessage("Fit Dashboard " FW_VERSION, "Verbinde mit dem WLAN …");
  if (!connectWiFi()) {
    otaRollbackIfPending();  // a fresh update that cannot connect goes back to the old firmware
    startConfigPortal();
  }
  showMessage("Fit Dashboard", "Lade Garmin- und Wetterdaten …");
  fetchAllData();
  currentScreen = SCREEN_OVERVIEW;
  showScreen(true);
#endif
}

void loop() {
  handleButtons();
  updatePortalCountdown();
  if (portalCountdownActive) { delay(10); return; }
  if (rapidPressCount > 0 && millis() - rapidPressWindowStart > 2500) rapidPressCount = 0;

#ifdef EPAPER_ENABLE
  struct tm now;
  if (!getLocalTime(&now, 100)) { delay(500); return; }

  if (needsDataRefresh) {
    showMessage("Aktualisiere …", nullptr);
    fetchAllData();
    needsDataRefresh = false;
    needsScreenUpdate = true;
  }
  if (needsScreenUpdate && rapidPressCount == 0) {
    showScreen(true);
    needsScreenUpdate = false;
  }

  unsigned long ms = millis();
  if (ms - lastDataFetch >= DATA_FETCH_INTERVAL_MS) {
    fetchAllData();
    showScreen(false);
  }
  if (SCREEN_RETURN_MS > 0 && currentScreen != SCREEN_OVERVIEW && ms - lastScreenChange >= SCREEN_RETURN_MS) {
    currentScreen = SCREEN_OVERVIEW;
    showScreen(true);
  }

  if (now.tm_min != lastMinute) {
    lastMinute = now.tm_min;
    if (now.tm_mday != lastDay) {
      showScreen(true);  // new day: dates, week and charts change
    } else if (!needsScreenUpdate) {
      updateClockPartial(now);
      // after three partial clock updates, redraw the whole image once (no black/white cycle)
      if (++clockRefreshCount >= 3) showScreen(false);
    }
  }
#endif
  lowPowerIdle();
}
