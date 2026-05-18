#include <Button2.h>

void setup() {  
 // global init
  global_state = s_init;
  Wire.begin();
  Wire1.begin();
  Serial.begin(115200);
  delay(1000);
  Serial.println("HELO\n");

  setup_pins();
  setup_scale();
  setup_neopixel();
  setup_buttons();
  setup_led();
  setup_oled();

  WDT.begin(5000); // 5-second timeout

  setup_net();

  Serial.println("INIT done.");

  global_state = s_idle;
  signal_pump = 0;
  signal_heater = 0;
  signal_spray = 0;
  neo(120,120,120);
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
  button.setLongClickHandler(handle_main_long);
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

  led_display.print("Milk");
}
