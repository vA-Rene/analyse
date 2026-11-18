/*
 * File: ota.h
 * Purpose: Over-the-air firmware updates, checked on every data fetch.
 *
 * Flow: GET OTA_MANIFEST_URL (version.json) -> if "build" > FW_BUILD, stream the
 * firmware into the free app partition while hashing it (SHA-256), check the
 * ECDSA P-256 signature from the manifest against OTA_PUBLIC_KEY_PEM
 * (ota_pubkey.h), and only then switch partitions and restart.
 *
 * The signature is what makes this safe: TLS does not verify certificates
 * (see https_tls13.h), so without it anybody on the network could install
 * their own firmware. Releases are signed with tools/ota_release.py.
 *
 * Rollback: a new firmware starts as "pending verify". otaConfirm() marks it
 * good after the first successful data fetch. If it resets before that, the
 * bootloader goes back to the previous firmware.
 *
 * version.json:
 *   {"build": 2, "version": "1.1.0", "url": "https://.../FitDashboard.bin",
 *    "size": 2051234, "sig": "<base64 DER ECDSA signature of the .bin>"}
 */
#pragma once
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/base64.h>
#include "ota_pubkey.h"

// Arduino core hook: do not mark a new firmware valid at boot, otaConfirm() does it.
extern "C" bool verifyRollbackLater() { return true; }

// Mark the running firmware as good (no-op if it already is).
inline void otaConfirm() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.printf("[OTA] firmware %s confirmed\n", FW_VERSION);
  }
}

inline void otaRecordFailure(int build);

// After an update: if the new firmware cannot work (no Wi-Fi), go back to the old one.
inline void otaRollbackIfPending() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    Serial.printf("[OTA] firmware %s not working, rolling back\n", FW_VERSION);
    otaRecordFailure(FW_BUILD);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

// true if sig (DER) is a valid signature of the SHA-256 hash with the release key
inline bool otaSignatureOk(const uint8_t hash[32], const uint8_t* sig, size_t sigLen) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  bool ok = mbedtls_pk_parse_public_key(&pk, (const unsigned char*)OTA_PUBLIC_KEY_PEM, strlen(OTA_PUBLIC_KEY_PEM) + 1) == 0 &&
            mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sigLen) == 0;
  mbedtls_pk_free(&pk);
  return ok;
}

// A build that failed OTA_MAX_TRIES times is skipped until a newer one appears.
inline bool otaGaveUp(int build) {
  Preferences p;
  p.begin("fit_ota", true);
  bool gaveUp = p.getInt("failBuild", 0) == build && p.getInt("failCount", 0) >= OTA_MAX_TRIES;
  p.end();
  return gaveUp;
}
inline void otaRecordFailure(int build) {
  Preferences p;
  p.begin("fit_ota", false);
  int count = p.getInt("failBuild", 0) == build ? p.getInt("failCount", 0) + 1 : 1;
  p.putInt("failBuild", build);
  p.putInt("failCount", count);
  p.end();
}

// Download, verify and install. Returns false on any error (old firmware stays active).
inline bool otaInstall(const String& url, size_t size, const String& sigB64) {
  uint8_t sig[160];
  size_t sigLen = 0;
  if (mbedtls_base64_decode(sig, sizeof(sig), &sigLen, (const unsigned char*)sigB64.c_str(), sigB64.length()) != 0) {
    Serial.println("[OTA] invalid signature encoding");
    return false;
  }
  HttpsConn c;
  int s = httpsOpenUrl(c, url);
  if (s != 200 || c.contentLength != (long)size) {
    Serial.printf("[OTA] download HTTP %d, length %ld (expected %u)\n", s, c.contentLength, (unsigned)size);
    httpsClose(c);
    return false;
  }
  if (!Update.begin(size)) {
    Serial.printf("[OTA] Update.begin: %s\n", Update.errorString());
    httpsClose(c);
    return false;
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  static uint8_t buf[4096];
  size_t done = 0;
  bool ok = true;
  while (done < size) {
    int n = httpsRead(c, buf, min(sizeof(buf), size - done));
    if (n <= 0) { Serial.printf("[OTA] connection ended after %u bytes\n", (unsigned)done); ok = false; break; }
    mbedtls_sha256_update(&sha, buf, n);
    if (Update.write(buf, n) != (size_t)n) { Serial.printf("[OTA] write: %s\n", Update.errorString()); ok = false; break; }
    done += n;
  }
  httpsClose(c);
  uint8_t hash[32];
  mbedtls_sha256_finish(&sha, hash);
  mbedtls_sha256_free(&sha);
  if (ok && !otaSignatureOk(hash, sig, sigLen)) { Serial.println("[OTA] signature INVALID, update discarded"); ok = false; }
  if (!ok) { Update.abort(); return false; }
  if (!Update.end()) { Serial.printf("[OTA] Update.end: %s\n", Update.errorString()); return false; }
  return true;
}

// Check the manifest and install a newer build. Needs Wi-Fi. On success the
// device restarts and this function does not return.
// showStatus(line1, line2) puts a message on the display.
inline void otaCheck(int batteryPct, void (*showStatus)(const char*, const char*)) {
  if (strlen(OTA_MANIFEST_URL) == 0) return;
  HttpsConn c;
  int s = httpsOpenUrl(c, OTA_MANIFEST_URL);
  JsonDocument doc;
  if (s == 200) {
    char body[1024];
    size_t len = 0;
    int n;
    while (len < sizeof(body) - 1 && (n = httpsRead(c, (uint8_t*)body + len, sizeof(body) - 1 - len)) > 0) len += n;
    body[len] = 0;
    if (deserializeJson(doc, body)) s = -6;
  }
  httpsClose(c);
  if (s != 200) {
    Serial.printf("[OTA] manifest HTTP %d\n", s);
    return;
  }
  int build = doc["build"] | 0;
  const char* version = doc["version"] | "?";
  size_t size = doc["size"] | 0;
  String url = doc["url"] | "", sig = doc["sig"] | "";
  if (build <= FW_BUILD) return;  // up to date
  Serial.printf("[OTA] build %d (%s) available, running %d (%s)\n", build, version, FW_BUILD, FW_VERSION);
  if (otaGaveUp(build)) { Serial.println("[OTA] skipped, failed too often"); return; }
  if (batteryPct < OTA_MIN_BATTERY) { Serial.printf("[OTA] battery %d %% too low\n", batteryPct); return; }
  if (size == 0 || url.length() == 0 || sig.length() == 0) { Serial.println("[OTA] manifest incomplete"); return; }

  char line[48];
  snprintf(line, sizeof(line), "Version %s wird geladen …", version);
  showStatus("Update", line);
  if (otaInstall(url, size, sig)) {
    snprintf(line, sizeof(line), "Version %s installiert, Neustart …", version);
    showStatus("Update", line);
    delay(500);
    ESP.restart();
  }
  otaRecordFailure(build);
  showStatus("Update fehlgeschlagen", "Alte Version läuft weiter");
  delay(3000);
}
