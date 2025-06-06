#include "core.hpp"
#include "math.hpp"
#include "fd_event_bus.hpp"
#include "udev_subsystem.hpp"
#include "evdev_subsystem.hpp"

#include <print>
#include <iostream>
#include <thread>
#include <chrono>
#include <utility>
#include <cmath>

using namespace std::literals;

#include <libudev.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <unistd.h>
#include <fcntl.h>

namespace input
{
    enum class AccelMode
    {
        ComponentWise,
        Whole,
    };

    int cmain(int argc, char* argv[])
    {
        auto event_bus = FdEventBus::create();
        defer { unref(event_bus); };

        auto udev_subsystem = UDevSubsystem::create();
        defer { unref(udev_subsystem); };

        auto evdev_subsystem = EvDevSubsystem::create(event_bus, udev_subsystem);
        defer { unref(evdev_subsystem); };

        // Setup joystick listener

        udev_subsystem->watch_subsystem("hidraw");

        evdev_subsystem->register_device_filter([&](EvInputDevice* device) -> bool {
            if (!device->has_gamepad() && !device->has_joystick()) return false;
            log_warn("Found joystick with name: {}", device->get_name());

            if (device->get_vid() == 0x18d1 && device->get_pid() == 0x9400) {
                log_warn("  FOUND STADIA CONTROLLER");
                device->hide();
                device->grab();
                return true;
            }

            if (device->get_vid() != 0x0483 || device->get_pid() != 0x5710) return false;

            auto virt_joystick = libevdev_new();
            defer { libevdev_free(virt_joystick); };

            libevdev_set_name(virt_joystick, "Virtual Joystick");
            libevdev_set_id_bustype(virt_joystick, BUS_VIRTUAL);
            libevdev_set_id_vendor(virt_joystick, 0x1234);
            libevdev_set_id_product(virt_joystick, 0x1111);
            libevdev_set_id_version(virt_joystick, 0);

            input_absinfo info = {
                .value = 0,
                .minimum = -32767,
                .maximum =  32767,
                .resolution = 1,
            };
            libevdev_enable_event_code(virt_joystick, EV_ABS, ABS_X, &info);
            libevdev_enable_event_code(virt_joystick, EV_ABS, ABS_Y, &info);
            libevdev_enable_event_code(virt_joystick, EV_ABS, ABS_Z, &info);
            libevdev_enable_event_code(virt_joystick, EV_ABS, ABS_RX, &info);
            libevdev_enable_event_code(virt_joystick, EV_KEY, BTN_TRIGGER, nullptr);
            libevdev_enable_event_code(virt_joystick, EV_KEY, BTN_THUMB, nullptr);
            libevdev_enable_event_code(virt_joystick, EV_KEY, BTN_THUMB2, nullptr);
            libevdev_enable_event_code(virt_joystick, EV_KEY, BTN_TOP, nullptr);

            libevdev_uinput* joy_uinput = nullptr;
            unix_check_ne(libevdev_uinput_create_from_device(virt_joystick, LIBEVDEV_UINPUT_OPEN_MANAGED, &joy_uinput));

            device->hide();
            device->grab();

            evdev_subsystem->register_input_device_event_callback(device, [joy_uinput](EvInputDevice* device, EvDevInputDeviceEventType type, input_event ev) {
                if (type == EvDevInputDeviceEventType::DeviceRemoved) {
                    log_debug("Joystick removed");
                    libevdev_uinput_destroy(joy_uinput);
                    return;
                }

                std::array<double, 8> values{};
                std::array<int, 8> abs_names { ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_THROTTLE };
                for (int i = 0; i < abs_names.size(); ++i) {
                    auto info = libevdev_get_abs_info(device->get_device(), abs_names[i]);

                    auto range = double(info->maximum - info->minimum);
                    values[i] = (info->value - info->minimum) / range;
                    values[i] = (values[i] * 2) - 1;
                }

                log_info("Abs: {:5.2f} {:5.2f} {:5.2f} {:5.2f} {:5.2f} {:5.2f} {:5.2f} {:5.2f}",
                    values[0], values[1], values[2], values[3],
                    values[4], values[5], values[6], values[7]);

                auto maprange = [](double v, double in_low, double in_high, double out_low, double out_high, bool clamp = false) -> double
                {
                    if (clamp) {
                        if (v < in_low) return out_low;
                        if (v > in_high) return out_high;
                    }
                    auto p = (v - in_low) / (in_high - in_low);
                    return p * (out_high - out_low) + out_low;
                };

                auto deadzone = [](double v, double inner, double outer = 0.0) -> double
                {
                    if (std::abs(v) < inner) return 0;
                    return std::copysign(std::min((std::abs(v) - inner) / (1.0 - inner - outer), 1.0), v);
                };

                auto gamma = [](double v, double g) -> double
                {
                    return std::copysign(std::pow(std::abs(v), g), v);
                };

                auto rescale = [](double value) -> int
                {
                    return std::clamp(value, -1.0, 1.0) * 32767;
                };

                auto throttle = maprange(values[0], -1, 1, -0.1, 1);
                auto wheel = gamma(values[1], 2.0);
                auto brake_handbrake = deadzone(values[3], 0.05, 0.120);
                auto brake = std::max(-brake_handbrake, 0.0);
                auto handbrake = std::max(brake_handbrake, 0.0) * 2;

                unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_ABS, ABS_X, rescale(wheel)));
                unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_ABS, ABS_Y, rescale(throttle)));
                unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_ABS, ABS_Z, brake == 0 ? -1 : rescale(brake)));
                unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_ABS, ABS_RX, handbrake == 0 ? -1 : rescale(handbrake)));
                unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_KEY, BTN_TRIGGER, values[2] > 0.25));
                unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_KEY, BTN_THUMB, values[2] < -0.25));
                unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_KEY, BTN_THUMB2, values[4] > 0));
                unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_SYN, SYN_REPORT, 0));
            });

            return true;
        });

        // Scan for mouse

        EvInputDevice* mouse_in = nullptr;
        evdev_subsystem->register_device_filter([&](EvInputDevice* device) -> bool {
            if (mouse_in || !device->has_mouse()) return false;

            auto name = device->get_name();
            log_info("Mouse: {}", name);
            if (argc >= 2 && std::string_view(argv[1]) == name) {
                mouse_in = device;
                log_info("  Selected!");
                return true;
            }
            return false;
        });

        // Scan

        udev_subsystem->start(event_bus);

        // Create virtual mouse

        if (!mouse_in) {
            log_info("Select mouse and run again");
            return EXIT_SUCCESS;
        }

        libevdev_uinput* mouse_out_uinput = nullptr;
        defer { if (mouse_out_uinput) libevdev_uinput_destroy(mouse_out_uinput); };
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

            unix_check_ne(libevdev_uinput_create_from_device(mouse_out, LIBEVDEV_UINPUT_OPEN_MANAGED, &mouse_out_uinput));
        }

        std::this_thread::sleep_for(500ms);

        log_info("Virtual mouse configured");
        log_debug("  devnode = {}", libevdev_uinput_get_devnode(mouse_out_uinput));
        log_debug("  syspath = {}", libevdev_uinput_get_syspath(mouse_out_uinput));

        // Grab source mouse

        mouse_in->grab();

        // Map source mouse events to virtual output

        vec2 delta_in = 0.0;
        vec2 delta_out = 0.0;

#define NOISY_EVENTS 0
#define REPORT_STATS 0

#if REPORT_STATS
        namespace chr = std::chrono;
        struct {
            chr::steady_clock::time_point last_report = {};
            chr::steady_clock::duration report_period = 250ms;
            vec2 max_sens = {};

            vec2 moved = {};
            double distance = {};
            double dots_per_meter = 1600.0 / 0.0254;
        } stats;
#endif

        auto apply_accel = [&](vec2 delta, AccelMode mode) -> vec2
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

        evdev_subsystem->register_input_device_event_callback(mouse_in, [&](EvInputDevice* device, EvDevInputDeviceEventType type, input_event ev) {
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
                unix_check_ne(libevdev_uinput_write_event(mouse_out_uinput, ev.type, ev.code, ev.value));
            }
        });

        // Run event loop

        event_bus->run();

        return EXIT_SUCCESS;
    }
}

int main(int argc, char* argv[])
{
    return input::cmain(argc, argv);
}
