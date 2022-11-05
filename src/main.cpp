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

void on_press(int);
void on_raw_press(uint8_t);
void on_raw_release(uint8_t);

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

uint8_t max_key = 0;
std::array<bind, 256> keymap;

uint32_t active_keys;

std::array<const char*, 17> keys = {
    "B_LOGO = 0",
    "B_A = 1",
    "B_B = 2",
    "B_X = 3",
    "B_Y = 4",
    "B_LB = 5",
    "B_RB = 6",
    "B_BACK = 7",
    "B_START = 8",
    "B_L3 = 9",
    "B_R3 = 10",
    "DPAD_UP = 11",
    "DPAD_DOWN = 12",
    "DPAD_LEFT = 13",
    "DPAD_RIGHT = 14",
    "TRIGGER_LEFT = 15",
    "TRIGGER_RIGHT = 16"
};

/*
 * Q         Y U I O
 * A S D     H J K L
 * 
 *         [  Space  ]
 */
std::array<std::pair<uint8_t, XInputControl>, 14> defaults = {{
    { 0x2C, XInputControl::DPAD_UP      }, // Space  -> D-Pad Up
    { 0x07, XInputControl::DPAD_RIGHT   }, // D      -> D-Pad Right
    { 0x16, XInputControl::DPAD_DOWN    }, // S      -> D-Pad Down
    { 0x04, XInputControl::DPAD_LEFT    }, // A      -> D-Pad Left
    { 0x34, XInputControl::BUTTON_BACK  }, // '      -> Window
    { 0x28, XInputControl::BUTTON_START }, // Enter  -> Menu
    { 0x0B, XInputControl::TRIGGER_LEFT }, // H      -> LT
    { 0x0D, XInputControl::JOY_LEFT     }, // J      -> LT + LB
    { 0x0E, XInputControl::BUTTON_RB    }, // K      -> RB
    { 0x14, XInputControl::BUTTON_LB    }, // L      -> LB
    { 0x1C, XInputControl::BUTTON_B     }, // Y      -> B
    { 0x18, XInputControl::BUTTON_Y     }, // U      -> Y
    { 0x0C, XInputControl::BUTTON_A     }, // I      -> A
    { 0x12, XInputControl::BUTTON_X     }  // O      -> X
}};

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

template <typename T, typename F>
void enumerate(std::initializer_list<T> l, F&& f) {
    int index = 0;
    for (auto value : l) {
        f(index++, value);
    }
}

template <typename T>
auto to_underlying(T v) -> std::underlying_type_t<T> {
    return static_cast<std::underlying_type_t<T>>(v);
}

// NohBoard style representation of the controller
//
//   ||   [A ] [B ] [Y ] [X ]
// |||||| [LB] [RB] [LT] [RT]
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
        // Down
        Vec2<int16_t> { 9,  6 + 17 },
        // Left
        Vec2<int16_t> { 1,  6 + 9  },
        // Right
        Vec2<int16_t> { 17, 6 + 9  },
    };

    auto key_pressed = [&](size_t key) {
        return (active_keys & (1u << key)) == (1u << key);
    };

    /* using enum XInputControl */ 

    /*
     * These rely on the enums being contiguous unless
     * otherwise stated, which is just convenient. 
     */
    enumerate({ XInputControl::DPAD_UP, XInputControl::DPAD_DOWN, XInputControl::DPAD_LEFT, XInputControl::DPAD_RIGHT }, [&](int i, auto v) {
        auto vec = dpad_coords[static_cast<size_t>(i)];
        if (key_pressed(to_underlying(v))) {
            display.fillRect(vec.x, vec.y, 8, 8, 1);
        } else {
            display.drawRect(vec.x, vec.y, 8, 8, 1);
        }
    });

    enumerate({ XInputControl::BUTTON_A, XInputControl::BUTTON_B, XInputControl::BUTTON_Y, XInputControl::BUTTON_X }, [&](int i, auto v) {
        if (key_pressed(to_underlying(v))) {
            display.fillCircle(static_cast<int16_t>(40 + 15 * i), 10, 6, int16_t { 1 });
        } else {
            display.drawCircle(static_cast<int16_t>(40 + 15 * i), 10, 6, int16_t { 1 });
        }
    });

    enumerate({ XInputControl::BUTTON_LB, XInputControl::BUTTON_RB, XInputControl::TRIGGER_LEFT, XInputControl::TRIGGER_RIGHT }, [&](int i, auto v) {
        if (key_pressed(to_underlying(v))) {
            display.fillCircle(static_cast<int16_t>(40 + 15 * i), 24, 6, int16_t { 1 });
        } else {
            display.drawCircle(static_cast<int16_t>(40 + 15 * i), 24, 6, int16_t { 1 });
        }
    });

    display.display();
}

void setup() {
    host.begin();
    Serial.begin(9600);

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.display();

    print("Attaching...\n");

    kb.attachPress(on_press);
    kb.attachRawPress(on_raw_press);
    kb.attachRawRelease(on_raw_release);

    XInput.begin();

    print("Attached\n\n");

    print("Select the button to bind to %s!\nClick ESC for default binds.", keys[max_key]);
}

void loop() {
    host.Task();
}

void bind_key(uint8_t keycode) {
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

        if (max_key == 0) {
            print("No bindings set, setting to defaults!\n");

            for (auto& pair : defaults) {
                bind_key(std::get<0>(pair), std::get<1>(pair));
            }
        }

        print("ESC clicked! Finished binding!\n");
        return;
    }

    // We cap at 17 buttons
    if (max_key >= 16) {
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

    if (keys.size() > static_cast<size_t>(max_key)) {
        print(DisplayStatus::Hold, "Select the button to bind to %s!\n", keys[max_key]);
    }
}

void on_raw_press(uint8_t keycode) {
    if (!bound) {
        bind_key(keycode);
        return;
    }

    keymap[keycode].map([&](auto key) {
        XInput.press(key);

        active_keys |= 1u << key;

        if (!print_keycodes) {
            show_controller();
        }
    });

    if (print_keycodes) {
        print("on_raw_press keycode: %x\n", keycode);
    }
}
void on_raw_release(uint8_t keycode) {
    if (!bound) {
        return;
    }

    keymap[keycode].map([&](auto key) {
        XInput.release(key);

        active_keys &= ~(1u << key);

        if (!print_keycodes) {
            show_controller();
        }
    });

    if (print_keycodes) {
        print("on_raw_release keycode: %x\n", keycode);
    }
}

void on_press(int key)
{
    if (!print_keycodes)
        return;

    if (isAscii((char) key)) {
        print("key '%c': %d\n", static_cast<char>(key), key);
    } else {
        print("key: %d\n", key);
    }
}
