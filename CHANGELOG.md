# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] - 2026-03-22

### Added
- **Battery Protection System**:
  - Implemented voltage reading logic from pin A0 (assuming a 100k/100k voltage divider on VIN).
  - Condition: USB Power detection (VIN < 1.0V) bypasses battery protection.
  - Condition: Low Battery display saving (VIN < 3.4V but >= 1.0V) gracefully powers down the OLED display using the `SSD1306_DISPLAYOFF` I2C command to save battery.
  - Condition: Critical Battery deep sleep (VIN < 3.3V but >= 1.0V) puts the ESP8266 in infinite deep sleep (`ESP.deepSleep(0)`) to prevent battery over-discharge and hardware damage.

### Changed
- Refactored `loop()` to incorporate conditional checks for the OLED display. The UI rendering block is now skipped completely when the display is physically toggled off, optimizing processor execution time and further extending battery life.
- Maintained MQTT publishing even with the screen off as long as the voltage is above 3.3V.

### Fixed
- Addressed energy bleeding on minimal voltage levels by using hardware-level commands rather than merely drawing blank screens.
