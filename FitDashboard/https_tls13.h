/*
 * File: https_tls13.h
 * Purpose: Minimal HTTPS client with TLS 1.3 (wolfSSL) on top of WiFiClient.
 *
 * Why: the Garmin Connect Personal API only accepts TLS 1.3, but the TLS
 * library inside the ESP32 Arduino core is built with TLS 1.2 only.
 * wolfSSL brings its own TLS 1.3.
 *
 * Needs the Arduino library "wolfssl" (Library Manager, tested with 5.8.4).
 * Add these lines to the end of  <Arduino>/libraries/wolfssl/src/user_settings.h :
 *
 *   // Fit Dashboard
 *   #define HAVE_SNI
 *   #if defined(ESP32)
 *     #include <esp_random.h>
 *     #define CUSTOM_RAND_TYPE      uint32_t
 *     #define CUSTOM_RAND_GENERATE  esp_random
 *   #endif
 *
 * HAVE_SNI: Garmin sits behind Cloudflare, which refuses connections without SNI.
 * CUSTOM_RAND_GENERATE: seed wolfSSL from the ESP32 hardware RNG; without it wolfSSL
 * falls back to the predictable C library random() on Arduino boards.
 *
 * The server certificate is not verified (same as setInsecure() before).
 */
#pragma once
#include <wolfssl.h>
#include <wolfssl/ssl.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#ifndef HAVE_SNI
#error "wolfSSL: HAVE_SNI fehlt. Zeilen aus dem Kommentar oben in https_tls13.h an libraries/wolfssl/src/user_settings.h anhängen."
#endif
#ifndef CUSTOM_RAND_GENERATE
#error "wolfSSL: CUSTOM_RAND_GENERATE fehlt. Zeilen aus dem Kommentar oben in https_tls13.h an libraries/wolfssl/src/user_settings.h anhängen."
#endif

static WOLFSSL_CTX* tlsCtx = nullptr;

static int tlsSend(WOLFSSL*, char* buf, int sz, void* ctx) {
  WiFiClient* c = (WiFiClient*)ctx;
  size_t n = c->write((const uint8_t*)buf, sz);
  return n > 0 ? (int)n : WOLFSSL_CBIO_ERR_CONN_CLOSE;
}

static int tlsRecv(WOLFSSL*, char* buf, int sz, void* ctx) {
  WiFiClient* c = (WiFiClient*)ctx;
  unsigned long started = millis();
  while (!c->available()) {
    if (!c->connected()) return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    if (millis() - started > 20000) return WOLFSSL_CBIO_ERR_TIMEOUT;
    delay(2);
  }
  int n = c->read((uint8_t*)buf, sz);
  return n > 0 ? n : WOLFSSL_CBIO_ERR_GENERAL;
}

static bool tlsInit() {
  if (tlsCtx) return true;
  if (wolfSSL_Init() != WOLFSSL_SUCCESS) return false;
  tlsCtx = wolfSSL_CTX_new(wolfTLSv1_3_client_method());
  if (!tlsCtx) return false;
  wolfSSL_CTX_set_verify(tlsCtx, WOLFSSL_VERIFY_NONE, nullptr);
  wolfSSL_SetIOSend(tlsCtx, tlsSend);
  wolfSSL_SetIORecv(tlsCtx, tlsRecv);
  return true;
}

// Response body in PSRAM; caller frees with httpsFree().
struct HttpsResponse {
  int status = -1;        // HTTP status, or negative on network/TLS errors
  char* body = nullptr;
  size_t bodyLen = 0;
};

inline void httpsFree(HttpsResponse& r) {
  if (r.body) heap_caps_free(r.body);
  r.body = nullptr;
}

// One request with "Connection: close". method = "GET" or "POST" (empty body).
// Errors: -1 connect, -2 TLS handshake, -3 send, -4 no/invalid answer, -5 out of memory.
inline HttpsResponse httpsRequest(const char* host, const char* method, const String& path, const String& auth) {
  HttpsResponse r;
  if (!tlsInit()) { r.status = -2; return r; }
  WiFiClient client;
  client.setTimeout(20);
  if (!client.connect(host, 443, 15000)) { r.status = -1; return r; }

  WOLFSSL* ssl = wolfSSL_new(tlsCtx);
  if (!ssl) { client.stop(); r.status = -5; return r; }
  wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOST_NAME, host, strlen(host));
  wolfSSL_SetIOReadCtx(ssl, &client);
  wolfSSL_SetIOWriteCtx(ssl, &client);
  int hs = wolfSSL_connect(ssl);
  if (hs != WOLFSSL_SUCCESS) {
    Serial.printf("[TLS] handshake with %s failed: %d\n", host, wolfSSL_get_error(ssl, hs));
    wolfSSL_free(ssl); client.stop(); r.status = -2; return r;
  }

  String req;
  req.reserve(256 + path.length() + auth.length());
  req += method; req += ' '; req += path; req += " HTTP/1.0\r\nHost: "; req += host;
  // "*/*" like curl: the Garmin token endpoint answers with plain text and rejects
  // "Accept: application/json" with HTTP 406
  req += "\r\nAccept: */*\r\nUser-Agent: FitDashboard/1.0\r\n";
  if (auth.length()) { req += "Authorization: "; req += auth; req += "\r\n"; }
  if (strcmp(method, "POST") == 0) req += "Content-Length: 0\r\n";
  req += "Connection: close\r\n\r\n";
  if (wolfSSL_write(ssl, req.c_str(), req.length()) != (int)req.length()) {
    wolfSSL_free(ssl); client.stop(); r.status = -3; return r;
  }

  // read the whole answer into PSRAM (HTTP/1.0 -> no chunked encoding)
  size_t cap = 16384, len = 0;
  char* buf = (char*)heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { wolfSSL_free(ssl); client.stop(); r.status = -5; return r; }
  for (;;) {
    if (cap - len < 4096) {
      char* bigger = (char*)heap_caps_realloc(buf, cap * 2 + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!bigger) { heap_caps_free(buf); wolfSSL_free(ssl); client.stop(); r.status = -5; return r; }
      buf = bigger; cap *= 2;
    }
    int n = wolfSSL_read(ssl, buf + len, (int)(cap - len));
    if (n <= 0) break;  // close notify or connection closed = end of body
    len += n;
  }
  buf[len] = 0;
  wolfSSL_shutdown(ssl);
  wolfSSL_free(ssl);
  client.stop();

  char* bodyStart = strstr(buf, "\r\n\r\n");
  if (len < 12 || strncmp(buf, "HTTP/1.", 7) != 0 || !bodyStart) { heap_caps_free(buf); r.status = -4; return r; }
  r.status = atoi(buf + 9);
  // move the body to the start of the buffer so httpsFree() can free it
  bodyStart += 4;
  r.bodyLen = len - (bodyStart - buf);
  memmove(buf, bodyStart, r.bodyLen + 1);
  r.body = buf;
  return r;
}
