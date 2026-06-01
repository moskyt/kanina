// Enter the IDLE state: set the state flag and paint the idle LED together,
// so the two never drift out of sync.
void perform__idle() {
  global_state = s_idle;
  neo_idle();
  mx__shutdown();
  update_flag = true;
}

void perform__bootstrap() {
  global_state = s_bootstrap;
  buzzer_start_bootstrap();
  neo_bootstrap();
}

void perform__error() {
  // stop brew!
  mx__shutdown();
  global_state = s_error;
  neo_error();
}
