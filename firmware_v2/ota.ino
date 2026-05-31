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

// Try a single network, blocking until it associates or times out.
static bool wifi_try(const WifiNetwork& net) {
  Serial.print("INIT WiFi: ");
  Serial.println(net.ssid);

  WiFi.begin(net.ssid, net.password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    if (millis() - start > config__wifi_connect_timeout_ms) {
      Serial.println();
      Serial.print("WiFi connect TIMEOUT: ");
      Serial.println(net.ssid);
      return false;
    }
    WDT.refresh();
    delay(250);
    Serial.print(".");
  }

  Serial.println();
  return true;
}

static void wifi_connect() {
  for (unsigned i = 0; i < config__wifi_network_count; i++) {
    if (!wifi_try(config__wifi_networks[i])) { continue; }

    Serial.print("WiFi connected, IP = ");
    Serial.println(WiFi.localIP());

    telnet_server.begin();
    Serial.print("Telnet listening on ");
    Serial.print(WiFi.localIP());
    Serial.print(":");
    Serial.println(config__telnet_port);

    wifi_ready = true;
    return;
  }

  Serial.println("WiFi: no network available.");
}

void setup_net() {
  wifi_connect();
  if (wifi_ready) {
    oled_display.println("WIFI OK");
    oled_display.display();
    delay(2000);
    check_for_update(/*verbose=*/true);
  } else {
    oled_display.println("Failed to connect to WIFI");
    oled_display.display();
    delay(2000);
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
