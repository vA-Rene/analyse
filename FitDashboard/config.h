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
#define HEALTH_DAYS      14   // days shown on the health detail screen
#define LOAD_DAYS        56   // days of activities used for fitness/fatigue
#define CHART_LOAD_DAYS  42   // days shown in the form chart
#define WEEKS_SHOWN      8    // weeks in the weekly-km chart
