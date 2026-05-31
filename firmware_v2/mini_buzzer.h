#pragma once
#include <Arduino.h>
#include <Wire.h>

// Minimal standalone driver for the Arduino Modulino Buzzer.
//
// Replaces the full Arduino_Modulino library, which we only used for the
// buzzer but which #defines LOW/HIGH/INPUT/OUTPUT and BLACK/WHITE, clashing
// with Button2 and Adafruit_SSD1306 (hence all the #undef / push_macro dance
// this lets us delete).
//
// Protocol, lifted from Arduino_Modulino ModulinoBuzzer/Module:
//   - lives on Wire1 (the Qwiic connector) on the UNO R4 WiFi
//   - 7-bit I2C address 0x1E  (firmware pinstrap 0x3C >> 1)
//   - tone:    write 8 bytes = uint32 freq (LE) + uint32 duration_ms (LE)
//   - silence: write 8 zero bytes  (i.e. tone(0, 0))
class MiniBuzzer {
public:
  explicit MiniBuzzer(TwoWire& wire = Wire1, uint8_t address = 0x1E)
    : _wire(&wire), _addr(address) {}

  // Returns true if the buzzer ACKs on the bus.
  bool begin() {
    _wire->begin();
    _wire->setClock(100000);  // Modulino firmware expects 100 kHz
    _wire->beginTransmission(_addr);
    return _wire->endTransmission() == 0;
  }

  void tone(uint32_t freq, uint32_t duration_ms) {
    uint8_t buf[8];
    memcpy(&buf[0], &freq, 4);
    memcpy(&buf[4], &duration_ms, 4);
    _wire->beginTransmission(_addr);
    _wire->write(buf, sizeof(buf));
    _wire->endTransmission();
  }

  void noTone() { tone(0, 0); }

private:
  TwoWire* _wire;
  uint8_t  _addr;
};
