#pragma once

#include <stdint.h>

constexpr int   brew_steady_target = 500;
constexpr float brew_steady_delta = 1.1;
constexpr float K1p =  2.5;
constexpr float K1i = 0.01;
constexpr float K1d = 30.0;
constexpr float K2p =  3.0;
constexpr float K2i = 0.01;
constexpr float K2d =  5.0;
constexpr float decay1_20  = 2.0;
constexpr float decay1_100 = 6.5;
constexpr float capacity1 = 0.08;
constexpr float transfer = 20.0;
constexpr float decay2 = 115.0;
constexpr float capacity2 = 0.006;

constexpr int   config__bootstrap_temperature = 90;       // bootstrap PID hold target
constexpr int   config__bootstrap_temperature_full = 70;  // phase 1 runs flat-out up to here
constexpr int   config__bootstrap_full_power = 100;       // phase-1 power cap (tapers to 0 at handoff)
constexpr int   config__bootstrap_ramp_time = 5;          // phase-1 soft-start ramp, seconds (0 = off)
constexpr int   config__brew_temperature = 95;
constexpr int   config__brew_base_power = 120;
constexpr int   config__brew_preheat_time = 150;
constexpr int   config__brew_pump = 100;

constexpr int brew_bloom_time = 30000;
constexpr int brew_bloom_weight = 50;

constexpr float pid_creep_delta = 0.5;
constexpr float pid_target_dt = 0.1;

struct WifiNetwork {
  const char* ssid;
  const char* password;
};

// Networks are tried in order at connect time; the first one that associates
// within config__wifi_connect_timeout_ms wins. Add more entries as needed.
constexpr WifiNetwork config__wifi_networks[] = {
  { "haf2201", "1a2b3c4d5e" },
  { "orf2201", "1a2b3c4d5e" },
  // { "second-network", "password" },
};
constexpr unsigned config__wifi_network_count =
  sizeof(config__wifi_networks) / sizeof(config__wifi_networks[0]);

// Per-network association timeout.
constexpr unsigned long config__wifi_connect_timeout_ms = 15000;

// Master WiFi switch. Set false to build a diagnostic firmware that never
// connects and never touches the ESP32-S3 modem (setup_net and loop_net become
// no-ops, so no WiFi.status()/telnet/OTA). Use it to test whether the mid-brew
// watchdog reset is the modem wedging under actuator noise: if brews stop
// resetting with this false, the WiFi path is the culprit.
constexpr bool config__wifi_enabled = true;

// Bump this string before every release; the GitHub release tag must match
// exactly (e.g. tag "v0.1.0" -> FIRMWARE_VERSION "v0.1.0").
constexpr const char* FIRMWARE_VERSION = "v0.1.26";

constexpr const char* config__github_owner = "moskyt";
constexpr const char* config__github_repo  = "kanina";
// Asset file name to download from the latest release.
constexpr const char* config__github_asset = "firmware.bin";

constexpr uint16_t config__telnet_port = 23;
