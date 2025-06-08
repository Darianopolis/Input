#include "example.hpp"

#include "input/math.hpp"

#include <libevdev/libevdev-uinput.h>

#include <cmath>

namespace input::example
{
    static
    libevdev_uinput* joy_uinput = nullptr;

    static
    void create_virtual_joystick()
    {
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

        unix_check_ne(libevdev_uinput_create_from_device(virt_joystick, LIBEVDEV_UINPUT_OPEN_MANAGED, &joy_uinput));
    }

    static
    auto joy_report(double wheel, double throttle, double brake, double handbrake, bool accept, bool save, bool other)
    {
        static constexpr auto rescale = [](double value) -> int
        {
            return std::clamp(value, -1.0, 1.0) * 32767;
        };

        unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_ABS, ABS_X, rescale(wheel)));
        unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_ABS, ABS_Y,   throttle <= 0 ? -1 : rescale(throttle)));
        unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_ABS, ABS_Z,      brake <= 0 ? -1 : rescale(brake)));
        unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_ABS, ABS_RX, handbrake <= 0 ? -1 : rescale(handbrake)));
        unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_KEY, BTN_TRIGGER, accept));
        unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_KEY, BTN_THUMB, save));
        unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_KEY, BTN_THUMB2, other));
        unix_check_ne(libevdev_uinput_write_event(joy_uinput, EV_SYN, SYN_REPORT, 0));
    };

    static
    auto maprange(double v, double in_low, double in_high, double out_low, double out_high, bool clamp = false) -> double
    {
        if (clamp) {
            if (v < in_low) return out_low;
            if (v > in_high) return out_high;
        }
        auto p = (v - in_low) / (in_high - in_low);
        return p * (out_high - out_low) + out_low;
    };

    static
    auto deadzone(double v, double inner, double outer) -> double
    {
        if (std::abs(v) < inner) return 0;
        return std::copysign(std::min((std::abs(v) - inner) / (1.0 - inner - outer), 1.0), v);
    };

    static
    auto deadzone_radial(vec2 pos, double inner, double outer) -> vec2
    {
        auto r = mag(pos);
        if (r < inner) return vec2(0.0);

        auto d = deadzone(r, inner, outer);

        // Bump deadzone output, otherwise value flickers between 1.0 and 0.9999...
        //   due to floating point precision limitations, which leads to rounding issues
        d = std::nextafter(d, std::numeric_limits<double>::infinity());

        return pos * (d / r);
    };

    static
    auto gamma(double v, double g) -> double
    {
        return std::copysign(std::pow(std::abs(v), g), v);
    };

    static
    auto radial_to_wheel(vec2 pos, double q_max, double r_gamma, double q_gamma) -> double
    {
        auto r = gamma(mag(pos), r_gamma);
        auto q = std::atan2(pos.x, pos.y) / q_max;
        q = std::clamp(q, -1.0, 1.0);
        q = gamma(q, q_gamma);
        return std::clamp(std::min(r, 1.0) * q, -1.0, 1.0);
    };

    static
    auto radial_to_throttle_brake(vec2 pos, double* throttle, double* brake, double* handbrake)
    {
        auto r = mag(pos);
        *throttle  = (pos.x > 0 && pos.y > 0) ? r : 0.0;
        *brake     =  pos.x < 0               ? r : 0.0;
        *handbrake = (pos.x > 0 && pos.y < 0) ? r : 0.0;
    };

    void init_joystick(int argc, char* argv[])
    {
        create_virtual_joystick();

#define INPUT_NOISY_JOYSTICKS 0

        // Google Stadia Controller

        evdev_subsystem->register_device_filter([](EvInputDevice* device) -> bool {
            if (!device->has_gamepad() && !device->has_joystick()) return false;
            if (device->get_vid() != 0x18d1 || device->get_pid() != 0x9400) return false;
            log_info("Found joystick: {}", device->get_name());

            device->get_udev_node()->parent->hide();
            device->grab();

            evdev_subsystem->register_input_device_event_callback(device, [](EvInputDevice* device, EvDevInputDeviceEventType type, input_event ev) {
                if (type == EvDevInputDeviceEventType::DeviceRemoved) {
                    log_debug("Joystick [{}] removed", device->get_name());
                    return;
                }

                if (ev.type != EV_SYN || ev.code != SYN_REPORT) return;

                std::array<double, 8> values{};
                std::array<int, 8> abs_names { ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_GAS, ABS_BRAKE, ABS_HAT0X, ABS_HAT0Y };
                for (int i = 0; i < abs_names.size(); ++i) {
                    auto info = libevdev_get_abs_info(device->get_device(), abs_names[i]);

                    auto range = double(info->maximum - info->minimum);
                    values[i] = (info->value - info->minimum) / range;
                    values[i] = (values[i] * 2) - 1;
                }

                std::array<bool, 18> key{};
                std::array<int, 18> key_names { KEY_VOLUMEDOWN, KEY_VOLUMEUP, KEY_PLAYPAUSE, BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR,
                    BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR, BTN_TRIGGER_HAPPY1, BTN_TRIGGER_HAPPY2, BTN_TRIGGER_HAPPY3, BTN_TRIGGER_HAPPY4 };
                for (int i = 0; i < key_names.size(); ++i) {
                    key[i] = libevdev_get_event_value(device->get_device(), EV_KEY, key_names[i]);
                }

#if INPUT_NOISY_JOYSTICKS
                std::string output;
                for (auto& v : values) output += std::format(" {:5.2f}", v);
                output += " --";
                for (int i = 0; i < key_names.size(); ++i) {
                    if (key[i]) output += std::format(" {}", i);
                }
                log_info("Stadia: {}", output);
#endif

                auto wheel = radial_to_wheel(deadzone_radial(vec2(values[2], -values[3]), 0.13, 0), 2.5, 2.25, 1.3);
                double throttle, brake, handbrake;
                radial_to_throttle_brake(deadzone_radial(vec2(values[0], -values[1]), 0.13, 0), &throttle, &brake, &handbrake);
                if (key[7]) handbrake = 1.0;

                auto a = key[3];
                auto y = key[6];
                auto right_shoulder = key[8];

                joy_report(wheel, throttle, brake, handbrake, a, right_shoulder, y);
            });

            return true;
        });

        // Taranis X9D

        evdev_subsystem->register_device_filter([&](EvInputDevice* device) -> bool {
            if (!device->has_gamepad() && !device->has_joystick()) return false;
            if (device->get_vid() != 0x0483 || device->get_pid() != 0x5710) return false;
            log_info("Found joystick: {}", device->get_name());

            device->get_udev_node()->parent->hide();
            device->grab();

            evdev_subsystem->register_input_device_event_callback(device, [](EvInputDevice* device, EvDevInputDeviceEventType type, input_event ev) {
                if (type == EvDevInputDeviceEventType::DeviceRemoved) {
                    log_debug("Joystick [{}] removed", device->get_name());
                    return;
                }

                if (ev.type != EV_SYN || ev.code != SYN_REPORT) return;

                std::array<double, 7> values{};
                std::array<int, 7> abs_names { ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_THROTTLE };
                for (int i = 0; i < abs_names.size(); ++i) {
                    auto info = libevdev_get_abs_info(device->get_device(), abs_names[i]);

                    auto range = double(info->maximum - info->minimum);
                    values[i] = (info->value - info->minimum) / range;
                    values[i] = (values[i] * 2) - 1;
                }

#if INPUT_NOISY_JOYSTICKS
                log_info("Abs: {:5.2f} {:5.2f} {:5.2f} {:5.2f} {:5.2f} {:5.2f} {:5.2f}",
                    values[0], values[1], values[2], values[3], values[4], values[5], values[6]);
#endif

                auto throttle = maprange(values[0], -1, 1, -0.1, 1);
                auto wheel = gamma(values[1], 2.0);
                auto brake_handbrake = deadzone(values[3], 0.05, 0.120);
                auto brake = std::max(-brake_handbrake, 0.0);
                auto handbrake = std::max(brake_handbrake, 0.0) * 2;

                auto rs_forward = values[2] > 0.25;
                auto rs_back = values[2] < -0.25;
                auto right_shoulder = values[4] > 0;

                joy_report(wheel, throttle, brake, handbrake, rs_forward, rs_back, right_shoulder);
            });

            return true;
        });
    }
}
