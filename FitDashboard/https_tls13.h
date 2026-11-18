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

// One open connection whose headers have been read; the body is read with httpsRead().
// Lets large answers (firmware) stream without holding them in memory.
struct HttpsConn {
  WiFiClient client;
  WOLFSSL* ssl = nullptr;
  int status = -1;
  long contentLength = -1;   // -1 = not sent
  String location;           // "Location" header of redirects
  char* head = nullptr;      // header buffer; body bytes read with the headers stay here
  size_t headPos = 0, headLen = 0;
};

inline void httpsClose(HttpsConn& c) {
  if (c.ssl) { wolfSSL_shutdown(c.ssl); wolfSSL_free(c.ssl); c.ssl = nullptr; }
  c.client.stop();
  if (c.head) { heap_caps_free(c.head); c.head = nullptr; }
  c.headPos = c.headLen = 0;
}

// Body bytes: >0 read, 0 = end of answer (connection closed).
inline int httpsRead(HttpsConn& c, uint8_t* buf, size_t n) {
  if (c.headPos < c.headLen) {
    size_t k = c.headLen - c.headPos;
    if (k > n) k = n;
    memcpy(buf, c.head + c.headPos, k);
    c.headPos += k;
    return (int)k;
  }
  if (!c.ssl) return 0;
  int r = wolfSSL_read(c.ssl, buf, (int)n);
  return r > 0 ? r : 0;  // close notify or connection closed = end of body
}

// Connect, send one request with "Connection: close" and read the headers.
// method = "GET" or "POST" (empty body). Returns the HTTP status or
// -1 connect, -2 TLS handshake, -3 send, -4 no/invalid answer, -5 out of memory.
// Call httpsClose() afterwards in every case.
inline int httpsOpen(HttpsConn& c, const char* host, const char* method, const String& path, const String& auth) {
  c.status = -1; c.contentLength = -1; c.location = "";
  if (!tlsInit()) return c.status = -2;
  c.client.setTimeout(20);
  if (!c.client.connect(host, 443, 15000)) return c.status = -1;
  c.ssl = wolfSSL_new(tlsCtx);
  if (!c.ssl) return c.status = -5;
  wolfSSL_UseSNI(c.ssl, WOLFSSL_SNI_HOST_NAME, host, strlen(host));
  wolfSSL_SetIOReadCtx(c.ssl, &c.client);
  wolfSSL_SetIOWriteCtx(c.ssl, &c.client);
  int hs = wolfSSL_connect(c.ssl);
  if (hs != WOLFSSL_SUCCESS) {
    Serial.printf("[TLS] handshake with %s failed: %d\n", host, wolfSSL_get_error(c.ssl, hs));
    return c.status = -2;
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
  if (wolfSSL_write(c.ssl, req.c_str(), req.length()) != (int)req.length()) return c.status = -3;

  // read until the end of the headers (HTTP/1.0 -> no chunked encoding)
  const size_t HEAD_CAP = 16384;
  c.head = (char*)heap_caps_malloc(HEAD_CAP + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!c.head) return c.status = -5;
  size_t len = 0;
  char* end = nullptr;
  while (!end && len < HEAD_CAP) {
    int n = wolfSSL_read(c.ssl, c.head + len, (int)(HEAD_CAP - len));
    if (n <= 0) break;
    len += n;
    c.head[len] = 0;
    end = strstr(c.head, "\r\n\r\n");
  }
  if (len < 12 || strncmp(c.head, "HTTP/1.", 7) != 0 || !end) return c.status = -4;
  c.status = atoi(c.head + 9);
  for (char* line = strstr(c.head, "\r\n") + 2; line < end; ) {
    char* eol = strstr(line, "\r\n");
    *eol = 0;
    if (strncasecmp(line, "Content-Length:", 15) == 0) c.contentLength = atol(line + 15);
    else if (strncasecmp(line, "Location:", 9) == 0) { c.location = line + 9; c.location.trim(); }
    line = eol + 2;
  }
  c.headPos = end + 4 - c.head;
  c.headLen = len;
  return c.status;
}

// GET a full https:// URL and follow up to 5 redirects. Same return values as httpsOpen().
inline int httpsOpenUrl(HttpsConn& c, String url, const String& auth = "") {
  for (int hop = 0; hop < 6; hop++) {
    if (!url.startsWith("https://")) return c.status = -4;
    int slash = url.indexOf('/', 8);
    String host = slash < 0 ? url.substring(8) : url.substring(8, slash);
    String path = slash < 0 ? String("/") : url.substring(slash);
    int s = httpsOpen(c, host.c_str(), "GET", path, auth);
    bool redirect = (s == 301 || s == 302 || s == 303 || s == 307 || s == 308) && c.location.length();
    if (!redirect) return s;
    url = c.location.startsWith("/") ? "https://" + host + c.location : c.location;
    httpsClose(c);
  }
  return c.status = -4;
}

// One request, whole answer in PSRAM. Errors as httpsOpen().
inline HttpsResponse httpsRequest(const char* host, const char* method, const String& path, const String& auth) {
  HttpsResponse r;
  HttpsConn c;
  r.status = httpsOpen(c, host, method, path, auth);
  if (r.status < 0) { httpsClose(c); return r; }
  size_t cap = c.contentLength > 0 ? (size_t)c.contentLength + 1024 : 16384, len = 0;
  char* buf = (char*)heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { httpsClose(c); r.status = -5; return r; }
  for (;;) {
    if (cap - len < 4096) {
      char* bigger = (char*)heap_caps_realloc(buf, cap * 2 + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!bigger) { heap_caps_free(buf); httpsClose(c); r.status = -5; return r; }
      buf = bigger; cap *= 2;
    }
    int n = httpsRead(c, (uint8_t*)buf + len, cap - len);
    if (n <= 0) break;
    len += n;
  }
  httpsClose(c);
  buf[len] = 0;
  r.body = buf;
  r.bodyLen = len;
  return r;
}
