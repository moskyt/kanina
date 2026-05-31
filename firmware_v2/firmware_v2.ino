#include <SparkFun_Alphanumeric_Display.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <QuickPID.h>
#include <Button2.h>
// Button2 #defines LOW/HIGH/INPUT/OUTPUT/INPUT_PULLUP as plain ints. On the R4
// (Renesas) core these are PinStatus enum values, so the macros clobber the enum
// and break Modulino's isPressed(). Undo the pollution to restore the enum.
#undef LOW
#undef HIGH
#undef INPUT
#undef OUTPUT
#undef INPUT_PULLUP
#include <Adafruit_NeoPixel.h>
#include <WDT.h>
#include <HX711_ADC.h>
#include <WiFiS3.h>
// Adafruit_SSD1306 #defines BLACK/WHITE as ints; Modulino declares them as
// ModulinoColor globals, so the macros wreck its declarations. Hide the macros
// only while Modulino.h is parsed, then restore them for the display code.
#pragma push_macro("BLACK")
#pragma push_macro("WHITE")
#undef BLACK
#undef WHITE
#include <Modulino.h>
#pragma pop_macro("BLACK")
#pragma pop_macro("WHITE")

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
                     s_update                 // self-update from GitHub Releases
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

ModulinoBuzzer buzzer;

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

int brew_target_weight = 250;

// BREW program
enum brew_step_ { b_idle, b_heatup1, b_preheat1, b_flow1, b_heatup2, b_preheat2, b_flow2, b_done, b_error } brew_step;

long brew_temperature;
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
//- printer
unsigned long pid_print_counter = -1;
unsigned long pid_print_period = 200;
//- integrator
float accumulated_heat;
float adjusted_accumulated_heat;
float decay1 = 0;
float dt;
unsigned long last_pid_t = 0;

// --- BUTTON HANDLERS

void handle_set_up(Button2& btn) {
  Serial.println("up");

  if (global_state == s_idle || global_state == s_bootstrap) {
    if (brew_target_weight < 500) brew_target_weight += 10;
    show_led_number(brew_target_weight);
  }
}

void handle_set_down(Button2& btn) {
  Serial.println("down");

  if (global_state == s_idle || global_state == s_bootstrap) {
    if (brew_target_weight > 100) brew_target_weight -= 10;
    show_led_number(brew_target_weight);
  }
}

void handle_main_long(Button2& btn) {
  Serial.println("main long");
  if (global_state == s_idle) {
    // start bootstrap!

    buzzer.tone(440, 100);
    delay(200);
    buzzer.tone(0, 0);
    delay(200);
    buzzer.tone(440, 100);
    delay(200);
    buzzer.tone(0, 0);

    pid_target = config__bootstrap_temperature;
    Serial.print("Bootstrap requested: ");
    Serial.println(config__bootstrap_temperature);

    global_state = s_bootstrap;
    Input = measurement_temperature;
    Setpoint = measurement_temperature + 1;
    if (Setpoint > pid_target) Setpoint = pid_target;
    pid_creep_timer = 0;
    pid_print_counter = 0; 
    accumulated_heat = 0.0;

    heaterPID.SetTunings(K1p, K1i, K1d);
    heaterPID.SetMode(heaterPID.Control::automatic);
    heaterPID.SetOutputLimits(0, 255);
    heaterPID.Reset();    
    neo_bootstrap();
  } else 
  if (global_state == s_bootstrap) {
    global_state = s_idle;
    neo_idle();
    mx__shutdown();
  } 
}

void handle_main(Button2& btn) {
  Serial.println("main");

  if ((global_state == s_idle) || (global_state == s_bootstrap)) {
    // start brew!
    brew_temperature = config__brew_temperature;
    brew_base_power = config__brew_base_power;
    brew_preheat_time = config__brew_preheat_time;
    global_state = s_brew;
    brew_step = b_idle;
    neo_brew();
  } else
  if (global_state == s_brew) {
    if (brew_step == b_error) {
      global_state = s_idle;
      brew_step = b_idle;
      neo_idle();
    } else {
      // stop brew!
      mx__shutdown();
      signal_pump = 0;
      signal_heater = 0;
      signal_spray = 0;
      brew_step = b_error;
      neo_error();
    }
  }
}