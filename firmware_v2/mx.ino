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
  if (measurement_temperature > 105) { power = 0; }
  analogWrite(PIN_HEATER, power);
}

void mx__stopHeater() {
  analogWrite(PIN_HEATER, 0);
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
