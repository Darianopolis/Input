#include "example.hpp"

#include <libevdev/libevdev-uinput.h>

namespace input::example
{
    static EvInputDevice* keyboard_in;
    static libevdev_uinput* keyboard_uinput = nullptr;

    static
    void create_virtual_keyboard()
    {
        auto keyboard_out = libevdev_new();
        defer { libevdev_free(keyboard_out); };

        libevdev_set_name(keyboard_out, "Virtual Keyboard");
        libevdev_set_id_vendor(keyboard_out, 0x1234);
        libevdev_set_id_product(keyboard_out, 0x6385);
        libevdev_set_id_version(keyboard_out, 0);

        log_info("Configuring virtual keyboard");

        for (auto[type, max_code] : {
            std::pair{EV_KEY, KEY_MAX},
        }) {
            for (int code = 0; code <= max_code; ++code) {
                if (libevdev_has_event_code(keyboard_in->get_device(), type, code)) {
                    // log_info("  Enabling {}: {}", libevdev_event_type_get_name(type), libevdev_event_code_get_name(type, code));
                    libevdev_enable_event_code(keyboard_out, type, code, nullptr);
                }
            }
        }

        unix_check_ne(libevdev_uinput_create_from_device(keyboard_out, LIBEVDEV_UINPUT_OPEN_MANAGED, &keyboard_uinput));
    }

    static
    void keyboard_input_callback(EvInputDevice* device, EvDevInputDeviceEventType ev_type, input_event ev)
    {
        if (ev_type == EvDevInputDeviceEventType::DeviceRemoved) {
            log_info("Keyboard removed...");
            libevdev_uinput_destroy(keyboard_uinput);
            keyboard_in = nullptr;
            keyboard_uinput = nullptr;
            return;
        }

        auto press = [&](int code) {
            unix_check_ne(libevdev_uinput_write_event(keyboard_uinput, EV_KEY, code, 1));
            unix_check_ne(libevdev_uinput_write_event(keyboard_uinput, EV_SYN, SYN_REPORT, 0));
        };

        auto release = [&](int code) {
            unix_check_ne(libevdev_uinput_write_event(keyboard_uinput, EV_KEY, code, 0));
            unix_check_ne(libevdev_uinput_write_event(keyboard_uinput, EV_SYN, SYN_REPORT, 0));
        };

        auto type = [&](int code) {
            press(code);
            release(code);
        };

        static bool alt_queued = false;
        static bool alt_down = false;

        if (ev.type == EV_KEY && ev.code == KEY_S && libevdev_get_event_value(keyboard_in->get_device(), EV_KEY, KEY_LEFTALT) && ev.value > 0) {
            if (ev.value == 1) {
                log_trace("Mapping (alt+S -> \"std::\")");

                type(KEY_S);
                type(KEY_T);
                type(KEY_D);
                press(KEY_LEFTSHIFT);
                type(KEY_SEMICOLON);
                type(KEY_SEMICOLON);
                release(KEY_LEFTSHIFT);

                alt_queued = false;
            }
        } else if (ev.type == EV_KEY && ev.code == KEY_W && libevdev_get_event_value(keyboard_in->get_device(), EV_KEY, KEY_LEFTALT) && ev.value > 0) {
            if (ev.value == 1) {
                log_trace("Mapping (alt+W -> \"->\")");

                type(KEY_MINUS);
                press(KEY_LEFTSHIFT);
                type(KEY_DOT);
                release(KEY_LEFTSHIFT);

                alt_queued = false;
            }
        } else if (ev.type == EV_KEY && ev.code == KEY_D && libevdev_get_event_value(keyboard_in->get_device(), EV_KEY, KEY_LEFTALT) && ev.value > 0) {
            if (ev.value == 1) {
                log_trace("Mapping (alt+D -> \"::\")");

                press(KEY_LEFTSHIFT);
                type(KEY_SEMICOLON);
                type(KEY_SEMICOLON);
                release(KEY_LEFTSHIFT);

                alt_queued = false;
            }
        } else if (ev.type == EV_KEY) {
            if (ev.code == KEY_LEFTALT) {
                if (ev.value) {
                    if (!alt_queued) {
                        log_trace("Queueing alt press");
                        alt_queued = true;
                    }
                } else {
                    if (alt_queued && !alt_down) {
                        log_trace("Tapping alt on release");
                        type(KEY_LEFTALT);
                        alt_queued = false;
                    } else {
                        log_trace("Releasing alt");
                        release(KEY_LEFTALT);
                        alt_queued = false;
                        alt_down = false;
                    }
                }
            } else {
                if (alt_queued && !alt_down && ev.value == 1) {
                    log_trace("Deferred activation of alt");
                    press(KEY_LEFTALT);
                    alt_down = true;
                }
                unix_check_ne(libevdev_uinput_write_event(keyboard_uinput, ev.type, ev.code, ev.value));
            }
        } else if (ev.type != EV_MSC || ev.type != MSC_SCAN) {
            unix_check_ne(libevdev_uinput_write_event(keyboard_uinput, ev.type, ev.code, ev.value));
        }
    }

    void init_keyboard(int argc, char* argv[])
    {
        evdev_subsystem->register_device_filter([=](EvInputDevice* device) -> bool {
            if (keyboard_in || !device->has_keyboard()) return false;

            auto name = device->get_name();
            log_warn("Keyboard: {}", name);

            if ("Wooting Wooting Two HE (ARM)"sv == name) {
                keyboard_in = device;
                log_info("  Selected");
                create_virtual_keyboard();
                keyboard_in->grab();
                evdev_subsystem->register_input_device_event_callback(keyboard_in, keyboard_input_callback);
                return true;
            }

            return false;
        });
    }
}
