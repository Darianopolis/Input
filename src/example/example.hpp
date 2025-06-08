#pragma once

#include "input/fd_event_bus.hpp"
#include "input/udev_subsystem.hpp"
#include "input/evdev_subsystem.hpp"

namespace input::example
{
    extern FdEventBus* event_bus;
    extern UDevSubsystem* udev_subsystem;
    extern EvDevSubsystem* evdev_subsystem;

    void init_udev_watch(int argc, char* argv[]);
    void init_joystick(int argc, char* argv[]);
    void init_mouse(int argc, char* argv[]);
}
