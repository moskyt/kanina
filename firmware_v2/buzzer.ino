// Buzzer melodies.
//
// The Modulino buzzer plays a tone for the requested number of milliseconds and
// then stops on its own, so a melody is just a list of note frequencies: each
// note sounds for `note_ms`, then the buzzer stays silent for `gap_ms` before
// the next one. (NOTE_ prefix avoids the A0-A5 analog-pin macros.)

enum : uint16_t {
  NOTE_A4  = 440,
  NOTE_CS5 = 554,
  NOTE_E5  = 659,
  NOTE_F5  = 700,  // ~F5; kept at 700 to match the original tone
};

template <size_t N>
static void play_melody(const uint16_t (&notes)[N], uint16_t note_ms, uint16_t gap_ms) {
  for (size_t i = 0; i < N; i++) {
    if (i) delay(gap_ms);
    buzzer.tone(notes[i], note_ms);
    delay(note_ms);
  }
  buzzer.noTone();
}

void buzzer_helo() {
  static const uint16_t melody[] = { NOTE_A4, NOTE_CS5, NOTE_E5 };
  play_melody(melody, 100, 20);
}

void buzzer_update() {
  static const uint16_t melody[] = { NOTE_A4, NOTE_E5, NOTE_A4 };
  play_melody(melody, 100, 20);
}

void buzzer_init() {
  static const uint16_t melody[] = { NOTE_A4, NOTE_CS5, NOTE_E5, NOTE_CS5, NOTE_E5 };
  play_melody(melody, 100, 20);
}

void buzzer_start_preheat() {
  static const uint16_t melody[] = { NOTE_A4, NOTE_A4, NOTE_A4 };
  play_melody(melody, 100, 20);
}

void buzzer_start_brew() {
  static const uint16_t melody[] = { NOTE_A4, NOTE_F5, NOTE_A4 };
  play_melody(melody, 100, 20);
}

void buzzer_start_bootstrap() {
  static const uint16_t melody[] = { NOTE_A4, NOTE_A4 };
  play_melody(melody, 100, 20);
}
