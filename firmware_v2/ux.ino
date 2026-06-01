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