#pragma once

#include "fd_event_bus.hpp"

#include <libudev.h>

namespace input
{
    enum class UDevAction
    {
        Add,
        Remove
    };

    struct UDeviceEvent
    {
        udev_device* device;
        UDevAction action;
    };

    using UDeviceCallbackFn = std::function<void(UDeviceEvent)>;

    struct UDevSubsystem : RefCounted
    {
        struct Impl;

        static UDevSubsystem* create();
        static void destroy(UDevSubsystem*);

    public:
        void watch_subsystem(std::string_view subsystem);
        void register_device_listener(UDeviceCallbackFn&&);

        void start(FdEventBus* bus);
    };
}
