#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

bool ota_ready = false;
WiFiUDP log_udp;

void setup_ota() {
  Serial.print("INIT WiFi: ");
  Serial.println(config__wifi_ssid);

  WiFi.begin(config__wifi_ssid, config__wifi_password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    if (millis() - start > config__wifi_connect_timeout_ms) {
      Serial.println();
      Serial.println("WiFi connect TIMEOUT, OTA disabled.");
      return;
    }
    WDT.refresh();
    delay(250);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected, IP = ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.onStart([]() {
    mx__shutdown();
    global_state = s_idle;
    brew_step = b_idle;
    neo(0, 0, 255);
    WDT.refresh();
    Serial.println("OTA: start");
  });
  ArduinoOTA.beforeApply([]() {
    mx__shutdown();
    WDT.refresh();
    Serial.println("OTA: applying");
  });
  ArduinoOTA.onError([](int code, const char* msg) {
    Serial.print("OTA: error ");
    Serial.print(code);
    Serial.print(": ");
    Serial.println(msg);
    neo(255, 0, 0);
  });

  ArduinoOTA.begin(WiFi.localIP(), config__ota_hostname, config__ota_password, InternalStorage);
  log_udp.begin(LOG_UDP_PORT);
  Serial.print("Log UDP broadcasting to 255.255.255.255:");
  Serial.println(LOG_UDP_PORT);

  ota_ready = true;
  Serial.println("INIT OTA OK.");
}

void loop_ota() {
  static unsigned long last_check = 0;
  unsigned long now = millis();

  // every 2s, verify WiFi is still up; rebind OTA if it came back after a drop
  if (now - last_check > 2000) {
    last_check = now;
    bool connected = (WiFi.status() == WL_CONNECTED) && (WiFi.localIP() != IPAddress(0, 0, 0, 0));
    if (ota_ready && !connected) {
      Serial.println("WiFi dropped, OTA suspended.");
      ota_ready = false;
    } else if (!ota_ready && connected) {
      Serial.print("WiFi back, re-binding OTA at ");
      Serial.println(WiFi.localIP());
      ArduinoOTA.begin(WiFi.localIP(), config__ota_hostname, config__ota_password, InternalStorage);
      ota_ready = true;
    }
  }

  if (ota_ready) {
    ArduinoOTA.poll();
  }
}
