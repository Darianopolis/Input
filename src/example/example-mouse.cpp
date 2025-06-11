#include "example.hpp"

#include "input/math.hpp"

#include <libevdev/libevdev-uinput.h>

#include <chrono>

namespace input::example
{
    static EvInputDevice* mouse_in;
    static libevdev_uinput* mouse_out_uinput = nullptr;

    static
    void create_virtual_mouse()
    {
        auto mouse_out = libevdev_new();
        defer { libevdev_free(mouse_out); };

        libevdev_set_name(mouse_out, "Virtual Mouse");
        libevdev_set_id_vendor(mouse_out, 0x1234);
        libevdev_set_id_product(mouse_out, 0x5678);
        libevdev_set_id_version(mouse_out, 0);

        log_info("Configuring virtual mouse");

        for (auto[type, max_code] : {
                std::pair{EV_KEY, KEY_MAX},
                std::pair{EV_REL, REL_MAX},
                std::pair{EV_ABS, ABS_MAX},
        }) {
            for (int code = 0; code <= max_code; ++code) {
                if (libevdev_has_event_code(mouse_in->get_device(), type, code)) {
                    log_info("  Enabling {}: {}", libevdev_event_type_get_name(type), libevdev_event_code_get_name(type, code));
                    libevdev_enable_event_code(mouse_out, type, code, nullptr);
                }
            }
        }

        libevdev_enable_event_code(mouse_out, EV_KEY, KEY_LEFTCTRL, nullptr);
        libevdev_enable_event_code(mouse_out, EV_KEY, KEY_F22, nullptr);

        unix_check_ne(libevdev_uinput_create_from_device(mouse_out, LIBEVDEV_UINPUT_OPEN_MANAGED, &mouse_out_uinput));
    }

    static vec2 delta_in = 0.0;
    static vec2 delta_out = 0.0;

#define NOISY_EVENTS 0
#define REPORT_STATS 0

#if REPORT_STATS
    namespace chr = std::chrono;
    static struct {
        chr::steady_clock::time_point last_report = {};
        chr::steady_clock::duration report_period = 250ms;
        vec2 max_sens = {};

        vec2 moved = {};
        double distance = {};
        double dots_per_meter = 1600.0 / 0.0254;
    } stats;
#endif

    enum class AccelMode
    {
        ComponentWise,
        Whole,
    };

    static
    auto apply_accel(vec2 delta, AccelMode mode) -> vec2
    {
        // Apply a linear mouse acceleration curve
        //
        // Offset - speed before acceleration is applied
        // Accel  - rate that sensitivity increases with motion
        // Mult   - total multplier for sensitivity
        //
        //      /
        //     / <- Accel
        // ___/
        //  ^-- Offset

        constexpr auto offset = 2.0;
        constexpr auto accel = 0.05;
        constexpr auto sens_mult = 1.0;

        vec2 sens;
        switch (mode) {
            break;case AccelMode::ComponentWise:
                sens = vec2(sens_mult) * (vec2(1) + (max(abs(delta), vec2(offset)) - vec2(offset)) * accel);
            break;case AccelMode::Whole: {
                auto speed = sqrt(delta.x * delta.x + delta.y * delta.y);
                sens = vec2(sens_mult * (1 + (max(speed, offset) - offset) * accel));
            }
            break;default:
                std::unreachable();
        }

#if REPORT_STATS
        stats.max_sens = max(stats.max_sens, sens);
#endif

        return sens * delta;
    };

    static
    void mouse_input_callback(EvInputDevice* device, EvDevInputDeviceEventType type, input_event ev)
    {
        if (type == EvDevInputDeviceEventType::DeviceRemoved) {
            raise_error("Mouse removed!");
        }

        if      (ev.type == EV_REL && ev.code == REL_X) delta_in.x += ev.value;
        else if (ev.type == EV_REL && ev.code == REL_Y) delta_in.y += ev.value;
        else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            // constexpr static auto accel_mode = AccelMode::ComponentWise;
            constexpr static auto accel_mode = AccelMode::Whole;

            delta_out += apply_accel(delta_in, accel_mode);
            // delta_out += delta_in;
            delta_in = {};

            auto i_move_delta = round_to_zero(delta_out);
            delta_out -= i_move_delta;

#if REPORT_STATS
            stats.moved += i_move_delta;
            stats.distance += mag(i_move_delta);
            if (stats.moved || stats.distance) {
                auto now = chr::steady_clock::now();
                if (now > stats.last_report + stats.report_period) {
                    auto delta_s = chr::duration_cast<chr::duration<double>>(now - stats.last_report).count();

                    log_info("delta ({:6}, {:6}) remainder ({:5.2f}, {:5.2f}) max sense ({}) speed {:.2f} cm/s",
                        stats.moved.x, stats.moved.y,
                        delta_out.x, delta_out.y,
                        accel_mode == AccelMode::ComponentWise
                            ? std::format("{:4.2f}, {:4.2f}", stats.max_sens.x, stats.max_sens.y)
                            : std::format("{:4.2f}", stats.max_sens.x),
                        (stats.distance / delta_s) * (100.0 / stats.dots_per_meter));
                    stats.last_report = now;
                    stats.max_sens = {};
                    stats.moved = {};
                    stats.distance = {};
                }
            }
#endif

            if (i_move_delta.x) unix_check_ne(libevdev_uinput_write_event(mouse_out_uinput, EV_REL, REL_X, int(i_move_delta.x)));
            if (i_move_delta.y) unix_check_ne(libevdev_uinput_write_event(mouse_out_uinput, EV_REL, REL_Y, int(i_move_delta.y)));

            unix_check_ne(libevdev_uinput_write_event(mouse_out_uinput, EV_SYN, SYN_REPORT, 0));
        } else {
            if (ev.type == EV_KEY && ev.code == BTN_EXTRA) {
                log_trace("Mouse, mapping (BTN_EXTRA -> KEY_LEFTCTRL) = {}", ev.value);
                unix_check_ne(libevdev_uinput_write_event(mouse_out_uinput, ev.type, KEY_LEFTCTRL, ev.value));
            } else if (ev.type == EV_KEY && ev.code == BTN_SIDE) {
                log_trace("Mouse, mapping (BTN_SIDE -> KEY_F22) = {}", ev.value);
                unix_check_ne(libevdev_uinput_write_event(mouse_out_uinput, ev.type, KEY_F22, ev.value));
            } else if (ev.type != EV_MSC || ev.type != MSC_SCAN) {
                unix_check_ne(libevdev_uinput_write_event(mouse_out_uinput, ev.type, ev.code, ev.value));
            }
        }
    }

    void init_mouse(int argc, char* argv[])
    {
        evdev_subsystem->register_device_filter([=](EvInputDevice* device) -> bool {
            if (mouse_in || !device->has_mouse()) return false;

            auto name = device->get_name();
            log_info("Mouse: {}", name);
            if ("Glorious Model O"sv == name) {
                mouse_in = device;
                log_info("  Selected!");
                create_virtual_mouse();
                mouse_in->grab();
                evdev_subsystem->register_input_device_event_callback(mouse_in, mouse_input_callback);
                return true;
            }
            return false;
        });
    }
}
