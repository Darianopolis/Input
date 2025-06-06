#include "udev_subsystem.hpp"

#include <libudev.h>
#include <limits.h>
#include <sys/stat.h>
// #include <fcntl.h>
// #include <unistd.h>

#include <unordered_set>

namespace input
{
    struct UDevSubsystem::Impl : UDevSubsystem
    {
        udev* ud = nullptr;
        udev_monitor* mon = nullptr;
        std::unordered_set<std::string> subsystems;
        std::vector<UDeviceCallbackFn> device_callbacks;
    };

    UDevSubsystem* UDevSubsystem::create()
    {
        auto self = new UDevSubsystem::Impl;
        defer { unref(self); };

        self->ud = udev_new();
        if (!self->ud) raise_unix_error("udev_new");

        self->mon = udev_monitor_new_from_netlink(self->ud, "udev");

        return take(self);
    }

    void UDevSubsystem::destroy(UDevSubsystem* _self)
    {
        decl_self(_self);

        udev_monitor_unref(self->mon);
        udev_unref(self->ud);

        delete self;
    }

    namespace
    {
        void handle_device_event(UDevSubsystem::Impl* self, udev_device* dev, UDevAction action)
        {
#define UDEV_DUMP_EVENTS 1
#if     UDEV_DUMP_EVENTS
            if (auto devnode = udev_device_get_devnode(dev)) {
                log_trace("{} {} - {}", action == UDevAction::Add ? "+" : "-", devnode, (void*)dev);

                auto parent = udev_device_get_parent(dev);
                if (parent) {
                    auto parent_path = udev_device_get_syspath(parent);
                    if (parent_path) {
                        // char real[PATH_MAX] = {};
                        // realpath(devnode, real);
                        log_trace("  --> {}", parent_path);
                    } else {
                        log_trace("  -- has parent, but no devpath");
                    }
                }

                {
                    auto hid_parent = udev_device_get_parent_with_subsystem_devtype(dev, "hidraw", nullptr);
                    if (hid_parent) {
                        auto hid_devnode = udev_device_get_devnode(hid_parent);
                        log_warn("  hid parent = {}", hid_devnode ?: "N/A");
                    }
                }
            }
#endif

            for (auto& cb : self->device_callbacks) {
                cb(UDeviceEvent {
                    .device = dev,
                    .action = action,
                });
            }
        }

        void handle_udev_events(UDevSubsystem::Impl* self)
        {
            for (;;) {
                auto dev = udev_monitor_receive_device(self->mon);
                if (!dev) break;
                defer { udev_device_unref(dev); };

                auto action = udev_device_get_action(dev);
                if      ("add"sv    == action) handle_device_event(self, dev, UDevAction::Add);
                else if ("remove"sv == action) handle_device_event(self, dev, UDevAction::Remove);
                else log_warn("Unknown udev action [{}]", action);
            }
        }
    }

    void UDevSubsystem::watch_subsystem(std::string_view subsystem)
    {
        get_impl(this)->subsystems.emplace(subsystem);
    }

    void UDevSubsystem::register_device_listener(UDeviceCallbackFn&& fn)
    {
        get_impl(this)->device_callbacks.emplace_back(std::move(fn));
    }

    void UDevSubsystem::start(FdEventBus* bus)
    {
        decl_self(this);

        if (self->subsystems.empty()) {
            raise_error("No subsystems selected!");
        }

        // Register event watcher

        for (auto& subsystem : self->subsystems) {
            udev_monitor_filter_add_match_subsystem_devtype(self->mon, subsystem.c_str(), nullptr);
        }
        udev_monitor_enable_receiving(self->mon);
        auto fd = udev_monitor_get_fd(self->mon);

        bus->register_fd_listener(fd, EPOLLIN, [self](FdEventData) {
            handle_udev_events(self);
        });

        // Perform initial scan

        auto enumerate = udev_enumerate_new(self->ud);
        defer { udev_enumerate_unref(enumerate); };
        for (auto& subsystem : self->subsystems) {
            udev_enumerate_add_match_subsystem(enumerate, subsystem.c_str());
        }
        udev_enumerate_scan_devices(enumerate);

        auto devices = udev_enumerate_get_list_entry(enumerate);
        udev_list_entry* entry;
        udev_list_entry_foreach(entry, devices) {
            auto path = udev_list_entry_get_name(entry);
            auto dev = udev_device_new_from_syspath(self->ud, path);
            defer { udev_device_unref(dev); };

            handle_device_event(self, dev, UDevAction::Add);
        }
    }
}
