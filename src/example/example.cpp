#include "example.hpp"

namespace input::example
{
    FdEventBus* event_bus;
    UDevSubsystem* udev_subsystem;
    EvDevSubsystem* evdev_subsystem;

    static
    int cmain(int argc, char* argv[])
    {
        event_bus = FdEventBus::create();
        defer { unref(event_bus); };

        udev_subsystem = UDevSubsystem::create();
        defer { unref(udev_subsystem); };

        udev_subsystem->watch_subsystem("input");
        udev_subsystem->watch_subsystem("hidraw");

        init_udev_watch(argc, argv);

        evdev_subsystem = EvDevSubsystem::create(event_bus, udev_subsystem);
        defer { unref(evdev_subsystem); };

        init_joystick(argc, argv);
        init_mouse(argc, argv);

        udev_subsystem->start(event_bus);
        event_bus->run();

        return EXIT_SUCCESS;
    }
}

int main(int argc, char* argv[])
{
    return input::example::cmain(argc, argv);
}
