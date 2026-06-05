#include <SparkFun_Alphanumeric_Display.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <QuickPID.h>
#include <Button2.h>
// Button2 #defines LOW/HIGH/INPUT/OUTPUT/INPUT_PULLUP as plain ints. On the R4
// (Renesas) core these are PinStatus enum values, so the macros clobber the enum.
// Undo the pollution to restore the enum for the core and the includes below.
#undef LOW
#undef HIGH
#undef INPUT
#undef OUTPUT
#undef INPUT_PULLUP
#include <Adafruit_NeoPixel.h>
#include <WDT.h>
#include <HX711_ADC.h>
#include <WiFiS3.h>
#include "mini_buzzer.h"

#include "config.h"
#include "update.h"

extern bool wifi_ready;
void telnet_write(const uint8_t* p, size_t n);

class LogStream : public Print {
  static constexpr size_t BUF = 128;
  uint8_t buf[BUF];
  size_t idx = 0;
  void flush_net() {
    if (idx == 0) { return; }
    if (wifi_ready) { telnet_write(buf, idx); }
    idx = 0;
  }
public:
  size_t write(uint8_t c) override {
    Serial.write(c);
    if (idx < BUF) buf[idx++] = c;
    if (c == '\n' || idx == BUF) flush_net();
    return 1;
  }
  size_t write(const uint8_t* p, size_t n) override {
    Serial.write(p, n);
    for (size_t i = 0; i < n; i++) {
      if (idx < BUF) buf[idx++] = p[i];
      if (p[i] == '\n' || idx == BUF) flush_net();
    }
    return n;
  }
};
LogStream Log;

/*

mosfet for pump
https://www.laskakit.cz/pwm-mosfet-modul-d4184--40vdc-50a

*/

//--- pins

#define PIN_SPRAY         D2
#define PIN_PUMP          D3
#define PIN_TEMPERATURE   A0
#define PIN_HEATER        D9
#define PIN_NEOPIXEL      D5
#define PIN_BUTTON        D4
#define PIN_SET_UP        D6
#define PIN_SET_DOWN      D7
#define PIN_WEIGHT_DATA   D12
#define PIN_WEIGHT_CLOCK  D13
#define PIN_METER_TEMP    D10
#define PIN_METER_POWER    D11

//--- I2C bus

#define LED_I2C_ADDR  0x70
#define OLED_I2C_ADDR 0x3C

//--- CLI

#define CLI_BUFFER_SIZE 64

char cli_buffer[CLI_BUFFER_SIZE];
uint8_t cli_index = 0;

//--- state

enum global_state_ { s_init,                  // startup
                     s_brew,                  // auto brew
                     s_bootstrap,             // preheat to make brew start quicker
                     s_idle,                  // idle
                     s_pid,                   // simple PID heatup
                     s_flow,                  // flow rate measurement
                     s_cool,                  // cooldown by pump
                     s_update,                // self-update from GitHub Releases
                     s_error                  // cancel/overheat/something
                     } global_state;

unsigned short signal_spray = 0;
unsigned short signal_pump = 0;
unsigned short signal_heater = 0;
float measurement_temperature = 0;
float measurement_weight = 0;

//--- thermistor

#define nominal_resistance 100000  // Nominal resistance at 25⁰C
#define nominal_temperature 25     // Temperature for nominal resistance (almost always 25⁰ C)
#define beta 3950                  // The beta coefficient or the B value of the thermistor (usually 3000-4000) check the datasheet for the accurate value.
#define Rref 10000                 // Value of resistor used for the voltage divider

const int n_temperature_buffer = 50;
int i_temperature_buffer = n_temperature_buffer - 10;
int m_temperature_buffer = 0;
double temperature_buffer = 0.0;

//--- scale
HX711_ADC scale(PIN_WEIGHT_DATA, PIN_WEIGHT_CLOCK);
int calibration_w = 77;
long int calibration_r = 155000;

//--- pump
#define pump_min        70    // pump wont run at all below this
#define pump_min_cold   130   // kickstart power needed below this
#define pump_prime      200  
#define pump_prime_time 900  
bool pump_primed = false;

//--- hardware

Adafruit_NeoPixel neopixel = Adafruit_NeoPixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

MiniBuzzer buzzer;

Button2 button, set_up, set_down;

HT16K33 led_display;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Use Wire1 instead of Wire
Adafruit_SSD1306 oled_display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire1,
  -1   // no reset pin
);

//--- UI update

int update_counter = 0;
int update_ref = 100;
bool update_flag = false;

//--- helper programs

unsigned long program_start = -1;

//--- COOL program

int cool_target;

//--- FLOW program

int flow_pump;
unsigned long flow_time;

//--- PID and BREW

/*
temperature(C) power(byte) preheat(ms) weight(g)
BREW 95 85 200 50

////
BREW 95 95 100 50

init:
- tare
step 0: 
- run PID(1) to setpoint T
- wait until (brew_steady_target) measurements fall within (brew_steady_delta) 
step 1:
- turn off PID and apply (brew_heat_power) for (brew_preheat_time) ms
step 2:
- start flow
- start valve
- run PID2
complete when weight is (brew_target_weight)
*/

int brew_target_weight = 320;

// BREW program
enum brew_step_ { b_idle, b_heatup1, b_preheat1, b_flow1, b_heatup2, b_preheat2, b_flow2, b_done } brew_step;

long brew_temperature = config__brew_temperature;  // sane default so the temp gauge
                                                    // works before the first brew sets it
long brew_base_power;
long brew_preheat_time;
unsigned long brew_preheat_timer;
int brew_preheat_power0;
unsigned long brew_start_timestamp;
int brew_steady_counter;
char brew_ux[16] = "         ";
float brew_temperature_avg;
float brew_temperature_min;
float brew_temperature_max;

//--- PID

float Setpoint, Input, Output, AdjustedSetpoint;
QuickPID heaterPID(&Input, &Output, &AdjustedSetpoint);
unsigned long pid_creep_timer = -1;
unsigned long pid_creep_period = 1000;
int pid_target = -1;
// BOOTSTRAP runs in phases (cf. brew_step): bs_full_power = heater flat-out ramp,
// bs_pid = PID final approach. Set on bootstrap start, advanced in loop_bootstrap().
enum bootstrap_step_ { bs_full_power, bs_pid } bootstrap_step;
unsigned long bootstrap_start_ms = 0;  // when bootstrap began (phase-1 soft-start ramp)
//- printer
unsigned long pid_print_counter = -1;
unsigned long pid_print_period = 200;
//- integrator
float accumulated_heat;
float adjusted_accumulated_heat;
float decay1 = 0;
float dt;
unsigned long last_pid_t = 0;

// Keep at least one function definition in this main sketch file. arduino-cli
// anchors the auto-generated function prototypes to the first function in the
// main .ino; if there is none, the whole prototype block is emitted at the very
// top of the merged translation unit — before this file's #includes — so the
// prototypes for helpers taking library/config types (WifiNetwork from config.h,
// WiFiSSLClient from WiFiS3.h, etc.) reference types that aren't declared yet and
// the build fails with misleading "does not name a type" errors in ota/update.
// All real code now lives in topical .ino files, so this anchor stands in. Do not
// remove it unless setup()/loop() (or another function) move back into this file.
void firmware_anchor() {}

