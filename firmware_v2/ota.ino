#include <WiFiS3.h>

bool wifi_ready = false;

static WiFiServer telnet_server(config__telnet_port);
static WiFiClient telnet_client;

// Telnet IAC (0xFF) byte introduces a 2-byte option negotiation we want to
// silently swallow rather than feed to the CLI.
static uint8_t telnet_iac_skip = 0;

static void telnet_drop() {
  if (telnet_client) {
    telnet_client.stop();
  }
  telnet_iac_skip = 0;
}

static void telnet_accept() {
  WiFiClient incoming = telnet_server.available();
  if (!incoming) { return; }
  if (telnet_client && telnet_client.connected()) {
    telnet_client.println();
    telnet_client.println("kanina: replaced by new connection");
    telnet_client.stop();
  }
  telnet_client = incoming;
  telnet_iac_skip = 0;
  telnet_client.print("kanina ");
  telnet_client.print(FIRMWARE_VERSION);
  telnet_client.println(" - connected");
}

// Called by LogStream to mirror serial output to the active telnet client.
void telnet_write(const uint8_t* p, size_t n) {
  if (telnet_client && telnet_client.connected()) {
    telnet_client.write(p, n);
  }
}

// Drain any pending telnet input into the CLI, filtering telnet IAC bytes.
void telnet_poll_cli() {
  if (!telnet_client || !telnet_client.connected()) { return; }
  while (telnet_client.available()) {
    int b = telnet_client.read();
    if (b < 0) { break; }
    if (telnet_iac_skip > 0) {
      telnet_iac_skip--;
      continue;
    }
    if (b == 0xFF) {
      telnet_iac_skip = 2;
      continue;
    }
    cli_feed((char) b);
  }
}

static void wifi_connect() {
  Serial.print("INIT WiFi: ");
  Serial.println(config__wifi_ssid);

  WiFi.begin(config__wifi_ssid, config__wifi_password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    if (millis() - start > config__wifi_connect_timeout_ms) {
      Serial.println();
      Serial.println("WiFi connect TIMEOUT.");
      return;
    }
    WDT.refresh();
    delay(250);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected, IP = ");
  Serial.println(WiFi.localIP());

  telnet_server.begin();
  Serial.print("Telnet listening on ");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(config__telnet_port);

  wifi_ready = true;
}

void setup_net() {
  wifi_connect();
  if (wifi_ready) {
    check_for_update(/*verbose=*/true);
  }
}

void loop_net() {
  static unsigned long last_check = 0;
  unsigned long now = millis();

  // every 2s, verify WiFi is still up; rebind telnet if it came back after a drop
  if (now - last_check > 2000) {
    last_check = now;
    bool connected = (WiFi.status() == WL_CONNECTED) && (WiFi.localIP() != IPAddress(0, 0, 0, 0));
    if (wifi_ready && !connected) {
      Serial.println("WiFi dropped.");
      telnet_drop();
      wifi_ready = false;
    } else if (!wifi_ready && connected) {
      Serial.print("WiFi back at ");
      Serial.println(WiFi.localIP());
      telnet_server.begin();
      wifi_ready = true;
    }
  }

  if (wifi_ready) {
    telnet_accept();
    telnet_poll_cli();
  }
}
