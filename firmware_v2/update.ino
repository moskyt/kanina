#include <WiFiS3.h>
#include <WiFiSSLClient.h>
#include <ArduinoOTA.h>

#include "update.h"

// Polled firmware update from GitHub Releases.
//
// We do not parse JSON; instead we ride GitHub's redirect chain:
//
//   GET https://github.com/<owner>/<repo>/releases/latest
//        -> 302 Location: .../releases/tag/<TAG>            (tag check)
//
//   GET https://github.com/<owner>/<repo>/releases/latest/download/<asset>
//        -> 302 .../releases/download/<TAG>/<asset>
//        -> 302 https://objects.githubusercontent.com/...    (signed URL)
//        -> 200 + binary body                                (flash it)

static constexpr int   UPDATE_MAX_REDIRECTS = 4;
static constexpr int   UPDATE_CONNECT_RETRIES = 3;
// Per-attempt TLS connect deadline, enforced by the ESP32 co-processor. MUST
// stay below the WiFiS3 modem's 10 s buf_read window: otherwise a stalled
// handshake makes the host give up while the co-processor is still working,
// desyncing the AT protocol and wedging every later modem call (the original
// "stuck on found vX forever" hang). Without this the connect uses the
// no-timeout +SSLCLIENTCONNECTNAME command and can never self-heal.
static constexpr int   UPDATE_CONNECT_TIMEOUT_MS = 8000;
static constexpr unsigned long UPDATE_HTTP_TIMEOUT_MS = 20000;
static constexpr unsigned long UPDATE_OLED_PROGRESS_MS = 500;
// Absolute ceiling for a single header line: read_line's inactivity timeout is
// reset on every byte, so a server that trickles bytes forever would otherwise
// never return. This caps the total time spent on one line.
static constexpr unsigned long UPDATE_LINE_MAX_MS = 30000;
// Absolute ceiling for the whole body download, independent of the per-chunk
// inactivity timeout. Stops a slow-drip transfer from running indefinitely.
static constexpr unsigned long UPDATE_DOWNLOAD_MAX_MS = 180000;

// Retrying SSL connect — the Azure-backed asset host (release-assets.*) is
// flaky on first contact, and the WiFiS3 stack sometimes needs a second try.
static bool ssl_connect(WiFiSSLClient& client, const char* host, uint16_t port) {
  // Bound the handshake on the co-processor so connect() returns instead of
  // wedging the modem when the CDN is slow. See UPDATE_CONNECT_TIMEOUT_MS.
  client.setConnectionTimeout(UPDATE_CONNECT_TIMEOUT_MS);
  for (int attempt = 1; attempt <= UPDATE_CONNECT_RETRIES; attempt++) {
    char detail[48];
    snprintf(detail, sizeof(detail), "connect %d/%d\n%s",
             attempt, UPDATE_CONNECT_RETRIES, host);
    oled_phase("Update", detail);
    client.stop();
    WDT.refresh();
    if (client.connect(host, port)) { return true; }
    Log.print("UPDATE: connect attempt "); Log.print(attempt);
    Log.print(" failed for "); Log.println(host);
    if (attempt < UPDATE_CONNECT_RETRIES) {
      WDT.refresh();
      delay(1000);
    }
  }
  return false;
}

// --- OLED status helpers (the main loop isn't drawing during update) -------

static void oled_status(const char* line1, const char* line2 = nullptr) {
  oled_display.clearDisplay();
  oled_display.setTextColor(SSD1306_WHITE);
  oled_display.setTextSize(1);
  oled_display.setCursor(0, 0);
  oled_display.println(line1);
  if (line2) {
    oled_display.setTextSize(1);
    oled_display.setCursor(0, 24);
    oled_display.println(line2);
  }
  oled_display.display();
}

// A spinner that advances on every call. The connect/redirect/read steps each
// block (we can't animate mid-call), but painting this before each hop and each
// retry turns the previously-silent "found vX" gap into visible motion, so a
// slow-but-working update can be told apart from a wedged one.
static const char UPDATE_SPIN[4] = { '|', '/', '-', '\\' };
static uint8_t update_spin_i = 0;

static void oled_phase(const char* title, const char* detail) {
  oled_display.clearDisplay();
  oled_display.setTextColor(SSD1306_WHITE);

  oled_display.setTextSize(1);
  oled_display.setCursor(0, 0);
  oled_display.print(title);
  // spinner glyph in the top-right of the title row
  oled_display.setCursor(SCREEN_WIDTH - 12, 0);
  oled_display.print(UPDATE_SPIN[update_spin_i++ & 3]);

  if (detail) {
    oled_display.setTextSize(1);
    oled_display.setCursor(0, 24);
    oled_display.println(detail);  // GFX wraps long hostnames automatically
  }
  oled_display.display();
}

static void oled_progress(long got, long total) {
  oled_display.clearDisplay();
  oled_display.setTextColor(SSD1306_WHITE);

  oled_display.setTextSize(2);
  oled_display.setCursor(0, 0);
  oled_display.println("Update");

  int pct = (total > 0) ? (int)(100L * got / total) : 0;
  if (pct > 100) { pct = 100; }
  char buf[24];
  snprintf(buf, sizeof(buf), "%d%% %ld/%ld", pct, got, total);
  oled_display.setTextSize(1);
  oled_display.setCursor(0, 24);
  oled_display.println(buf);

  int bar_w = (SCREEN_WIDTH - 4) * pct / 100;
  oled_display.drawRect(0, 40, SCREEN_WIDTH, 12, SSD1306_WHITE);
  if (bar_w > 0) { oled_display.fillRect(2, 42, bar_w, 8, SSD1306_WHITE); }
  oled_display.display();
}

static bool parse_https_url(const String& url, UrlParts& out) {
  const char* prefix = "https://";
  if (!url.startsWith(prefix)) { return false; }
  int host_start = strlen(prefix);
  int path_start = url.indexOf('/', host_start);
  if (path_start < 0) {
    out.host = url.substring(host_start);
    out.path = "/";
  } else {
    out.host = url.substring(host_start, path_start);
    out.path = url.substring(path_start);
  }
  return true;
}

static void send_get(WiFiSSLClient& c, const String& host, const String& path) {
  c.print("GET "); c.print(path); c.print(" HTTP/1.1\r\n");
  c.print("Host: "); c.print(host); c.print("\r\n");
  c.print("User-Agent: kanina-fw/"); c.print(FIRMWARE_VERSION); c.print("\r\n");
  c.print("Accept: */*\r\n");
  c.print("Connection: close\r\n");
  c.print("\r\n");
}

// Read one CRLF-terminated header line. Returns "" on timeout / EOF.
static String read_line(WiFiSSLClient& c) {
  String line;
  unsigned long deadline = millis() + UPDATE_HTTP_TIMEOUT_MS;  // inactivity
  unsigned long hard = millis() + UPDATE_LINE_MAX_MS;          // absolute cap
  while (millis() < deadline && millis() < hard) {
    WDT.refresh();
    if (c.available()) {
      int ch = c.read();
      if (ch < 0) { break; }
      if (ch == '\n') {
        if (line.endsWith("\r")) { line.remove(line.length() - 1); }
        return line;
      }
      line += (char) ch;
      deadline = millis() + UPDATE_HTTP_TIMEOUT_MS;
    } else if (!c.connected()) {
      break;
    }
  }
  return line;
}

static bool read_head(WiFiSSLClient& c, HttpHead& out) {
  out.status = 0;
  out.location = "";
  out.content_length = -1;

  String status_line = read_line(c);
  if (status_line.length() == 0) { return false; }
  int sp1 = status_line.indexOf(' ');
  int sp2 = (sp1 >= 0) ? status_line.indexOf(' ', sp1 + 1) : -1;
  if (sp1 < 0 || sp2 < 0) { return false; }
  out.status = status_line.substring(sp1 + 1, sp2).toInt();

  while (true) {
    String line = read_line(c);
    if (line.length() == 0) { break; }
    String lower = line; lower.toLowerCase();
    if (lower.startsWith("location:")) {
      out.location = line.substring(9); out.location.trim();
    } else if (lower.startsWith("content-length:")) {
      out.content_length = line.substring(15).toInt();
    }
  }
  return true;
}

// Resolve the redirect chain starting at `start_url`. Returns the final 200-OK
// Location on success (with `out_*` populated and `out_client` still holding the
// body), or false on failure. Caller owns out_client.
static bool open_followed(WiFiSSLClient& client, const String& start_url, HttpHead& out_head) {
  String url = start_url;
  for (int hop = 0; hop < UPDATE_MAX_REDIRECTS; hop++) {
    UrlParts u;
    if (!parse_https_url(url, u)) {
      Log.print("UPDATE: bad URL: "); Log.println(url);
      return false;
    }
    Log.print("UPDATE: GET "); Log.print(u.host); Log.println(u.path);
    char detail[48];
    snprintf(detail, sizeof(detail), "fetch [%d]\n%s", hop + 1, u.host.c_str());
    oled_phase("Update", detail);
    if (!ssl_connect(client, u.host.c_str(), 443)) {
      Log.print("UPDATE: connect failed: "); Log.println(u.host);
      oled_status("Update failed", "no connection");
      return false;
    }
    send_get(client, u.host, u.path);
    if (!read_head(client, out_head)) {
      Log.println("UPDATE: bad HTTP response");
      oled_status("Update failed", "no response");
      return false;
    }
    Log.print("UPDATE: status "); Log.println(out_head.status);
    if (out_head.status >= 200 && out_head.status < 300) { return true; }
    if (out_head.status >= 300 && out_head.status < 400 && out_head.location.length() > 0) {
      url = out_head.location;
      continue;
    }
    Log.print("UPDATE: unexpected status "); Log.println(out_head.status);
    char sbuf[24];
    snprintf(sbuf, sizeof(sbuf), "HTTP %d", out_head.status);
    oled_status("Update failed", sbuf);
    return false;
  }
  Log.println("UPDATE: too many redirects");
  oled_status("Update failed", "too many hops");
  return false;
}

// Same as open_followed but stops at the first 3xx and returns the Location
// instead of opening the body. Used for the cheap tag-check probe.
static bool probe_redirect(WiFiSSLClient& client, const String& url, String& out_location) {
  UrlParts u;
  if (!parse_https_url(url, u)) { return false; }
  Log.print("UPDATE: PROBE "); Log.print(u.host); Log.println(u.path);
  if (!ssl_connect(client, u.host.c_str(), 443)) {
    Log.print("UPDATE: connect failed: "); Log.println(u.host);
    return false;
  }
  send_get(client, u.host, u.path);
  HttpHead head;
  if (!read_head(client, head)) { return false; }
  client.stop();
  if (head.status >= 300 && head.status < 400 && head.location.length() > 0) {
    out_location = head.location;
    return true;
  }
  Log.print("UPDATE: probe got status "); Log.println(head.status);
  return false;
}

// Extract the tag from a redirect URL like ".../releases/tag/<TAG>".
// Returns "" when the redirect target doesn't look like a tag URL (e.g.
// the repo has no releases yet and GitHub bounces us to /releases).
static String tag_from_location(const String& location) {
  const char* marker = "/releases/tag/";
  int at = location.indexOf(marker);
  if (at < 0) { return ""; }
  String tag = location.substring(at + strlen(marker));
  int cut = tag.indexOf('?'); if (cut >= 0) { tag = tag.substring(0, cut); }
  cut = tag.indexOf('/');     if (cut >= 0) { tag = tag.substring(0, cut); }
  cut = tag.indexOf('#');     if (cut >= 0) { tag = tag.substring(0, cut); }
  return tag;
}

// Look up the latest release tag without downloading the binary.
static bool latest_tag(String& out_tag) {
  String url = String("https://github.com/") +
               config__github_owner + "/" + config__github_repo +
               "/releases/latest";
  WiFiSSLClient ssl;
  String location;
  if (!probe_redirect(ssl, url, location)) { return false; }
  out_tag = tag_from_location(location);
  if (out_tag.length() == 0) {
    Log.print("UPDATE: no release found (redirect went to ");
    Log.print(location);
    Log.println(")");
    return false;
  }
  return true;
}

static bool download_and_apply(const String& tag) {
  String url = String("https://github.com/") +
               config__github_owner + "/" + config__github_repo +
               "/releases/latest/download/" + config__github_asset;

  WiFiSSLClient ssl;
  HttpHead head;
  if (!open_followed(ssl, url, head)) {
    ssl.stop();
    return false;
  }
  if (head.content_length <= 0) {
    Log.println("UPDATE: missing Content-Length, refusing to flash");
    oled_status("Update failed", "no length");
    ssl.stop();
    return false;
  }

  Log.print("UPDATE: downloading "); Log.print(head.content_length);
  Log.print(" bytes for "); Log.println(tag);

  // The R4's OTA scheme stages the image in the upper half of flash and copies
  // it down over the running sketch at apply(), so the largest OTA-flashable
  // image is half the usable flash (~122 KB). InternalStorage.open() returns 0
  // for an oversize image with no way to tell that apart from a real flash
  // fault, so check up front and say so plainly. USB upload has no such limit
  // (full ~240 KB region), which is why a too-big build still flashes over USB.
  long ota_max = InternalStorage.maxSize();
  if (head.content_length > ota_max) {
    Log.print("UPDATE: image too big for OTA — "); Log.print(head.content_length);
    Log.print(" > "); Log.print(ota_max); Log.println(" (flash over USB)");
    char msg[24];
    snprintf(msg, sizeof(msg), "%ldk > %ldk", head.content_length / 1024, ota_max / 1024);
    oled_status("Too big for OTA", msg);
    ssl.stop();
    delay(4000);
    return false;
  }

  // From here on, the heater/pump must be off; the apply() will reset the board.
  mx__shutdown();
  global_state = s_update;
  neo_update();
  oled_progress(0, head.content_length);

  if (InternalStorage.open(head.content_length) != 1) {
    Log.println("UPDATE: InternalStorage.open failed");
    oled_status("Update failed", "storage open");
    ssl.stop();
    return false;
  }

  uint8_t buf[256];
  long remaining = head.content_length;
  unsigned long deadline = millis() + UPDATE_HTTP_TIMEOUT_MS;  // inactivity
  unsigned long hard = millis() + UPDATE_DOWNLOAD_MAX_MS;      // absolute cap
  unsigned long last_progress = 0;
  while (remaining > 0) {
    WDT.refresh();
    int avail = ssl.available();
    if (avail > 0) {
      int want = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
      if ((long)want > remaining) { want = (int)remaining; }
      int n = ssl.read(buf, want);
      if (n <= 0) { continue; }
      for (int i = 0; i < n; i++) { InternalStorage.write(buf[i]); }
      remaining -= n;
      deadline = millis() + UPDATE_HTTP_TIMEOUT_MS;
      if (millis() - last_progress > UPDATE_OLED_PROGRESS_MS) {
        oled_progress(head.content_length - remaining, head.content_length);
        last_progress = millis();
      }
    } else if (!ssl.connected()) {
      break;
    } else if (millis() > deadline || millis() > hard) {
      Log.println("UPDATE: download timeout");
      oled_status("Update failed", "timeout");
      InternalStorage.close();
      ssl.stop();
      return false;
    }
  }

  InternalStorage.close();
  ssl.stop();

  if (remaining != 0) {
    Log.print("UPDATE: short read, "); Log.print(remaining); Log.println(" bytes missing");
    oled_status("Update failed", "short read");
    return false;
  }

  oled_progress(head.content_length, head.content_length);
  oled_status("Update", "Applying and resetting...");
  Log.println("UPDATE: applying — board will reset");
  delay(100);
  InternalStorage.apply(); // reboots into the new firmware; does not return
  return true;
}

// Entry point. `verbose=true` logs the no-update path; called from setup_net()
// before the WDT is enabled (TLS handshakes block longer than the IWDT max).
// Also exposed to the CLI; note that the CLI path runs with WDT already
// running at 5 s — long blocks inside WiFiSSLClient will reset the board.
void check_for_update(bool verbose) {
  if (!wifi_ready) {
    if (verbose) { Log.println("UPDATE: WiFi not ready, skip"); }
    return;
  }

  global_state = s_update;
  neo_update();
  oled_status("Update", "checking...");

  String tag;
  if (!latest_tag(tag)) {
    Log.println("UPDATE: could not resolve latest tag");
    oled_status("Update", "no tag");
    delay(1500);
  } else if (tag == FIRMWARE_VERSION) {
    if (verbose) {
      Log.print("UPDATE: already on latest ("); Log.print(tag); Log.println(")");
    }
    oled_status("Up to date", FIRMWARE_VERSION);
    delay(1500);
  } else {
    Log.print("UPDATE: new version available "); Log.print(tag);
    Log.print(" (have "); Log.print(FIRMWARE_VERSION); Log.println(")");
    char msg[24];
    snprintf(msg, sizeof(msg), "found %s", tag.c_str());
    oled_status("Update", msg);
    delay(800); // let the user see which version we're fetching
    download_and_apply(tag); // on success this never returns (apply() resets)
    // if we get here the download failed; download_and_apply() has already
    // painted the specific reason — hold it long enough to read.
    delay(4000);
  }

  perform__idle();
}
