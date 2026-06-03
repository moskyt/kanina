#include <Button2.h>

// --- Reset cause -------------------------------------------------------------
// The RA4M1 latches why it last reset in RSTSR0/1/2. We snapshot it at the very
// start of setup() (before anything can clear it), then clear the flags so the
// *next* boot's registers describe only the next reset. A plain power-on (PORF)
// is the boring case; anything else (watchdog, brownout/LVD, software) gets an
// OLED splash so a mid-brew reset leaves a visible trail.
//
// Note: a brownout deep enough to drop the rail to 0 V looks like a power-on
// (PORF) and will NOT splash; an LVD-detected dip or a watchdog timeout will.
static uint8_t  reset_rstsr0 = 0;  // bit0 PORF, bit1-3 LVD0/1/2RF, bit7 DPSRSTF
static uint16_t reset_rstsr1 = 0;  // bit0 IWDTRF, bit1 WDTRF, bit2 SWRF
static uint8_t  reset_rstsr2 = 0;  // bit0 CWSF (cold/warm start)
static uint8_t  prev_boot_phase  = 0;  // boot phase reached before this reset (0 = none)
static uint8_t  prev_boot_gstate = 0xFF;  // global_state at the reset (if running)
static uint8_t  prev_boot_bstep  = 0xFF;  // brew_step at the reset (if in brew)

static bool reset_was_power_on() {
  return (reset_rstsr0 & 0x01) != 0;  // PORF
}

static const char* boot_phase_label() {
  switch (prev_boot_phase) {
    case BP_HW_INIT:   return "HW init";
    case BP_UPDATE:    return "update chk";
    case BP_WDT_ARMED: return "WDT armed";
    case BP_FIRST_NET: return "1st net";
    case BP_RUNNING:   return "running";
    default:           return "?";
  }
}

// Fine location for a reset during normal operation (prev_boot_phase==BP_RUNNING):
// the brew step if we were brewing, otherwise the global state.
static const char* boot_run_label() {
  if (prev_boot_gstate == s_brew) {
    switch (prev_boot_bstep) {
      case b_idle:     return "brew idle/tare";
      case b_heatup1:  return "brew heatup1";
      case b_preheat1: return "brew preheat1";  // 2nd scale.tare() lives here
      case b_flow1:    return "brew flow1";
      case b_heatup2:  return "brew heatup2";
      case b_preheat2: return "brew preheat2";
      case b_flow2:    return "brew flow2";
      case b_done:     return "brew done";
      default:         return "brew ?";
    }
  }
  switch (prev_boot_gstate) {
    case s_idle:      return "idle";
    case s_bootstrap: return "bootstrap";
    case s_pid:       return "pid";
    case s_flow:      return "flow";
    case s_cool:      return "cool";
    case s_update:    return "update";
    case s_error:     return "error";
    case s_init:      return "init";
    default:          return "?";
  }
}

static const char* reset_cause_label() {
  if (reset_rstsr1 & 0x0002) return "WATCHDOG";       // WDTRF
  if (reset_rstsr1 & 0x0001) return "IND.WATCHDOG";   // IWDTRF
  if (reset_rstsr1 & 0x0004) return "SOFTWARE";       // SWRF
  if (reset_rstsr0 & 0x000E) return "BROWNOUT/LVD";   // LVD0/1/2RF
  if (reset_rstsr0 & 0x0080) return "DEEP-STANDBY";   // DPSRSTF
  if (reset_rstsr1 & 0x0300) return "RAM ERROR";      // RPERF/REERF
  return "EXT / PIN";                                 // nothing latched (NRST, upload)
}

static void capture_reset_cause() {
  reset_rstsr0 = R_SYSTEM->RSTSR0;
  reset_rstsr1 = R_SYSTEM->RSTSR1;
  reset_rstsr2 = R_SYSTEM->RSTSR2;

  // Boot breadcrumb: the phase reached before this reset is valid only if the
  // magic survived in .noinit (a warm reset). Then re-arm it for this boot.
  bool warm = (boot_magic == BOOT_MAGIC);
  prev_boot_phase  = warm ? boot_phase  : 0;
  prev_boot_gstate = warm ? boot_gstate : 0xFF;
  prev_boot_bstep  = warm ? boot_bstep  : 0xFF;
  boot_magic = BOOT_MAGIC;
  boot_phase = BP_HW_INIT;

  char line[96];
  snprintf(line, sizeof(line), "RESET cause: %s  where: %s (%s)  [%02X %04X %02X]",
           reset_was_power_on() ? "power-on" : reset_cause_label(),
           boot_phase_label(), boot_run_label(),
           reset_rstsr0, reset_rstsr1, reset_rstsr2);
  Serial.println(line);

  // Clear the flags (write 0; they live behind PRCR.PRC1) so the next boot
  // reflects only the next reset. Leave RSTSR2/CWSF alone (different semantics).
  R_SYSTEM->PRCR = 0xA502;  // unlock PRC1 (key 0xA5)
  R_SYSTEM->RSTSR0 = 0;
  R_SYSTEM->RSTSR1 = 0;
  R_SYSTEM->PRCR = 0xA500;  // relock
}

static void oled_reset_cause_splash() {
  oled_display.clearDisplay();
  oled_display.setTextColor(SSD1306_WHITE);

  oled_display.setTextSize(2);
  oled_display.setCursor(0, 0);
  oled_display.println("RESET");

  oled_display.setTextSize(1);
  oled_display.println("");
  oled_display.print("cause: ");
  oled_display.println(reset_cause_label());
  oled_display.print("where: ");
  oled_display.println(boot_phase_label());
  if (prev_boot_phase == BP_RUNNING) {
    oled_display.print("in: ");
    oled_display.println(boot_run_label());
  }

  char raw[22];
  snprintf(raw, sizeof(raw), "%02X %04X %02X", reset_rstsr0, reset_rstsr1, reset_rstsr2);
  oled_display.println(raw);

  oled_display.display();
}

void setup() {  
 // global init
  global_state = s_init;
  Wire.begin();
  Wire1.begin();
  Serial.begin(115200);
  delay(1000);
  Serial.println("HELO");
  Serial.println("Kanina");
  Serial.println(FIRMWARE_VERSION);
  Serial.println("");

  capture_reset_cause();

  buzzer.begin();
  buzzer_helo();

  setup_pins();
  setup_scale();
  setup_neopixel();
  setup_buttons();
  setup_led();
  setup_oled();

  // If the last reset was anything other than a normal power-on, flash the
  // cause on the OLED now (the WiFi/update screens overwrite it shortly).
  if (!reset_was_power_on()) {
    oled_reset_cause_splash();
    delay(4000);  // hold long enough to read; WDT isn't running yet
  }

  // setup_net() runs the boot-time update check, which performs TLS handshakes
  // that block inside WiFiSSLClient longer than the IWDT max (~5.5 s on RA4M1,
  // and the IWDT can't be reinitialised once started). So we start the WDT
  // only AFTER the update check has had its chance.
  boot_phase = BP_UPDATE;
  setup_net();

  WDT.begin(5000); // 5-second timeout for normal operation
  boot_phase = BP_WDT_ARMED;

  Serial.println("INIT done.");

  perform__idle();
  signal_pump = 0;
  signal_heater = 0;
  signal_spray = 0;
  buzzer_init();
  show_led_number(brew_target_weight);
}

void setup_scale() {
  Serial.println("INIT scale...");
  scale.begin();
  scale.start(2000, true);
  if (scale.getTareTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  }
  else {
    scale.setCalFactor(1.0);
    Serial.println("scale init OK");
  }
}

void setup_buttons() {
  Serial.println("INIT buttons...");

  button.begin(PIN_BUTTON, INPUT_PULLUP, true);
  set_up.begin(PIN_SET_UP, INPUT_PULLUP, true);
  set_down.begin(PIN_SET_DOWN, INPUT_PULLUP, true);

  button.setClickHandler(handle_main);
  button.setLongClickTime(2000);
  button.setLongClickDetectedHandler(handle_main_long);
  set_up.setPressedHandler(handle_set_up);
  set_down.setPressedHandler(handle_set_down);
}

void setup_neopixel() {
  Serial.println("INIT neopixel...");

  neopixel.setBrightness(255);
  neopixel.begin();
  neopixel.setPixelColor(0,255,0,0);
  neopixel.show();
}
  
void setup_pins() {
  Serial.println("INIT pins...");

  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_SPRAY, OUTPUT);
  pinMode(PIN_HEATER, OUTPUT);
  mx__stopSpray();
  mx__stopPump();
  mx__stopHeater();

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_SET_UP, INPUT_PULLUP);
  pinMode(PIN_SET_DOWN, INPUT_PULLUP);

  pinMode(PIN_TEMPERATURE, INPUT);

  pinMode(PIN_METER_TEMP, OUTPUT);
  pinMode(PIN_METER_POWER, OUTPUT);
}
 

void setup_oled() {
  Serial.println("INIT oled...");

  // https://www.laskakit.cz/2-42--128x64-oled-displej-i--c--bily/

  if (!oled_display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("OLED not found");
    while (1);
  }

  oled_display.clearDisplay();

  oled_display.setTextSize(1);
  oled_display.setTextColor(SSD1306_WHITE);
  oled_display.setCursor(0,0);
  oled_display.println("Kanina");
  oled_display.println("");
  oled_display.print("firmware ");
  oled_display.println(FIRMWARE_VERSION);

  oled_display.display();

  Serial.println("INIT oled OK.");
}

void setup_led() {
  Serial.println("INIT led...");

  if (led_display.begin(LED_I2C_ADDR, 0xff, 0xff, 0xff, Wire1) == false)
  {
    Serial.println("INIT led FAILED.");
    while (1);
  }
  Serial.println("INIT led OK.");

  led_display.print("CAFE");
}
