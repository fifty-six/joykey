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

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#pragma GCC diagnostic pop

#include <array>
#include <algorithm>

constexpr bool print_keycodes = 
#ifdef DEBUG
    true;
#else
    false;
#endif

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

uint32_t active_keys;

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

Adafruit_SSD1306 display(128, 32, &Wire, -1, 1000000);

enum class DisplayStatus {
    Clear,
    Hold
};


void print(DisplayStatus clear, const char* str, va_list vl) {
    std::array<char, 1024> buf {};

    vsnprintf(buf.data(), buf.size(), str, vl);

    if (clear == DisplayStatus::Clear) {
        display.clearDisplay();
        display.setCursor(0, 0);
    }
    display.setTextColor(1);
    display.setTextSize(1);
    display.print(buf.data());
    display.display();

    Serial.write(buf.data());
}

template <typename... Ts>
[[gnu::format(printf, 1, 2)]]
void print(const char* str, ...) {
    va_list vl {};

    va_start(vl, str);
    print(DisplayStatus::Clear, str, vl);
    va_end(vl);
}

template <typename... Ts>
[[gnu::format(printf, 2, 3)]]
void print(DisplayStatus ds, const char* str, ...) {
    va_list vl {};

    va_start(vl, str);
    print(ds, str, vl);
    va_end(vl);
}

template <typename T>
struct Vec2 {
    T x;
    T y;

    Vec2(T x_, T y_) : x{x_}, y{y_} {};
};

// NohBoard style representation of the controller
//
//   ||   [B ] [Y  ] [A ] [X ]
// |||||| [LT] [LTB] [RB] [LB]
//   ||
//   
void show_controller() {
    display.clearDisplay();
    display.setTextColor(1);
    display.setTextSize(1);

    // Try to center this a bit given we have 128x32
    // Each is 8 big, so we this is 24 wide so start at 1 + 4
    // instead of 1 in theory, but beneath the display is a black
    // bar, so instead offset a *little* more underneath that.
    const std::array<Vec2<int16_t>, 4> dpad_coords {
        // Up
        Vec2<int16_t> { 9,  6 + 1  },
        // Right
        Vec2<int16_t> { 17, 6 + 9  },
        // Down
        Vec2<int16_t> { 9,  6 + 17 },
        // Left
        Vec2<int16_t> { 1,  6 + 9  },
    };

    auto key_pressed = [&](size_t key) {
        return (active_keys & (1u << key)) == (1u << key);
    };

    // D-Pad
    for (size_t i = 1; i < 5; i++) {
        auto v = dpad_coords[i - 1];
        if (key_pressed(i)) {
            display.fillRect(v.x, v.y, 8, 8, 1);
        } else {
            display.drawRect(v.x, v.y, 8, 8, 1);
        }
    }

    // Top row
    for (size_t i = 11; i <= 14; i++) {
        if (key_pressed(i)) {
            display.fillCircle(static_cast<int16_t>(40 + 15 * (i - 11)), 10, 6, int16_t { 1 });
        } else {
            display.drawCircle(static_cast<int16_t>(40 + 15 * (i - 11)), 10, 6, int16_t { 1 });
        }
    }

    // Bottom row
    for (size_t i = 7; i <= 10; i++) {
        if (key_pressed(i)) {
            display.fillCircle(static_cast<int16_t>(40 + 15 * (i - 7)), 24, 6, int16_t { 1 });
        } else {
            display.drawCircle(static_cast<int16_t>(40 + 15 * (i - 7)), 24, 6, int16_t { 1 });
        }
    }

    display.display();
}

void setup() {
    host.begin();
    Serial.begin(9600);

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.display();

    print("Attaching...\n");

    kb.attachPress(OnPress);
    kb.attachRawPress(OnRawPress);
    kb.attachRawRelease(OnRawRelease);

    print("Attached\n\n");

    print("Select the button to bind to %s!\nClick ESC for default binds.", keys[static_cast<size_t>(max_key - 1)]);
}

void loop() {
    host.Task();
}

void Bind(uint8_t keycode) {
    auto bind_key = [&](auto kb_key, auto joy_key) {
        print("Binding %d (%x) to %x = %s!\n",
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
            print("No bindings set, setting to defaults!\n");

            for (auto& pair : defaults) {
                bind_key(std::get<0>(pair), std::get<1>(pair));
            }
        }

        print("ESC clicked! Finished binding!\n");
        return;
    }

    // We cap at 32 buttons
    if (max_key >= 32) {
        bound = true;
        print(DisplayStatus::Hold, "Out of keys, finishing binding!\n");
        return;
    }

    if (keymap[keycode].has_value()) {
        print("Already have key: %x!\n", keycode);
        return;
    }

    bind_key(keycode, max_key);
    ++max_key;

    if (keys.size() > static_cast<size_t>(max_key - 1)) {
        print(DisplayStatus::Hold, "Select the button to bind to %s!\n", keys[static_cast<size_t>(max_key - 1)]);
    }
}

void OnRawPress(uint8_t keycode) {
    if (!bound) {
        Bind(keycode);
        return;
    }

    keymap[keycode].map([&](auto key) {
        Joystick.button(key, true);

        active_keys |= 1u << key;

        if (!print_keycodes) {
            show_controller();
        }
    });

    if (print_keycodes) {
        print("OnRawPress keycode: %x\n", keycode);
    }
}
void OnRawRelease(uint8_t keycode) {
    if (!bound) {
        return;
    }

    keymap[keycode].map([&](auto key) {
        Joystick.button(key, false);

        active_keys &= ~(1u << key);

        if (!print_keycodes) {
            show_controller();
        }
    });

    if (print_keycodes) {
        print("OnRawRelease keycode: %x\n", keycode);
    }
}

void OnPress(int key)
{
    if (!print_keycodes)
        return;

    if (isAscii((char) key)) {
        print("key '%c': %d\n", static_cast<char>(key), key);
    } else {
        print("key: %d\n", key);
    }
}
