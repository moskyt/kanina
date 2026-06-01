#include <stdlib.h>

void process_cli(char* input) {
  // ignore ESP-IDF log spill from the WiFi co-processor: "E (1234) wifi:..." etc.
  if (input[0] && input[1] == ' ' && input[2] == '(' &&
      (input[0] == 'E' || input[0] == 'W' || input[0] == 'I' || input[0] == 'D' || input[0] == 'V')) {
    return;
  }

  Log.print("Command: >");
  Log.print(input);
  Log.println("<");
  long argi1, argi2, argi3, argi4;

  if (strcmp(input, "STOP") == 0) {
    Log.println("Shutdown requested.");
    mx__shutdown();
    global_state = s_idle;
    neo_idle();
  } else
  if (strcmp(input, "RESET") == 0) {
    Log.println("Reset requested.");
    mx__shutdown();
    delay(50);
    NVIC_SystemReset();
  } else
  if (strcmp(input, "WIFI") == 0) {
    Log.print("WiFi status = ");
    Log.println(WiFi.status());
    Log.print("IP = ");
    Log.println(WiFi.localIP());
    Log.print("RSSI = ");
    Log.println(WiFi.RSSI());
    Log.print("SSID = ");
    Log.println(WiFi.SSID());
    Log.print("wifi_ready = ");
    Log.println(wifi_ready);
    Log.print("firmware = ");
    Log.println(FIRMWARE_VERSION);
  } else
  if (strcmp(input, "TARE") == 0) {
    Log.println("Resetting W to zero.");    
    scale.tare();
  } else 
  if (strcmp(input, "SPRAY ON") == 0) {
    Log.println("Spray on requested.");
    signal_spray = 1;
  } else 
  if (strcmp(input, "SPRAY OFF") == 0) {
    Log.println("Spray off requested.");
    signal_spray = 0;
  } else 
  if (sscanf(input, "BREW %ld %ld %ld %ld", &argi1, &argi2, &argi3, &argi4) == 4) {
    brew_temperature = argi1;
    brew_base_power = argi2;
    brew_preheat_time = argi3;
    brew_target_weight = argi4;
    global_state = s_brew;
    brew_step = b_idle;
    Log.println("Running BREW program.");
    Log.print("Temperaure: ");
    Log.print(brew_temperature);
    Log.println(" degC");
    Log.print("Base power: ");
    Log.print(brew_base_power);
    Log.println(" i");
    Log.print("Preheat time: ");
    Log.print(brew_preheat_time);
    Log.println(" ms");
    Log.print("Target weight: ");
    Log.print(brew_target_weight);
    Log.println(" g");
  } else
  if (sscanf(input, "FLOW %ld %ld", &argi1, &argi2) == 2) {
    Log.println("Running FLOW program.");
    Log.println("Resetting the scale...");
    scale.tare();
    flow_pump = argi1;
    flow_time = argi2;
    global_state = s_flow;
    program_start = millis();
    pump_primed = false;
    Log.print("Pump at power ");
    Log.print(flow_pump);
    Log.print(" will run for ");
    Log.println(flow_time);
  } else
  if (sscanf(input, "COOL %ld", &argi1) == 1) {
    cool_target = argi1;
    Log.print("COOL requested: ");
    Log.println(cool_target);  
    global_state = s_cool;
  } else 
  if (sscanf(input, "PUMP %ld", &argi1) == 1) {
    pump_primed = (signal_pump > pump_min_cold);
    signal_pump = argi1;
    Log.print("PUMP requested: ");
    Log.println(signal_pump);  
  } else 
  if (strcmp(input, "PUMP OFF") == 0) {
    Log.println("PUMP OFF requested.");
    signal_pump = 0;
  } else
  if (sscanf(input, "HEAT %ld", &argi1) == 1) {
    signal_heater = argi1;
    Log.print("HEAT requested: ");
    Log.println(signal_heater);  
  } else 
  if (strcmp(input, "HEAT OFF") == 0) {
    Log.println("HEAT OFF requested.");
    signal_heater = 0;
  } else
  if (strcmp(input, "TEST GAUGES") == 0) {
    Log.println("TEST GAUGES requested.");
    signal_heater = 0;

    analogWrite(PIN_METER_POWER, 0);
    analogWrite(PIN_METER_TEMP, 0);
    delay(1500); WDT.refresh();
    analogWrite(PIN_METER_TEMP, 128);
    delay(1500); WDT.refresh();
    analogWrite(PIN_METER_TEMP, 255);
    delay(1500); WDT.refresh();
    analogWrite(PIN_METER_POWER, 0);
    delay(1500); WDT.refresh();
    analogWrite(PIN_METER_POWER, 128);
    delay(1500); WDT.refresh();
    analogWrite(PIN_METER_POWER, 255);
    delay(1500); WDT.refresh();

    analogWrite(PIN_METER_POWER, 0);
    analogWrite(PIN_METER_TEMP, 0);

  } else
  if (sscanf(input, "PID %ld", &argi1) == 1) {
    pid_target = argi1;
    Log.print("PID requested: ");
    Log.println(pid_target);

    global_state = s_pid;
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
  } else {
    Log.print("Unknown command: ");
    Log.println(input);
  }
}

void cli_feed(char c) {
  if (c == '\n' || c == '\r') {
    if (cli_index > 0) {
      cli_buffer[cli_index] = '\0';
      process_cli(cli_buffer);
      cli_index = 0;
    }
  } else {
    if (cli_index < CLI_BUFFER_SIZE - 1) {
      cli_buffer[cli_index++] = c;
    } else {
      cli_index = 0;
    }
  }
}

void cli_update() {
  while (Serial.available() > 0) {
    cli_feed((char) Serial.read());
  }
}