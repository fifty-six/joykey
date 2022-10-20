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

#include <USBHost_t36.h>

#include <LiquidCrystal_I2C.h>

#define ENABLE_LOG4ARDUINO
#include <log4arduino.h>
#pragma GCC diagnostic pop

#include <array>
#include <algorithm>

// We need an USBHIDParser for the USBHost in order for KeyboardController to be
// able to read our input, I believe if we have multiple it'll better handle
// keyboards that pretend to be serveral devices, but this library isn't super
// well documented.
USBHost host;

USBHub hub(host);
KeyboardController kb(host);

USBHIDParser hid(host);
// I *believe* this is for stuff like the K95 which pretends to be 2 keyboards.
USBHIDParser hid_extra(host);

// Bound all of the keys
bool bound;

void OnPress(int);
void OnRawPress(uint8_t);
void OnRawRelease(uint8_t);

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

uint8_t max_key = 1;
std::array<bind, 256> keymap;

std::array<const char*, 14> keys = {
    "D-Pad Up",
    "D-Pad Right",
    "D-Pad Down",
    "D-Pad Left",
    "Window",
    "Menu",
    "LT",
    "LT + LB",
    "RB",
    "LB",
    "B",
    "Y",
    "A",
    "X"
};

#define P std::make_pair
std::array<std::pair<uint8_t, uint8_t>, 14> defaults = {
    P(0x2C, 1),     // Space  -> D-Pad Up
    P(0x07, 2),     // D      -> D-Pad Right
    P(0x16, 3),     // S      -> D-Pad Down
    P(0x04, 4),     // A      -> D-Pad Left
    P(0x34, 5),     // '      -> Window
    P(0x28, 6),     // Enter  -> Menu
    P(0x0B, 7),     // H      -> LT
    P(0x0D, 8),     // J      -> LT + LB
    P(0x0E, 9),     // K      -> RB
    P(0x14, 0xA),   // L      -> LB
    P(0x1C, 0xB),   // Y      -> B
    P(0x18, 0xC),   // U      -> Y
    P(0x0C, 0xD),   // I      -> A
    P(0x12, 0xE)    // O      -> X
};
#undef MP

template <typename T, typename... Ts>
void print(T str, Ts... args) {
    // Avoid issues with C++ varadic macros requiring more than 1 argument
    if (sizeof...(args) > 0) {
        LOG(str, args...);
    } else {
        FLOGS(str);
    }
}

void setup() {
    host.begin();
    Serial.begin(9600);
    log4arduino_init(&Serial);

    print("Attaching...");

    kb.attachPress(OnPress);
    kb.attachRawPress(OnRawPress);
    kb.attachRawRelease(OnRawRelease);

    print("Attached\n");

    print("Select the button to bind to %s!", keys[max_key]);
}

void loop() {
    host.Task();
}

void Bind(uint8_t keycode) {

    auto bind_key = [&](auto kb_key, auto joy_key) {
        print("Binding %d (%x) to %x = %s!",
            kb_key,
            kb_key,
            joy_key,
            keys.size() > static_cast<size_t>(joy_key - 1)
                ? keys[static_cast<size_t>(joy_key - 1)]
                : "unknown"
        );

        keymap[kb_key] = bind { joy_key };
    };

    // Esc to finish binding
    if (keycode == 0x29) {
        bound = true;

        if (max_key == 1) {
            print("No bindings set, setting to defaults!");

            for (auto& pair : defaults) {
                bind_key(std::get<0>(pair), std::get<1>(pair));
            }
        }

        print("ESC clicked! Finished binding!");
        return;
    }

    // We cap at 32 buttons
    if (max_key >= 32) {
        bound = true;
        print("Out of keys, finishing binding!");
        return;
    }

    if (keymap[keycode].has_value()) {
        print("Already have key: %x!", keycode);
        return;
    }

    bind_key(keycode, max_key);
    ++max_key;

    if (keys.size() > static_cast<size_t>(max_key - 1)) {
        print("Select the button to bind to %s!", keys[static_cast<size_t>(max_key - 1)]);
    }
}

void OnRawPress(uint8_t keycode) {
    if (!bound) {
        Bind(keycode);
        return;
    }

    keymap[keycode].map([&](auto key) {
        print("Pressing joystick button %s!", keys[static_cast<size_t>(key - 1)]);
        Joystick.button(key, true);
    });

    print("OnRawPress keycode: %x", keycode);
}
void OnRawRelease(uint8_t keycode) {
    if (!bound) {
        return;
    }

    keymap[keycode].map([&](auto key) {
        Joystick.button(key, false);
    });

    print("OnRawRelease keycode: %x", keycode);
}

void OnPress(int key)
{
    if (isAscii((char) key)) {
        print("key '%c': %d", static_cast<char>(key), key);
    } else {
        print("key: %d", key);
    }
}
