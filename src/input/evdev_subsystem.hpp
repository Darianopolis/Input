#pragma once

#include "udev_subsystem.hpp"

#include <libevdev/libevdev.h>

namespace input
{
    struct EvInputDevice;

    enum class EvDevInputDeviceEventType
    {
        InputEvent,
        DeviceRemoved,
    };

    using EvDevDeviceFilter = std::function<bool(EvInputDevice*)>;
    using EvDevInputDeviceEventCallback = std::function<void(EvInputDevice*, EvDevInputDeviceEventType, input_event)>;

    struct EvInputDevice
    {
        struct Impl;

    public:
        void grab(bool force = false);
        void ungrab();

        UDevHidNode* get_udev_node();

        libevdev* get_device();
        const char* get_devnode();

        const char* get_name();
        int get_vid();
        int get_pid();

        bool has_gamepad();
        bool has_joystick();
        bool has_mouse();
        bool has_keyboard();
        bool has_cctrl();
    };

    struct EvDevSubsystem : RefCounted
    {
        struct Impl;

        static EvDevSubsystem* create(FdEventBus*, UDevSubsystem*);
        static void destroy(EvDevSubsystem*);

    public:
        void register_device_filter(EvDevDeviceFilter&&);
        void register_input_device_event_callback(EvInputDevice* device, EvDevInputDeviceEventCallback&&);
    };
}
