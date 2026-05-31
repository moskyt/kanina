void buzzer_helo() {
  buzzer.tone(440, 100);
  delay(150);
  buzzer.tone(0, 0);
  delay(20);
  buzzer.tone(554, 100);
  delay(150);
  buzzer.tone(0, 0);
  delay(20);
  buzzer.tone(659, 100);
  delay(150);
  buzzer.tone(0, 0);
}

void buzzer_init() {
  buzzer.tone(440, 100);
  delay(150);
  buzzer.tone(0, 0);
  delay(20);
  buzzer.tone(554, 100);
  delay(150);
  buzzer.tone(0, 0);
  delay(20);
  buzzer.tone(659, 100);
  delay(150);
  delay(20);
  buzzer.tone(554, 100);
  delay(150);
  buzzer.tone(0, 0);
  delay(20);
  buzzer.tone(659, 100);
  delay(150);
  buzzer.tone(0, 0);
}

void buzzer_start_preheat() {
  buzzer.tone(440, 100);
  delay(200);
  buzzer.tone(0, 0);
  delay(50);
  buzzer.tone(440, 100);
  delay(200);
  buzzer.tone(0, 0);
  delay(50);
  buzzer.tone(440, 100);
  delay(200);
  buzzer.tone(0, 0);
}

void buzzer_start_brew() {
  buzzer.tone(440, 100);
  delay(200);
  buzzer.tone(0, 0);
  delay(50);
  buzzer.tone(700, 100);
  delay(200);
  buzzer.tone(0, 0);
  delay(50);
  buzzer.tone(440, 100);
  delay(200);
  buzzer.tone(0, 0);
}

void buzzer_start_bootstrap() {
  buzzer.tone(440, 100);
  delay(200);
  buzzer.tone(0, 0);
  delay(50);
  buzzer.tone(440, 100);
  delay(200);
  buzzer.tone(0, 0);
}