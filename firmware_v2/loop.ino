void start_heatup_and_hold(unsigned long now, int target_temperature) {
  pid_target = target_temperature;
  Input = measurement_temperature;
  Setpoint = measurement_temperature + 1;
  if (Setpoint > target_temperature) Setpoint = target_temperature;
  pid_creep_timer = 0;
  brew_steady_counter = 0;
  accumulated_heat = 0.0;
  program_start = now;

  snprintf(brew_ux, sizeof(brew_ux), "heatup: %d", target_temperature);

  heaterPID.SetTunings(K1p, K1i, K1d);
  heaterPID.SetMode(heaterPID.Control::automatic);
  heaterPID.SetOutputLimits(0, 255);
  heaterPID.Reset();
}

bool heatup_and_hold(unsigned long now, unsigned long steady_target, unsigned long not_before) {
  if (last_pid_t > 0) {
    dt = (now - last_pid_t) * 1e-3;
  } else {
    dt = 0.0;
  }

  if (last_pid_t <= 0 || dt >= pid_target_dt) {
    last_pid_t = now;
    
    if (pid_target > Setpoint) {
      if (now - pid_creep_timer >= pid_creep_period) {
        pid_creep_timer = now;
        Setpoint = Setpoint + pid_creep_delta;
        if (Setpoint > pid_target) Setpoint = pid_target;
      }
    }

    Input = measurement_temperature;
    decay1 = decay1_20 + (Input - 20) * (decay1_100 - decay1_20) / (100 - 20);
    accumulated_heat += signal_heater * dt;
    accumulated_heat -= decay1 * dt;
    if (accumulated_heat < 0) accumulated_heat = 0;
    adjusted_accumulated_heat = accumulated_heat;
    if (Input < Setpoint)
      adjusted_accumulated_heat -= transfer * (Setpoint - Input);    
    if (adjusted_accumulated_heat < 0) adjusted_accumulated_heat = 0;
    AdjustedSetpoint = Setpoint - adjusted_accumulated_heat * capacity1;
    AdjustedSetpoint = constrain(AdjustedSetpoint, 20.0, Setpoint);
    heaterPID.Compute();    
    signal_heater = constrain((int)(Output), 0, 255);

    if (steady_target > 0) {
      if (fabs(Input - brew_temperature) < brew_steady_delta) {
        brew_steady_counter++;
        snprintf(brew_ux, sizeof(brew_ux), "S %3d/%3d", brew_steady_counter, steady_target);
      } else {
        brew_steady_counter = 0;
        snprintf(brew_ux, sizeof(brew_ux), "heatup: %d", pid_target);
      }
    }
  }

  return (((steady_target == 0) || (brew_steady_counter >= steady_target)) && ( (not_before == 0) || (now > not_before) ));
}

void start_preheat(unsigned long now) {
  snprintf(brew_ux, sizeof(brew_ux), "pre %d ms", brew_preheat_time);
  brew_preheat_timer = now;
  brew_preheat_power0 = signal_heater;
  signal_pump = 0;
  signal_spray = 0;
}

bool preheat(unsigned long now) {
  signal_heater = brew_preheat_power0 + (now - brew_preheat_timer) * (brew_base_power - brew_preheat_power0) / brew_preheat_time;
  return (now > brew_preheat_timer + brew_preheat_time);
}

void start_flow(unsigned long now) {
  strncpy(brew_ux, "(brew)   ", sizeof(brew_ux));

  brew_start_timestamp = now;
  accumulated_heat = 0.0;//(brew_base_power - decay2) * brew_preheat_time;
  // Serial.print("U = ");
  // Serial.print(accumulated_heat);
  // Serial.print(" -> dT = ");
  // Serial.println(accumulated_heat * capacity2);
  
  brew_temperature_avg = 0.0;
  brew_temperature_min = Input;
  brew_temperature_max = Input;

  signal_spray = 1;
  signal_pump = config__brew_pump;        
  signal_heater = brew_base_power;

  heaterPID.SetTunings(K2p, K2i, K2d);
  heaterPID.SetMode(heaterPID.Control::automatic);
  heaterPID.SetOutputLimits(-30, +30);
  heaterPID.Reset();
}

bool flow(unsigned long now, int target_weight) {
  Input = measurement_temperature;
  if (last_pid_t > 0) {
    dt = (now - last_pid_t) * 1e-3;
  } else {
    dt = 0.0;
  }
  if (last_pid_t <= 0 || dt >= pid_target_dt) {
    last_pid_t = now;
    accumulated_heat += signal_heater * dt;
    accumulated_heat -= decay2 * dt;
    if (accumulated_heat < 0) accumulated_heat = 0;
    AdjustedSetpoint = Setpoint - accumulated_heat * capacity2;
    AdjustedSetpoint = constrain(AdjustedSetpoint, 20.0, 99.0);
    heaterPID.Compute();    
    signal_heater = constrain(((int)(Output) + brew_base_power), 0, 255);

    brew_temperature_avg += Input * dt;
    brew_temperature_min = min(brew_temperature_min, Input);
    brew_temperature_max = max(brew_temperature_max, Input);
  }
  return (measurement_weight >= target_weight);
}

void loop_brew(unsigned long now) {
  if (brew_step == b_idle) {
    Serial.println("Starting brew!");
    Serial.println("Tare.");
    scale.tare();

    strncpy(brew_ux, "            ", sizeof(brew_ux));

    signal_heater = 0;
    signal_pump = 0;
    signal_spray = 0;

    Serial.println("BREW: stating heat-up phase.");

    start_heatup_and_hold(now, brew_temperature);
    brew_step = b_heatup1;
  }
  else if (brew_step == b_heatup1) {
    if (heatup_and_hold(now, brew_steady_target, 0)) {
      Serial.println("Steady state achieved! Starting preheat phase.");
      Serial.print("Preheat time = ");
      Serial.print(brew_preheat_time);
      Serial.println(" ms");

      buzzer_start_preheat();
      neo_brew_actual();
      start_preheat(now);
      brew_step = b_preheat1;
    }
    if (update_counter == 0) print_brew_heatup_serial();
  }
  else if (brew_step == b_preheat1) {
    if (preheat(now)) {
      Serial.println("Preheat completed, starting the brew itself.");
      Serial.println("Tare.");
      scale.tare();
      start_flow(now);
      brew_step = b_flow1;
    }
    if (update_counter == 0) print_brew_preheat_serial();
  }
  else if (brew_step == b_flow1) {
    if (flow(now, brew_bloom_weight)) {
      float brew_time = (now - brew_start_timestamp) * 1e-3;
      Serial.println("First flow completed! Going to bloom.");        
      Serial.print("It took ");        
      Serial.print(brew_time);
      Serial.print(" s for ");
      Serial.print(measurement_weight);
      Serial.println(" g of coffee.");
      Serial.print("Temperatures:");
      Serial.print(" avg = ");
      brew_temperature_avg /= brew_time;
      Serial.println(brew_temperature_avg);
      Serial.print(" min = ");
      Serial.println(brew_temperature_min);
      Serial.print(" max = ");
      Serial.println(brew_temperature_max);
      signal_pump = 0;
      signal_spray = 0;
      update_counter = 0;
      brew_step = b_heatup2;
      program_start = now;
      strncpy(brew_ux, "(bloom)  ", sizeof(brew_ux));
    }
    if (update_counter == 0) print_brew_flow_serial();
  }
  else if (brew_step == b_heatup2) {
    if (heatup_and_hold(now, 0, program_start + brew_bloom_time)) {
      Serial.println("Post-bloom steady state achieved! Starting preheat phase.");
      Serial.print("Preheat time = ");
      Serial.print(brew_preheat_time);
      Serial.println(" ms");

      start_preheat(now);
      brew_step = b_preheat2;
    }
    if (update_counter == 0) print_brew_heatup_serial();
  }
  else if (brew_step == b_preheat2) {
    if (preheat(now)) {
      Serial.println("Post-bloom preheat completed, starting the actual brew itself.");
      start_flow(now);
      brew_step = b_flow2;
    }
    if (update_counter == 0) print_brew_flow_serial();
  }
  else if (brew_step == b_flow2) {
    if (flow(now, brew_target_weight)) {
      float brew_time = (now - brew_start_timestamp) * 1e-3;
      Serial.println("Brew completed! Enjoy your joe.");        
      Serial.print("It took ");        
      Serial.print(brew_time);
      Serial.print(" s for ");
      Serial.print(measurement_weight);
      Serial.println(" g of coffee.");
      Serial.print("Temperatures:");
      Serial.print(" avg = ");
      brew_temperature_avg /= brew_time;
      Serial.println(brew_temperature_avg);
      Serial.print(" min = ");
      Serial.println(brew_temperature_min);
      Serial.print(" max = ");
      Serial.println(brew_temperature_max);
      signal_pump = 0;
      signal_spray = 0;
      update_counter = 0;
      brew_step = b_done;
      program_start = now;
      mx__shutdown();  
      strncpy(brew_ux, "(done)", sizeof(brew_ux));
      neo_done();
    }
    if (update_counter == 0) print_brew_flow_serial();
  }
  else if (brew_step == b_done) {
    // The pot is full of coffee (200+ g) when brewing finishes, so the moment
    // the weight drops below zero the tared pot has clearly been lifted off.
    if (measurement_weight < 0) {
      perform__idle();
    }
  }
  else {
    // brew cancelled
    if (now >= program_start + 30000) {
      // go back to IDLE
      perform__idle();
    } else {
      strncpy(brew_ux, "(cancelled)", sizeof(brew_ux));
    }
  }
}

void loop_cool(unsigned long now) {
  if (measurement_temperature < cool_target) {
    Serial.println("COOL program ends");
    perform__idle();
    signal_pump = 0;
  } else {
    signal_pump = 255;
  }
}

void loop_flow(unsigned long now) {
  if (now >= program_start + flow_time) {
    Serial.print("FLOW program ends -- ");
    Serial.print("started at ");
    Serial.print(program_start);
    Serial.print(" and now is ");
    Serial.print(now);
    Serial.println(".");
    long dt = now - program_start;
    float evaluated_flow_rate = measurement_weight * 1000.0 / dt;
    Serial.print("Flow rate is ");
    char buf[16];
    dtostrf(evaluated_flow_rate, 0, 1, buf);  // (value, min_width, decimal_places, buffer)
    Serial.print(buf);
    Serial.println(" g/s");
    perform__idle();
  } else {
    if (signal_pump == 0) {
      Serial.print("Flow program starting at ");
      Serial.println(now);
      program_start = now;
    }
    signal_pump = flow_pump;
  }
}

void loop_pid(unsigned long now) {
  Input = measurement_temperature;
  if (last_pid_t > 0) {
    dt = (now - last_pid_t) * 1e-3;
  } else {
    dt = 0.0;
  }
  if (last_pid_t <= 0 || dt >= pid_target_dt) {
    last_pid_t = now;

    if (pid_target > Setpoint) {
      if (now - pid_creep_timer >= pid_creep_period) {
        pid_creep_timer = now;
        Setpoint = Setpoint + pid_creep_delta;
        if (Setpoint > pid_target) Setpoint = pid_target;
      }
    }

    decay1 = decay1_20 + (Input - 20) * (decay1_100 - decay1_20) / (100 - 20);
    accumulated_heat += signal_heater * dt;
    accumulated_heat -= decay1 * dt;
    // Serial.print("U ");
    // Serial.print(accumulated_heat);
    // Serial.print(" dt ");
    // Serial.print(dt);
    // Serial.print(" P ");
    // Serial.print(signal_heater);
    // Serial.print(" D ");
    // Serial.println(decay1);
    if (accumulated_heat < 0) accumulated_heat = 0;
    float adjusted_accumulated_heat = accumulated_heat - transfer * (Setpoint - Input);    
    if (adjusted_accumulated_heat < 0) adjusted_accumulated_heat = 0;
    AdjustedSetpoint = Setpoint - adjusted_accumulated_heat * capacity1;
    AdjustedSetpoint = constrain(AdjustedSetpoint, 20.0, Setpoint);
    heaterPID.Compute();    
    signal_heater = constrain((int)(Output), 0, 255);
  }

  if (update_counter == 0) print_pid_serial();
}

void loop() {
  unsigned long now = millis();

  // Breadcrumb: mark the first post-update modem call, then "running" once we
  // survive it. If loop_net() wedges on a modem desynced by the update's TLS and
  // the freshly-armed WDT fires, the splash shows where: 1st net.
  static bool booted = false;
  if (!booted) boot_phase = BP_FIRST_NET;

  // Live breadcrumb: which state/brew step is executing this loop, so a reset
  // during the brew sequence pinpoints the phase on the next boot's splash.
  boot_gstate = global_state;
  boot_bstep  = brew_step;

  //--- WiFi / telnet
  loop_net();

  if (!booted) { boot_phase = BP_RUNNING; booted = true; }

  if (update_counter <= 0 || update_flag) {
    update_displays();
    update_flag = false;
  }

  //--- status neopixel pulse (runs every loop for smooth breathing)
  neo_tick(now);

  //--- buttons hardware
  button.loop();
  set_up.loop();
  set_down.loop();

  //Serial.println(digitalRead(PIN_BUTTON));

  //--- CLI processing
  cli_update();

  //--- sensors
  measure_temperature();
  
  //--- scale
  static boolean scaleNewDataReady = 0;
  if (scale.update()) scaleNewDataReady = true;
  if (scaleNewDataReady) {
    scaleNewDataReady = 0;
    float reading = scale.getData();
    measurement_weight = (reading * calibration_w / calibration_r);
  }

  //--- work logic
  now = millis();
  if (global_state == s_pid) loop_pid(now);
  if (global_state == s_bootstrap) loop_pid(now);
  if (global_state == s_flow) loop_flow(now);  
  if (global_state == s_cool) loop_cool(now);  
  if (global_state == s_brew) loop_brew(now);  
  
  //--- HO
  if (measurement_temperature > 105) {
    perform__error();
  }

  //--- actuators
  if (signal_heater)
    mx__runHeater(signal_heater);
  else
    mx__stopHeater();
  if (signal_pump)
    mx__runPump(signal_pump);
  else
    mx__stopPump();
  if (signal_spray)
    mx__startSpray();
  else
    mx__stopSpray();

  if (update_counter <= 0) {
    update_counter = update_ref;  
  } else {
    update_counter--;
  }

  WDT.refresh();
  delay(10);
}
