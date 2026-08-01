#pragma once

#include <Arduino.h>

static constexpr bool SERIAL_DEBUG = true;
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr bool BLUETOOTH_KEYBOARD_ENABLED = true;
static constexpr char BLUETOOTH_DEVICE_NAME[] = "CYD Word Games";

static constexpr uint8_t TFT_ROTATION = 1;
static constexpr uint8_t TFT_BACKLIGHT_PIN = 21;
static constexpr bool TFT_BACKLIGHT_ON = HIGH;

// Backlight dims after this idle period; any touch or key wakes it again.
static constexpr uint32_t BACKLIGHT_DIM_AFTER_MS = 60000;
static constexpr uint8_t BACKLIGHT_DIM_LEVEL = 25;    // PWM duty 0-255 while idle
static constexpr uint8_t BACKLIGHT_FULL_LEVEL = 255;  // PWM duty 0-255 while active

static constexpr uint8_t TOUCH_SCLK_PIN = 25;
static constexpr uint8_t TOUCH_MISO_PIN = 39;
static constexpr uint8_t TOUCH_MOSI_PIN = 32;
static constexpr uint8_t TOUCH_CS_PIN = 33;
static constexpr uint8_t TOUCH_IRQ_PIN = 36;
static constexpr uint8_t TOUCH_ROTATION = 1;

static constexpr int16_t TOUCH_RAW_X_MIN = 200;
static constexpr int16_t TOUCH_RAW_X_MAX = 3700;
static constexpr int16_t TOUCH_RAW_Y_MIN = 240;
static constexpr int16_t TOUCH_RAW_Y_MAX = 3800;
static constexpr int16_t TOUCH_PRESSURE_MIN = 150;
static constexpr bool TOUCH_DIAGNOSTICS = true;
static constexpr uint16_t TOUCH_DEBOUNCE_MS = 70;
static constexpr uint16_t TOUCH_RELEASE_STABLE_MS = 180;
static constexpr uint16_t TOUCH_DIAGNOSTIC_INTERVAL_MS = 250;
