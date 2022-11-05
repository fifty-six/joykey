#pragma once

#include <stdint.h>

/*
 * Disable warnings for third-party libraries 
 * because platformio includes them with -I
 * as opposed to -isystem
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#include <Arduino.h>
#include <Keyboard.h>

#include <usb_rawhid.h>
#include <usb_joystick.h>
#include <usb_xinput.h>

#include <USBHost_t36.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <XInput.h>
#pragma GCC diagnostic pop

#include <array>
#include <algorithm>

void on_raw_release(uint8_t keycode);
void on_raw_press(uint8_t keycode);

void on_press(int key);

void bind_key(uint8_t keycode);

class bind {
    uint8_t key_ = 255;

public:
    template<typename F>
    constexpr auto map(F&& f) {
        if (key_ != 255) {
            f(key_);
        }
    }

    constexpr auto has_value() -> bool {
        return key_ != 255;
    }

    constexpr bind() : key_{255} {};
    constexpr bind(uint8_t v) : key_{v} {};
};

