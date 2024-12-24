#include <main.h>
#include <usb_nsgamepad.h>

constexpr bool print_keycodes = 
#ifdef DEBUG
    true;
#else
    false;
#endif

#define DPAD true

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

uint8_t max_key = 0;
std::array<bind, 256> keymap;

uint32_t active_keys;

uint8_t dpad_bits = 0;

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

// std::array<std::pair<uint8_t, uint16_t>, 14> defaults_ns = {{
//     //
//     { 0x1A, 0xff00 + (1 << 0) }, // W      -> D-Pad Up
//     { 0x16, 0xff00 + (1 << 2) }, // S      -> D-Pad Down
//     //
//     { 0x04, 0xff00 + (1 << 3) }, // A      -> D-Pad Left
//     { 0x07, 0xff00 + (1 << 1) }, // D      -> D-Pad Right
//     //
//     { 0x34, NSButton_Minus }, // '      -> Window
//     { 0x28, NSButton_Plus }, // Enter  -> Menu
//     { 0x0B, NSButton_LeftTrigger }, // H      -> LT
//     { 0x0D, NSButton_RightTrigger }, // J      -> LT + LB
//     { 0x0E, NSButton_LeftThrottle }, // K      -> RB
//     { 0x14, NSButton_RightThrottle }, // L      -> LB
//     { 0x1C, NSButton_B }, // Y      -> B
//     { 0x2C, NSButton_Y }, // U      -> Y
//     { 0x0C, NSButton_A }, // I      -> A
//     { 0x12, NSButton_X }  // O      -> X
// }};

std::array<std::pair<uint8_t, uint16_t>, 8> defaults_ns = {{
    //
    { 0x2c, 0xff00 + (1 << 0) }, // Space -> Up
    { 0x51, 0xff00 + (1 << 2) }, // Down
    //
    { 0x50, 0xff00 + (1 << 3) }, // Left
    { 0x4f, 0xff00 + (1 << 1) }, // Right
    //
    { 0x1d, NSButton_Y }, // Z -> B
    { 0x1b, NSButton_X }, // X -> A
    { 0x06, NSButton_B }, // C -> RB
    { 0x19, NSButton_A }
    // leaving the rest here for the sake of it
    // { 0x0D, NSButton_RightTrigger }, // J      -> LT + LB
    // { 0x0E, NSButton_LeftThrottle }, // K      -> RB
    // { 0x14, NSButton_RightThrottle }, // L      -> LB
    // { 0x1C, NSButton_B }, // Y      -> B
    // { 0x2C, NSButton_Y }, // U      -> Y
    // { 0x0C, NSButton_A }, // I      -> A
    // { 0x12, NSButton_X }  // O      -> X
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
        // Right
        Vec2<int16_t> { 17, 6 + 9  },
        // Down
        Vec2<int16_t> { 9,  6 + 17 },
        // Left
        Vec2<int16_t> { 1,  6 + 9  },
    };

    const auto top_row = {
        NSButton_Y,
        NSButton_B,
        NSButton_A,
        NSButton_X,
    };

    const auto bot_row = {
        NSButton_LeftThrottle,
        NSButton_RightThrottle,
        NSButton_LeftTrigger,
        NSButton_RightTrigger
    };

    auto key_pressed = [&](size_t key) {
        return (active_keys & (1u << key)) == (1u << key);
    };

    auto dpad_pressed = [&](size_t key) {
        return (dpad_bits & (1u << key)) == (1u << key);
    };

    /*
     * These rely on the enums being contiguous unless
     * otherwise stated, which is just convenient. 
     */
    for (size_t i = 0; i < 4; i++) {
        auto vec = dpad_coords[i];

        if (dpad_pressed(i)) {
            display.fillRect(vec.x, vec.y, 8, 8, 1);
        } else {
            display.drawRect(vec.x, vec.y, 8, 8, 1);
        }
    }
    enumerate(top_row, [&](int i, auto v) {
        if (key_pressed(to_underlying(v))) {
            display.fillCircle(static_cast<int16_t>(40 + 15 * i), 10, 6, int16_t { 1 });
        } else {
            display.drawCircle(static_cast<int16_t>(40 + 15 * i), 10, 6, int16_t { 1 });
        }
    });

    enumerate(bot_row, [&](int i, auto v) {
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

    NSGamepad.begin();

    print("Attached\n\n");

    print("Select the button to bind to %s!\nClick ESC for default binds.", keys[max_key]);
}

void loop() {
    host.Task();

#if DPAD
    const std::array<uint8_t, 16> dpad_map = {
        NSGAMEPAD_DPAD_CENTERED,  // 0000 All dpad buttons up
        NSGAMEPAD_DPAD_UP,        // 0001 direction UP
        NSGAMEPAD_DPAD_RIGHT,     // 0010 direction RIGHT
        NSGAMEPAD_DPAD_UP_RIGHT,  // 0011 direction UP RIGHT
        NSGAMEPAD_DPAD_DOWN,      // 0100 direction DOWN
        NSGAMEPAD_DPAD_CENTERED,  // 0101 DOWN + UP => NONE
        NSGAMEPAD_DPAD_DOWN_RIGHT,// 0110 direction DOWN RIGHT
        NSGAMEPAD_DPAD_RIGHT,     // 0111 DOWN + RIGHT + UP => RIGHT
        NSGAMEPAD_DPAD_LEFT,      // 1000 direction LEFT
        NSGAMEPAD_DPAD_UP_LEFT,   // 1001 direction UP LEFT
        NSGAMEPAD_DPAD_CENTERED,  // 1010 LEFT + RIGHT => NONE
        NSGAMEPAD_DPAD_UP,        // 1011 LEFT + RIGHT + UP => UP
        NSGAMEPAD_DPAD_DOWN_LEFT, // 1100 direction DOWN LEFT
        NSGAMEPAD_DPAD_LEFT,      // 1101 DOWN + LEFT + UP => LEFT
        NSGAMEPAD_DPAD_DOWN,      // 1110 LEFT + RIGHT + DOWN => DOWN
        NSGAMEPAD_DPAD_CENTERED,  // 1111 ALL => NONE
    };

    NSGamepad.dPad(static_cast<int8_t>(dpad_map[dpad_bits]));
#else
    auto pressed = [&](uint8_t v) {
        return (dpad_bits & (1 << v)) == (1 << v);
    };

    NSGamepad.leftYAxis(static_cast<uint8_t>(127 * (1 + -1 * pressed(0) + pressed(2))));
    NSGamepad.leftXAxis(static_cast<uint8_t>(127 * (1 + -1 * pressed(3) + pressed(1))));
#endif

    NSGamepad.loop();
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

            // for (auto& pair : defaults) {
            //     bind_key(std::get<0>(pair), std::get<1>(pair));
            // }
            for (auto& pair : defaults_ns) {
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

        if ((key & 0xff00) == 0) {
            NSGamepad.press(static_cast<uint8_t>(key));
            active_keys |= 1u << key;
        } else {
            dpad_bits |= (key & ~0xff00);
        }
        // XInput.press(key);

        if (!print_keycodes) {
            show_controller();
        }
        // print("hit key %x\n", key);
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
        // XInput.release(key);
        // NSGamepad.release(key);
        if ((key & 0xff00) == 0) {
            NSGamepad.release(static_cast<uint8_t>(key));
            active_keys &= ~(1u << key);
        } else {
            dpad_bits ^= (key & ~0xff00);
        }

        if (!print_keycodes) {
            show_controller();
        }
        // print("unhit key %x\n", key);
    });

    if (print_keycodes) {
        print("on_raw_release keycode: %x\n", keycode);
    }
}

void on_press(int key)
{
    if (!print_keycodes) {
        return;
    }

    if (isAscii((char) key)) {
        print("key '%c': %d\n", static_cast<char>(key), key);
    } else {
        print("key: %d\n", key);
    }
}
