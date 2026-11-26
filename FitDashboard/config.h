/*
 * File: config.h
 * Purpose: Default settings for the Fit Dashboard. Values saved in the setup
 * portal (5x quick button press) take precedence over these defaults.
 * Secrets (Wi-Fi password, Garmin API key) live in secrets.h, which is not
 * committed to git. Copy secrets.example.h to secrets.h to get started.
 */
#pragma once

// --- Location for the weather forecast (Open-Meteo, no key needed) ---
#define DEFAULT_PLACE_NAME   "Würzburg"
#define DEFAULT_LATITUDE     49.79f
#define DEFAULT_LONGITUDE    9.95f

// --- Time zone: Central European Time with automatic daylight saving time ---
#define TIMEZONE_POSIX       "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1         "pool.ntp.org"
#define NTP_SERVER_2         "time.google.com"
#define NTP_SERVER_3         "time.cloudflare.com"

// --- Training defaults ---
#define DEFAULT_WEEKLY_GOAL_KM   25    // weekly running goal shown as progress bar
#define DEFAULT_HR_MAX           190   // maximum heart rate for the load model
#define DEFAULT_HR_REST          48    // used until Garmin has enough resting HR nights
#define TRIMP_PER_MIN_NO_HR      1.9f  // load estimate for activities without heart rate

// --- Timing ---
#define DATA_FETCH_INTERVAL_MS   (60UL * 60UL * 1000UL)  // fetch Garmin + weather every hour
#define SCREEN_RETURN_MS         (10UL * 60UL * 1000UL)  // detail screens return to screen 1 after 10 min (0 = never)
#define GARMIN_TOKEN_MAX_AGE_S   (20L * 3600L)           // Garmin access tokens live 24 h; renew after 20 h

// --- Analysis windows ---
#define HEALTH_DAYS      14   // days shown on the body screen
#define COACH_DAYS       30   // days of daily values and nights loaded (the sleep list allows about 30)
#define ACTIVITY_DAYS    120  // days of activities loaded
#define LOAD_DAYS        56   // days of activities used for fitness/fatigue
#define GARMIN_FIRST_DAY ""   // optional "YYYY-MM-DD": ignore Garmin data before this day; "" = no limit
#define CHART_LOAD_DAYS  42   // days shown in the form chart
#define WEEKS_SHOWN      8    // weeks in the weekly-km chart

// --- Coach (coach.h): an LLM answers questions about your data with one drawn screen ---
// The coach screen comes after the three fixed screens. The kit has no microphone and the display
// holds no key for the AI service: the question comes from the phone, and the phone sends the
// coach's requests to the AI service with a Gemini key kept in its browser (below). These fixed
// questions are offered on the phone's page to tap.
#define COACH_MODEL      "gemini-3.5-flash-lite"
#define COACH_ROUNDS     3    // LLM calls at most per question, including corrections
#define COACH_QUESTIONS  { "How was my sleep this week?", \
                           "Am I training too much? Should I rest tomorrow?", \
                           "How is my training split across heart rate zones this month?", \
                           "What does my week look like so far?", \
                           "Is my fitness improving?" }

// --- Phone as the microphone (phone.h) ---
// KEY1 on the coach screen: the display joins the Wi-Fi and serves a page for the phone's browser.
// There the phone's keyboard turns speech into text, and the page sends it to the display. The page
// asks for a Gemini API key once (https://aistudio.google.com), keeps it in the browser and has a
// button that deletes it. Use a key with a quota: who is in your Wi-Fi could get at it.
#define PHONE_HOSTNAME       "fitdash"               // the page is at http://fitdash.local (letters, digits, hyphen)
#define PHONE_WINDOW_MS      (90UL * 1000UL)         // the display waits this long for a question, Wi-Fi on
#define PHONE_WINDOW_MAX_MS  (5UL * 60UL * 1000UL)   // and at most this long while someone is typing on the page
#define PHONE_QUESTION_MAX   300                     // characters of a question

// --- Firmware version and over-the-air updates (ota.h, tools/ota_release.py) ---
#define FW_VERSION        "1.1.0"   // shown on the start screen and in the setup portal
#define FW_BUILD          3         // +1 for every release; devices only install a higher build
// Checked on every data fetch (hourly and KEY1). Empty string = updates off.
#define OTA_MANIFEST_URL  "https://github.com/vA-Rene/analyse/releases/latest/download/version.json"
#define OTA_MIN_BATTERY   30        // % battery needed to start an update
#define OTA_MAX_TRIES     3         // give up on a build after this many failed installs
