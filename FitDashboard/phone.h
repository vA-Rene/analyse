/*
 * File: phone.h
 * Purpose: The phone as the microphone of the coach, and as its way to the AI service.
 *
 * KEY1 on the coach screen opens a window: the display joins the home Wi-Fi and serves one web
 * page. On that page the keyboard of the phone turns speech into text (its microphone key) and the
 * page sends the text. The display then builds the request for the AI service (coach.h), but does
 * not send it: it has no key. The page fetches the request, sends it to the AI service with the key
 * it keeps in the browser of the phone, and brings the reply back. The display checks the reply,
 * may hand out a corrected request, and draws the answer. No audio and no key reach the display.
 *
 *   page()            the web page: text field, send button, the fixed questions to tap, the key
 *                     (saved in the browser, with a button that deletes it)
 *   handle()          every HTTP request of the window: GET /, GET /ping, POST /ask, GET /job, POST /answer
 *   cleanQuestion()   what arrives over the network, made safe to draw and to send on
 *   introScreen(), waitScreen(), workingScreen(), lostScreen()   what the display shows around the window
 *
 * The HTTP server is not in here: the firmware feeds the requests of its WebServer into handle(),
 * the PC emulator those of a small socket server. So this file has no Arduino dependency.
 *
 * Without the page (a shortcut app on the phone), every request with the header "X-Fitdash":
 *   POST /ask     header value "1", the question as form field "q", as plain text or as JSON {"q": "..."}
 *                 -> {"ok":true,"id":"..."}; the id is the header value from here on
 *   GET /job      -> {"wait":true} | {"job":N,"request":{...}} | {"done":true,"answered":true|false}
 *                 A request goes as JSON to https://COACH_HOST COACH_PATH (coach.h), header "x-goog-api-key".
 *   POST /answer  text/plain: first line "<N> <HTTP status>", then the reply of the AI service as it came
 */
#pragma once
#include <stdio.h>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "kit.h"
#include "coach.h"

#ifndef PHONE_HOSTNAME
#define PHONE_HOSTNAME      "fitdash"
#endif
#ifndef PHONE_WINDOW_MS
#define PHONE_WINDOW_MS     (90UL * 1000UL)
#endif
#ifndef PHONE_WINDOW_MAX_MS
#define PHONE_WINDOW_MAX_MS (5UL * 60UL * 1000UL)
#endif
#ifndef PHONE_QUESTION_MAX
#define PHONE_QUESTION_MAX  300
#endif
#ifndef PHONE_FETCH_MS
#define PHONE_FETCH_MS      (20UL * 1000UL)    // the phone has to pick up a request for the AI service within this time
#endif
#ifndef PHONE_ANSWER_MS
#define PHONE_ANSWER_MS     (200UL * 1000UL)   // and to bring the reply within this one (the page gives up after 2 x 90 s)
#endif
// The page sends this header with every request. A page of another website cannot: the browser asks
// the display for permission first (CORS preflight) and gets none. So only this page and apps on
// the phone can post a question, not a website the phone happens to have open. After the question
// the header carries the id that only the asking phone got: no other device gets the requests.
#define PHONE_HEADER        "X-Fitdash"
// Result of the coach's PostFn when no reply came: the phone is gone, or a key on the display stopped it.
#define PHONE_LOST          (-2)

namespace phone {

// One listening window, and after the question the work on it. Times are millis(); the differences
// survive the counter's overflow.
struct Window {
  unsigned long opened = 0, seen = 0;   // when it opened, when the phone last showed up
  std::string question;                 // filled once, by the first question that arrives
  std::string id;                       // what the asking phone sends in PHONE_HEADER from then on
  // The request for the AI service that is on offer (the coach asks up to COACH_ROUNDS times, one after the other)
  int job = 0;                          // its number, from 1
  std::string request;                  // its body, as coach.h built it
  unsigned long offered = 0;            // when it was offered; from the pick-up on: when it was picked up
  bool taken = false, answered = false; // the phone picked it up, the phone brought the reply
  int status = 0;                       // HTTP status of the AI service, -1 = the phone did not reach it
  std::string reply;
  int done = 0;                         // 1 = the answer is drawn, 2 = the screen that says why there is none
  bool told = false;                    // the phone has heard that it is done

  // `random`: 64 bits nobody can guess (esp_random on the display)
  void open(unsigned long now, unsigned long long random) {
    opened = seen = now;
    question.clear(); request.clear(); reply.clear();
    job = status = done = 0;
    taken = answered = told = false;
    char b[20];
    snprintf(b, sizeof(b), "%016llx", random);
    id = b;
  }
  void touch(unsigned long now) { seen = now; }
  long left(unsigned long now) const {  // milliseconds until it closes, 0 = closed
    unsigned long quiet = now - seen, total = now - opened;
    if (quiet >= PHONE_WINDOW_MS || total >= PHONE_WINDOW_MAX_MS) return 0;
    unsigned long a = PHONE_WINDOW_MS - quiet, b = PHONE_WINDOW_MAX_MS - total;
    return (long)(a < b ? a : b);
  }
  bool isOpen(unsigned long now) const { return left(now) > 0; }

  // Puts a request for the AI service on offer for the phone.
  void offer(const std::string& body, unsigned long now) {
    job++;
    request = body;
    offered = now;
    taken = answered = false;
    status = 0;
    reply.clear();
  }
  // Still worth waiting for the reply to the request on offer.
  bool waiting(unsigned long now) const { return !answered && now - offered < (taken ? PHONE_ANSWER_MS : PHONE_FETCH_MS); }
  void finish(bool drawn) { done = drawn ? 1 : 2; request.clear(); }
};

// A question as it arrives over the network, made safe to draw and to send on: valid UTF-8 only,
// line breaks and other invisible characters as spaces, one space between words, at most
// PHONE_QUESTION_MAX characters.
inline std::string cleanQuestion(const std::string& raw) {
  static const unsigned least[5] = {0, 0, 0x80, 0x800, 0x10000};
  std::string out;
  int count = 0;
  bool space = false;
  const unsigned char* s = (const unsigned char*)raw.data();
  size_t n = raw.size();
  for (size_t i = 0; i < n && count < PHONE_QUESTION_MAX;) {
    unsigned c = s[i];
    int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    bool ok = len > 0 && i + len <= n;
    if (ok && len > 1) {
      c &= 0xFFu >> (len + 1);
      for (int k = 1; k < len; k++) {
        if ((s[i + k] & 0xC0) != 0x80) ok = false;
        c = (c << 6) | (s[i + k] & 0x3F);
      }
      ok = ok && c >= least[len] && c <= 0x10FFFF && (c < 0xD800 || c > 0xDFFF);
    }
    if (!ok) { i++; continue; }   // not UTF-8: drop the byte
    bool blank = c <= 0x20 || (c >= 0x7F && c <= 0xA0) || (c >= 0x2000 && c <= 0x200F) || (c >= 0x2028 && c <= 0x202F) || c == 0xFEFF;
    if (blank) space = !out.empty();
    else {
      if (space) { out += ' '; count++; space = false; }
      out.append(raw, i, len);
      count++;
    }
    i += len;
  }
  return out;
}

// The question out of a request: the form field "q" (what the page sends), or the body itself, as
// plain text or as JSON {"q": "..."} (shortcut apps).
inline std::string questionFrom(const std::string& q, const std::string& body) {
  if (!q.empty()) return cleanQuestion(q);
  size_t a = body.find_first_not_of(" \t\r\n");
  if (a == std::string::npos || body[a] != '{') return cleanQuestion(body);
  JsonDocument doc;
  if (deserializeJson(doc, body)) return "";
  const char* text = doc["q"] | "";
  if (!*text) text = doc["question"] | "";
  return cleanQuestion(text);
}

inline std::string escapeHtml(const std::string& v) {
  std::string o;
  for (char c : v) {
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;"; else o += c;
  }
  return o;
}

// The page. `picks` are questions to send with one tap (the fixed questions of config.h).
// Its script is the courier between the display and the AI service: see the file header.
inline std::string page(const char* const* picks, int pickN) {
  char limit[12];
  snprintf(limit, sizeof(limit), "%d", PHONE_QUESTION_MAX);
  std::string p;
  p.reserve(11000);
  p += R"HTML(<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Coach</title>
<style>
:root{color-scheme:light dark;--bg:#f4f4f1;--card:#fff;--ink:#14171a;--mute:#5b626a;--line:#cfd1cc;--go:#14171a;--goink:#fff;--bad:#a3241c}
@media(prefers-color-scheme:dark){:root{--bg:#121416;--card:#1c1f22;--ink:#eceeef;--mute:#a3aab2;--line:#3a4046;--go:#eceeef;--goink:#14171a;--bad:#ff8f85}}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:520px;margin:0 auto;padding:24px 16px 40px}
h1{font-size:24px;line-height:1.2;margin:0 0 6px}
p{margin:0 0 16px;color:var(--mute)}
textarea,input{display:block;width:100%;padding:12px;border:1px solid var(--line);border-radius:12px;background:var(--card);color:var(--ink);font:inherit}
textarea{min-height:132px;font-size:18px;resize:vertical}
button{font:inherit;border-radius:12px;cursor:pointer}
textarea:focus-visible,input:focus-visible,button:focus-visible{outline:2px solid var(--ink);outline-offset:2px}
#send{width:100%;margin-top:12px;padding:14px;border:0;background:var(--go);color:var(--goink);font-size:18px;font-weight:600}
#send:disabled{opacity:.45}
#state{min-height:24px;margin:14px 0 0;color:var(--ink)}
#state.bad{color:var(--bad)}
h2{font-size:13px;font-weight:600;letter-spacing:.06em;text-transform:uppercase;color:var(--mute);margin:28px 0 0}
.pick{display:block;width:100%;margin-top:8px;padding:12px;border:1px solid var(--line);background:var(--card);color:var(--ink);text-align:left}
#keybox{margin:0 0 20px;padding:16px;border:1px solid var(--line);border-radius:12px;background:var(--card)}
#keybox input{background:var(--bg)}
label{display:block;font-weight:600;margin:0 0 6px}
#keep{width:100%;margin-top:10px;padding:12px;border:1px solid var(--ink);background:transparent;color:var(--ink);font-weight:600}
.note{font-size:14px;margin:10px 0 0}
#keyrow{margin-top:32px;padding-top:6px;border-top:1px solid var(--line)}
#forget{margin-top:10px;padding:10px 14px;border:1px solid var(--bad);background:transparent;color:var(--bad)}
</style>
<main>
<h1>Ask your coach</h1>
<p>Tap the text field, then the microphone on your keyboard, and speak. The answer appears on the display.</p>
<div id="keybox" hidden>
<label for="k">Your Gemini API key</label>
<input id="k" type="password" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="Paste the key here">
<button id="keep" type="button">Save key on this phone</button>
<p class="note">This phone asks Google with your key. The key stays in this browser, saved for this address, and goes only to Google, never to the display. You can delete it here at any time.</p>
</div>
<form id="f">
<textarea id="q" aria-label="Your question" placeholder="Your question" enterkeyhint="send" maxlength=")HTML";
  p += limit;
  p += R"HTML("></textarea>
<button id="send" type="submit">Send to the display</button>
</form>
<p id="state" role="status" aria-live="polite"></p>
)HTML";
  if (pickN > 0) p += "<h2>Or tap a question</h2>\n";
  for (int i = 0; i < pickN; i++) p += "<button class=\"pick\" type=\"button\">" + escapeHtml(picks[i]) + "</button>\n";
  p += R"HTML(<div id="keyrow" hidden>
<p class="note">Your Gemini key is saved in this browser. It goes only to Google, never to the display.</p>
<button id="forget" type="button">Delete key from this phone</button>
</div>
</main>
<script>
(function () {
  function el(i) { return document.getElementById(i); }
  var form = el('f'), field = el('q'), button = el('send'), state = el('state'), keyBox = el('keybox'), keyField = el('k'), keyRow = el('keyrow');
  var HEAD = ')HTML" PHONE_HEADER R"HTML(', AI = 'https://)HTML" COACH_HOST COACH_PATH R"HTML(', STORE = 'coach-key';
  var key = '', id = '1', busy = false, lastPing = 0, trouble = '';
  var GONE = 'The display is not listening. Press KEY1 on it, wait until it shows this address, then send again. Your text stays here.';
  var LOST = 'The display stopped answering. Look at it: the answer may already be there.';
  function say(text, bad) { state.textContent = text; state.className = bad ? 'bad' : ''; }
  function pause(ms) { return new Promise(function (go) { setTimeout(go, ms); }); }
  function within(ms, o) {
    if (window.AbortController) {
      var stop = new AbortController();
      o.signal = stop.signal;
      setTimeout(function () { stop.abort(); }, ms);
    }
    return o;
  }

  // ---- the key: only in this browser
  function showKey() { keyBox.hidden = !!key; keyRow.hidden = !key; }
  function saveKey() {
    var v = keyField.value.replace(/\s+/g, ''), kept = false;
    if (!v) { say('Paste your Gemini key into the field first.', true); return; }
    key = v;
    keyField.value = '';
    try { localStorage.setItem(STORE, v); kept = localStorage.getItem(STORE) === v; } catch (e) {}
    showKey();
    say(kept ? 'Key saved on this phone.' : 'This browser does not keep the key. It is gone when you close this page.', !kept);
  }
  function deleteKey() {
    var gone = true;
    key = '';
    try { localStorage.removeItem(STORE); gone = localStorage.getItem(STORE) == null; } catch (e) {}
    showKey();
    say(gone ? 'Key deleted from this phone.' : 'The browser did not delete the key. Clear the data of this page in its settings.', !gone);
  }
  try { key = localStorage.getItem(STORE) || ''; } catch (e) {}
  showKey();
  el('keep').addEventListener('click', saveKey);
  keyField.addEventListener('keydown', function (e) { if (e.key === 'Enter') { e.preventDefault(); saveKey(); } });
  el('forget').addEventListener('click', deleteKey);

  // ---- one request to the display
  function call(path, body, type, ms) {
    var o = { method: body == null ? 'GET' : 'POST', headers: {}, cache: 'no-store' };
    o.headers[HEAD] = id;
    if (body != null) { o.headers['Content-Type'] = type; o.body = body; }
    return fetch(path, within(ms || 6000, o)).then(function (r) { return r.json(); });
  }

  // ---- the way to the AI service: the display builds each request, this page sends it with the key
  function why(status, text) {
    var m = '';
    try { m = JSON.parse(text).error.message || ''; } catch (e) {}
    if (m.length > 160) m = m.slice(0, 160) + ' ...';
    if (!status) return 'Google did not answer. Is this phone online?';
    if (status === 429) return 'The limit of your Gemini key is used up for now (HTTP 429).';
    return 'Google refused (HTTP ' + status + ')' + (m ? ': ' + m : '.') +
           (status === 400 || status === 401 || status === 403 ? ' If the key is wrong, delete it below and save the right one.' : '');
  }
  function bring(job, status, text, again) {
    if (status === 200) say('The display is checking the answer ...');
    return call('/answer', job + ' ' + status + '\n' + text, 'text/plain', 10000).catch(function () {
      return again ? null : pause(1000).then(function () { return bring(job, status, text, true); });
    });
  }
  function relay(j, again) {
    if (!key) { trouble = 'The key was deleted, so nothing went to Google.'; return bring(j.job, 0, ''); }
    if (!again) say(j.job > 1 ? 'The display found problems in the answer. Asking Google again (try ' + j.job + ') ...' : 'Asking Google ...');
    var status = 0;
    var o = within(90000, { method: 'POST', headers: { 'x-goog-api-key': key, 'Content-Type': 'application/json' }, body: JSON.stringify(j.request) });
    return fetch(AI, o).then(function (r) { status = r.status; return r.text(); }).catch(function () { status = 0; return ''; }).then(function (text) {
      if (status === 503 && !again) {   // overloaded for a moment; every try counts against the quota
        say('Google is busy. Trying once more in a few seconds ...');
        return pause(8000).then(function () { return relay(j, true); });
      }
      if (status !== 200) trouble = why(status, text);
      return bring(j.job, status, text);
    });
  }
  // Asks the display what to do next until it has drawn. `misses`: requests in a row it did not answer
  // (it is deaf for a few seconds while it refreshes its panel).
  function work(misses) {
    return call('/job', null, null, 8000).then(function (j) {
      if (j.done) {
        if (trouble) say(trouble, true);
        else if (j.answered) say('Done. The answer is on the display. For another question press KEY1 on it again.');
        else say('The display could not draw a clean answer. It says why.', true);
        return;
      }
      if (j.request) return relay(j).then(function () { return work(0); });
      if (!j.ok) { say(GONE, true); return; }
      return pause(600).then(function () { return work(0); });
    }, function () {
      if (misses >= 3) { say(trouble || LOST, true); return; }
      return pause(1200).then(function () { return work(misses + 1); });
    });
  }

  function send(text) {
    text = text.trim();
    if (!text || busy) return;
    if (!key) { say('Save your Gemini key first.', true); keyField.focus(); return; }
    busy = true;
    button.disabled = true;
    trouble = '';
    id = '1';
    say('Sending ...');
    call('/ask', 'q=' + encodeURIComponent(text), 'application/x-www-form-urlencoded').then(function (j) {
      if (j.ok) {
        id = j.id;
        field.value = '';
        say('The display has your question ...');
        return work(0);
      }
      if (j.error === 'busy') say('The display is already working on a question.', true);
      else if (j.error === 'empty') say('That was empty. Speak or type a question.', true);
      else say(GONE, true);
    }).catch(function () { say(GONE, true); }).then(function () { busy = false; button.disabled = false; });
  }
  form.addEventListener('submit', function (e) { e.preventDefault(); send(field.value); });
  field.addEventListener('keydown', function (e) { if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); send(field.value); } });
  // while someone speaks or types, tell the display to keep waiting
  field.addEventListener('input', function () {
    var now = Date.now();
    if (now - lastPing > 5000) { lastPing = now; call('/ping').catch(function () {}); }
  });
  [].forEach.call(document.querySelectorAll('.pick'), function (pick) {
    pick.addEventListener('click', function () { send(pick.textContent); });
  });
})();
</script>
</html>
)HTML";
  return p;
}

struct Reply {
  int status = 200;
  const char* type = "application/json";
  std::string body;
};

// Answers one request of the window. `mark` is the value of the header PHONE_HEADER, empty when the
// request has none. `q` is the form field "q", `body` the request body when it was not a form.
// A question that arrives is left in w.question; the caller then lets the coach work, whose requests
// for the AI service go out through w.offer() and come back in w.reply.
inline Reply handle(const std::string& method, const std::string& path, const std::string& mark, const std::string& q, const std::string& body,
                    Window& w, unsigned long now, const char* const* picks, int pickN) {
  Reply r;
  auto fail = [&r](int status, const char* error) { r.status = status; r.body = std::string("{\"ok\":false,\"error\":\"") + error + "\"}"; return r; };
  if (method == "GET" && (path == "/" || path == "/index.html")) {
    w.touch(now);
    r.type = "text/html; charset=utf-8";
    r.body = page(picks, pickN);
    return r;
  }
  if (method == "GET" && path == "/ping") {
    w.touch(now);
    char b[48];
    snprintf(b, sizeof(b), "{\"ok\":true,\"left\":%ld}", w.left(now) / 1000);
    r.body = b;
    return r;
  }
  if (method == "POST" && path == "/ask") {
    if (mark.empty()) return fail(403, "header");
    if (!w.question.empty()) return fail(409, "busy");
    std::string text = questionFrom(q, body);
    w.touch(now);
    if (text.empty()) return fail(400, "empty");
    w.question = text;
    r.body = "{\"ok\":true,\"id\":\"" + w.id + "\"}";
    return r;
  }
  bool asker = !w.question.empty() && !w.id.empty() && mark == w.id;   // the phone that sent the question
  if (method == "GET" && path == "/job") {
    if (!asker) return fail(403, "header");
    w.touch(now);
    if (w.done) {
      w.told = true;
      r.body = std::string("{\"ok\":true,\"done\":true,\"answered\":") + (w.done == 1 ? "true" : "false") + "}";
    } else if (w.job == 0 || w.answered) r.body = "{\"ok\":true,\"wait\":true}";   // the display is still thinking or checking
    else {
      if (!w.taken) { w.taken = true; w.offered = now; }
      r.body = "{\"ok\":true,\"job\":" + std::to_string(w.job) + ",\"request\":" + w.request + "}";
    }
    return r;
  }
  if (method == "POST" && path == "/answer") {
    if (!asker) return fail(403, "header");
    w.touch(now);
    int job = 0, status = 0;
    size_t line = body.find('\n');
    if (line == std::string::npos || sscanf(body.c_str(), "%d %d", &job, &status) != 2) return fail(400, "bad");
    if (w.done || w.answered || job != w.job) return fail(409, "stale");
    w.status = status >= 100 && status <= 599 ? status : -1;
    w.reply = body.substr(line + 1);
    w.answered = true;
    r.body = "{\"ok\":true}";
    return r;
  }
  return fail(404, "not found");
}

// ----------------------------------------------------------------- what the display shows
typedef std::vector<std::string> Warnings;   // of the drawing code; empty = everything fits

// The coach screen before anything was asked.
inline Warnings introScreen(const DashboardData& D, const struct tm& now) {
  JsonDocument d;
  coach::coachFrame(D, now, d["frame"].to<JsonObject>(), "KEY1: ask by phone");
  d["header"]["label"] = "COACH";
  d["header"]["title"] = "Press KEY1 and ask by phone";
  d["content"]["element"] = "text";
  d["content"]["content"] = "The display then shows an address. Open it in the browser of your phone, tap the microphone on "
                            "the keyboard and speak your question. Phone and display must be in the same Wi-Fi.";
  d["sentence"] = "Your phone asks the AI service with your own key. The display holds no key.";
  return kit::render(d.as<JsonVariantConst>());
}

// While the window is open: where the page is. `name` is the address by name ("fitdash.local", empty
// when the name service did not start), `ip` the address by number, which every phone can open.
inline Warnings waitScreen(const DashboardData& D, const struct tm& now, const std::string& name, const std::string& ip) {
  JsonDocument d;
  coach::coachFrame(D, now, d["frame"].to<JsonObject>(), "KEY1: cancel");
  d["header"]["label"] = "COACH";
  d["header"]["title"] = "Waiting for your phone";
  JsonArray rows = d["content"]["rows"].to<JsonArray>();
  JsonObject address = rows.add<JsonObject>();
  address["element"] = "text";
  address["content"] = "http://" + (name.empty() ? ip : name);
  address["size"] = "large";
  address["bold"] = true;
  address["border"] = true;
  address["weight"] = 2;
  if (!name.empty()) {
    JsonObject number = rows.add<JsonObject>();
    number["element"] = "text";
    number["content"] = "If that does not open: http://" + ip;
    number["size"] = "small";
    number["weight"] = 1;
  }
  d["sentence"] = "Open the address in the browser of your phone, tap the microphone on the keyboard and speak.";
  return kit::render(d.as<JsonVariantConst>());
}

// From the arrival of the question until the answer is drawn.
inline Warnings workingScreen(const DashboardData& D, const struct tm& now, const std::string& question) {
  JsonDocument d;
  coach::coachFrame(D, now, d["frame"].to<JsonObject>(), "KEY1: cancel");
  d["header"]["label"] = "COACH";
  d["header"]["title"] = "Working on your question";
  d["content"]["element"] = "text";
  d["content"]["content"] = question;
  d["sentence"] = "Your phone is asking the AI service. Keep its page open until the answer is here.";
  return kit::render(d.as<JsonVariantConst>());
}

// When the phone did not bring a reply (PHONE_LOST). Draws the screen and returns it as a spec for
// coach::coachRedraw, like the coach's own screens.
inline std::string lostScreen(const DashboardData& D, const struct tm& now, const std::string& question, const char* hint) {
  JsonDocument frame;
  coach::coachFrame(D, now, frame.to<JsonObject>(), hint);
  return coach::coachSorry(question, frame.as<JsonObjectConst>(), "Your phone did not bring an answer. Keep its page open until the answer is here.");
}

}  // namespace phone
