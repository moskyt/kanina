void mx__shutdown() {
  mx__stopSpray();
  mx__stopPump();
  mx__stopHeater();
  signal_spray = 0;
  signal_pump = 0;
  signal_heater = 0;
}

void mx__startSpray() {
  digitalWrite(PIN_SPRAY, HIGH);
}

void mx__stopSpray() {
  digitalWrite(PIN_SPRAY, LOW);
}

void mx__runPump(unsigned short int speed) {
  if (speed < pump_min) speed = pump_min;
  if (speed < pump_min_cold) {
    if (!pump_primed) {
      analogWrite(PIN_PUMP, pump_prime);
      Serial.println("Priming pump...");
      delay(pump_prime_time); WDT.refresh();
      pump_primed = true;
    }
  } else {
    pump_primed = true;
  }
  analogWrite(PIN_PUMP, speed);
}

void mx__stopPump() {
  pump_primed = false;
  analogWrite(PIN_PUMP, 0);
}

void mx__runHeater(unsigned short int power) {
  analogWrite(PIN_HEATER, power);
}

void mx__stopHeater() {
  analogWrite(PIN_HEATER, 0);
}

void show_led_number(int number) {
  char buf[5];
  // Right-align in a 4-character field, space-padded on the left
  snprintf(buf, sizeof(buf), "%4d", number);
  led_display.print(buf);
}

// --- status neopixel ---------------------------------------------------------
// The pixel either holds a solid colour or breathes between two colours.
// neo_tick() must run every loop iteration to advance a pulse; the helpers
// below only choose the mode/colours and never block.

enum neo_mode_ { neo_mode_solid, neo_mode_pulse };
neo_mode_ neo_mode = neo_mode_solid;

uint8_t neo_r1, neo_g1, neo_b1;        // solid colour, or pulse "from" colour
uint8_t neo_r2, neo_g2, neo_b2;        // pulse "to" colour
unsigned long neo_period_ms = 2000;    // full from->to->from cycle length

// Last colour actually pushed to the LED, so we skip redundant show()s.
int neo_last_r = -1, neo_last_g = -1, neo_last_b = -1;

// Low-level writer. Deduplicates, so calling it every loop is cheap.
void neo(uint8_t r, uint8_t g, uint8_t b) {
  if (r == neo_last_r && g == neo_last_g && b == neo_last_b) return;
  neo_last_r = r; neo_last_g = g; neo_last_b = b;
  neopixel.setPixelColor(0, r, g, b);
  neopixel.show();
}

// Hold a fixed colour (also cancels any running pulse).
void neo_solid(uint8_t r, uint8_t g, uint8_t b) {
  neo_mode = neo_mode_solid;
  neo(r, g, b);
}

// Breathe between (r1,g1,b1) and (r2,g2,b2), one full cycle every t_sec.
void neo_pulse(uint8_t r1, uint8_t g1, uint8_t b1,
               uint8_t r2, uint8_t g2, uint8_t b2, float t_sec) {
  neo_mode = neo_mode_pulse;
  neo_r1 = r1; neo_g1 = g1; neo_b1 = b1;
  neo_r2 = r2; neo_g2 = g2; neo_b2 = b2;
  neo_period_ms = (t_sec > 0.0) ? (unsigned long)(t_sec * 1000.0) : 1;
}

// Advance the pulse; cheap no-op in solid mode. Phase derives from millis(),
// so re-arming the same pulse never makes the colour jump.
void neo_tick(unsigned long now) {
  if (neo_mode != neo_mode_pulse) return;
  float phase = (float)(now % neo_period_ms) / (float)neo_period_ms;  // 0..1
  float f = 0.5 * (1.0 - cos(phase * 2.0 * PI));                      // 0..1..0
  uint8_t r = neo_r1 + (int)lround((neo_r2 - neo_r1) * f);
  uint8_t g = neo_g1 + (int)lround((neo_g2 - neo_g1) * f);
  uint8_t b = neo_b1 + (int)lround((neo_b2 - neo_b1) * f);
  neo(r, g, b);
}

// --- status helpers ----------------------------------------------------------
// Swap any neo_solid(...) below for e.g. neo_pulse(r1,g1,b1, r2,g2,b2, t_sec)
// to make that status breathe instead.

void neo_bootstrap() {
  // neo_solid(255, 128, 0);
  neo_pulse(128, 64, 0,  255, 128, 0, 3.0); 
}

void neo_idle() {
  neo_solid(128, 128, 128);
}

void neo_done() {
  neo_pulse(0, 128, 0,  0, 255, 0, 3.0);
}

void neo_update() {
  neo_pulse(255, 255, 0, 0, 255, 0, 1.0); 
}

void neo_brew() {
  // neo_solid(255, 0, 255);
  neo_pulse(255, 0, 255, 0, 0, 255, 3.0); 
}

void neo_error() {
  neo_solid(255, 0, 0);
}

void measure_temperature() {
  //--- temperature measurement
  float x = analogRead(PIN_TEMPERATURE);
  temperature_buffer += x;
  i_temperature_buffer++;
  m_temperature_buffer++;
  if (i_temperature_buffer == n_temperature_buffer) {
    float raw = temperature_buffer / m_temperature_buffer;
    float res = Rref * (1023.0 / raw - 1);

    //Serial.print("ADC: ");
    //Serial.println(raw);
    //Serial.print("R: ");
    //Serial.println(res);

    float temperature = res / nominal_resistance;         // (R/Ro)
    temperature = log(temperature);                       // ln(R/Ro)
    temperature /= beta;                                  // 1/B * ln(R/Ro)
    temperature += 1.0 / (nominal_temperature + 273.15);  // + (1/To)
    temperature = 1.0 / temperature;                      // Invert
    measurement_temperature = temperature - 273.15;       // convert absolute temp to C

    //Serial.print("T: ");
    //Serial.println(measurement_temperature);

    temperature_buffer = 0.0;
    i_temperature_buffer = 0;
    m_temperature_buffer = 0;
  }
}