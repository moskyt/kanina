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
static constexpr unsigned long UPDATE_HTTP_TIMEOUT_MS = 20000;

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
  unsigned long deadline = millis() + UPDATE_HTTP_TIMEOUT_MS;
  while (millis() < deadline) {
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
    client.stop();
    if (!client.connect(u.host.c_str(), 443)) {
      Log.print("UPDATE: connect failed: "); Log.println(u.host);
      return false;
    }
    send_get(client, u.host, u.path);
    if (!read_head(client, out_head)) {
      Log.println("UPDATE: bad HTTP response");
      return false;
    }
    Log.print("UPDATE: status "); Log.println(out_head.status);
    if (out_head.status >= 200 && out_head.status < 300) { return true; }
    if (out_head.status >= 300 && out_head.status < 400 && out_head.location.length() > 0) {
      url = out_head.location;
      continue;
    }
    Log.print("UPDATE: unexpected status "); Log.println(out_head.status);
    return false;
  }
  Log.println("UPDATE: too many redirects");
  return false;
}

// Same as open_followed but stops at the first 3xx and returns the Location
// instead of opening the body. Used for the cheap tag-check probe.
static bool probe_redirect(WiFiSSLClient& client, const String& url, String& out_location) {
  UrlParts u;
  if (!parse_https_url(url, u)) { return false; }
  Log.print("UPDATE: PROBE "); Log.print(u.host); Log.println(u.path);
  client.stop();
  if (!client.connect(u.host.c_str(), 443)) {
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
    ssl.stop();
    return false;
  }

  Log.print("UPDATE: downloading "); Log.print(head.content_length);
  Log.print(" bytes for "); Log.println(tag);

  // From here on, the heater/pump must be off; the apply() will reset the board.
  mx__shutdown();
  global_state = s_update;
  neo_update();

  if (InternalStorage.open(head.content_length) != 1) {
    Log.println("UPDATE: InternalStorage.open failed");
    ssl.stop();
    return false;
  }

  uint8_t buf[256];
  long remaining = head.content_length;
  unsigned long deadline = millis() + UPDATE_HTTP_TIMEOUT_MS;
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
    } else if (!ssl.connected()) {
      break;
    } else if (millis() > deadline) {
      Log.println("UPDATE: download timeout");
      InternalStorage.close();
      ssl.stop();
      return false;
    }
  }

  InternalStorage.close();
  ssl.stop();

  if (remaining != 0) {
    Log.print("UPDATE: short read, "); Log.print(remaining); Log.println(" bytes missing");
    return false;
  }

  Log.println("UPDATE: applying — board will reset");
  delay(100);
  InternalStorage.apply(); // reboots into the new firmware; does not return
  return true;
}

// Entry point. `verbose=true` logs the no-update path; called from setup_net().
void check_for_update(bool verbose) {
  if (!wifi_ready) {
    if (verbose) { Log.println("UPDATE: WiFi not ready, skip"); }
    global_state = s_idle;
    neo_idle();
    return;
  }

  // TLS handshakes and the first SPI-mediated read from the ESP32-S3 coproc
  // can block inside WiFiSSLClient longer than the 5 s app-level WDT. Widen
  // the WDT for the update window; if apply() succeeds the board resets and
  // setup() restores the 5 s value, if it fails we restore it explicitly.
  WDT.begin(30000);
  global_state = s_update;
  neo_update();

  String tag;
  if (!latest_tag(tag)) {
    Log.println("UPDATE: could not resolve latest tag");
  } else if (tag == FIRMWARE_VERSION) {
    if (verbose) {
      Log.print("UPDATE: already on latest ("); Log.print(tag); Log.println(")");
    }
  } else {
    Log.print("UPDATE: new version available "); Log.print(tag);
    Log.print(" (have "); Log.print(FIRMWARE_VERSION); Log.println(")");
    download_and_apply(tag); // on success this never returns (apply() resets)
  }

  // Any return path lands here: restore normal operation.
  WDT.begin(5000);
  global_state = s_idle;
  neo_idle();
}
