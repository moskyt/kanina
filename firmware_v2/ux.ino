void print_pid_serial() {
  Serial.print("PID ");
  Serial.print("I ");
  Serial.print(Input);
  Serial.print(" T ");
  Serial.print(pid_target);
  Serial.print(" S ");
  Serial.print(Setpoint);
  Serial.print(" S* ");
  Serial.print(AdjustedSetpoint);
  Serial.print(" O ");
  Serial.print(Output);
  Serial.print(" H ");
  Serial.print(signal_heater);
  Serial.print("         Kp ");
  Serial.print(heaterPID.GetKp());
  Serial.print(" Ki ");
  Serial.print(heaterPID.GetKi());
  Serial.print(" Kd ");
  Serial.print(heaterPID.GetKd());
  Serial.print(" U ");
  Serial.print(accumulated_heat);
  Serial.println("");
}

void print_brew_heatup_serial() {
  char buffer[1024];
  int ti = (int)(10 * Input);
  int tt = (int)(10 * pid_target);
  int ts = (int)(10 * Setpoint);
  int ta = (int)(10 * AdjustedSetpoint);
  int ou = (int)(10 * Output);
  snprintf(buffer, sizeof(buffer), "PID1 I %3d.%1d T %3d.%1d S %3d.%1d S* %3d.%1d O %4d.%1d H %3d        U %7ld %7ld    EQ %3d", 
    ti/10, ti%10, tt/10, tt%10, ts/10, ts%10, ta/10, ta%10, 
    ou/10, ou%10, signal_heater,          
    (long int)accumulated_heat, 
    (long int)adjusted_accumulated_heat, brew_steady_counter);
  Serial.println(buffer);
}

void print_brew_preheat_serial() {
  char buffer[1024];
  int ti = (int)(10 * Input);
  int tt = (int)(10 * pid_target);
  int ts = (int)(10 * Setpoint);
  int ta = (int)(10 * AdjustedSetpoint);
  int ou = (int)(10 * Output);
  snprintf(buffer, sizeof(buffer), "**** I %3d.%1d T %3d.%1d S %3d.%1d S* %3d.%1d O %4d.%1d H %3d", 
    ti/10, ti%10, tt/10, tt%10, ts/10, ts%10, ta/10, ta%10, 
    ou/10, ou%10, signal_heater);
  Serial.println(buffer);
}

void print_brew_flow_serial() {
  char buffer[1024];
  int ti = (int)(10 * Input);
  int tt = (int)(10 * pid_target);
  int ts = (int)(10 * Setpoint);
  int ta = (int)(10 * AdjustedSetpoint);
  int ou = (int)(10 * Output);
  snprintf(buffer, sizeof(buffer), "PID2 I %3d.%1d T %3d.%1d S %3d.%1d S* %3d.%1d O %+3d.%1d H %3d        U %7ld    W  %3d  /p %3d", 
    ti/10, ti%10, tt/10, tt%10, ts/10, ts%10, ta/10, ta%10, 
    ou/10, abs(ou)%10, signal_heater,          
    (long int)accumulated_heat, (int)measurement_weight, signal_pump);
  Serial.println(buffer);
}

void update_displays() {
  //--- show gauges
  int t = round(measurement_temperature);
  int n_temp;
  int t_min = 80;
  int t_b = brew_temperature;
  int t_a = t_b - 5;
  int t_c = t_b + 5;
  int t_max = 105;
  if (t < t_min)
    n_temp = 0;
  else if (t < t_a)
    n_temp = map(t, t_min,   t_a,   0,  64);
  else if (t < t_b)
    n_temp = map(t,   t_a,   t_b,  64, 128);
  else if (t < t_c)
    n_temp = map(t,   t_b,   t_c, 128, 196);
  else if (t < t_max)
    n_temp = map(t,   t_c, t_max, 196, 255);
  else
    n_temp = 255;
  analogWrite(PIN_METER_TEMP, n_temp);

  int n_power = signal_heater;
  analogWrite(PIN_METER_POWER, n_power);

  //--- print LED
  if (global_state == s_brew) {
    show_led_number(measurement_weight);
  } else {
    show_led_number(brew_target_weight);
  }

  //--- print OLED
  oled_display.clearDisplay();

  oled_display.setTextSize(1);
  oled_display.setTextColor(SSD1306_WHITE);
  oled_display.setCursor(0,0);

  char tmp[22];

  snprintf(tmp, sizeof(tmp), "Temp: %5.1f C", measurement_temperature);
  oled_display.println(tmp);

  float percentage_heater = 100.0 * signal_heater / 255.0;
  snprintf(tmp, sizeof(tmp), "Heat: %5.1f %%", percentage_heater);
  oled_display.println(tmp);

  int print_weight = round(measurement_weight);
  snprintf(tmp, sizeof(tmp), "Wght: %5d g", print_weight);
  oled_display.println(tmp);

  int percentage_pump = 100 * signal_pump;
  percentage_pump /= 255;
  snprintf(tmp, sizeof(tmp), "Pump: %5d %%", percentage_pump);
  if (signal_spray) {
    oled_display.print(tmp);
    oled_display.println(" + S");
  } else {
    oled_display.println(tmp);
  }
  
  if (global_state == s_pid) {
    oled_display.println("");
    oled_display.print("-> PID ");
    oled_display.print(pid_target);
    oled_display.println(" C");
  }
  if (global_state == s_bootstrap) {
    oled_display.println("");
    oled_display.print("-> WARM-UP ");
    oled_display.print(config__bootstrap_temperature);
    oled_display.println(" C");
  }
  if (global_state == s_brew) {
    oled_display.println("");
    oled_display.print("Brewing ");
    oled_display.print(brew_target_weight);
    oled_display.print(" g ");
    oled_display.print(brew_temperature);
    oled_display.print(" C ");
    oled_display.println("");

    oled_display.print("=> ");
    oled_display.println(brew_ux);
  }

  oled_display.display();
}

void show_led_number(int number) {
  char buf[5];
  // Right-align in a 4-character field, space-padded on the left
  snprintf(buf, sizeof(buf), "%4d", number);
  led_display.print(buf);
}

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
  Serial.println("Pressed button for long.");
  if (global_state == s_idle) {
    // start bootstrap!
    pid_target = config__bootstrap_temperature;
    Serial.print("Bootstrap requested: ");
    Serial.println(config__bootstrap_temperature);

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
    perform__bootstrap();
  } else 
  if (global_state == s_bootstrap) {
    perform__idle();
  }
  update_flag = true;
}

void handle_main(Button2& btn) {
  Serial.println("Pressed button.");

  if ((global_state == s_idle) || (global_state == s_bootstrap)) {
    // start brew!
    brew_temperature = config__brew_temperature;
    brew_base_power = config__brew_base_power;
    brew_preheat_time = config__brew_preheat_time;
    global_state = s_brew;
    brew_step = b_idle;
    buzzer_start_brew();
    neo_brew();
  } else
  if (global_state == s_brew) {
    perform__error();
  }
  update_flag = true;
}