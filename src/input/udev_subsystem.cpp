#include "udev_subsystem.hpp"

#include <libudev.h>
#include <limits.h>
#include <sys/stat.h>

#include <unordered_set>

#define UDEV_TRACE_EVENTS 0
namespace input
{
    struct UDevSubsystem::Impl : UDevSubsystem
    {
        udev* ud = nullptr;
        udev_monitor* mon = nullptr;
        std::unordered_set<std::string> subsystems;
        std::vector<UDeviceCallbackFn> device_callbacks;

        std::unordered_map<std::string, UDevHidDevice> hid_devices;
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

    namespace {
        void hide_udev_node(udev_device* dev)
        {
            auto node = udev_device_get_devnode(dev);
            if (node) {
                log_debug("Hiding device [{}]", node);
                unix_check_n1(chmod(node, 0));
            }
        }
    }

    void UDevHidDevice::hide()
    {
        auto self = this;

        self->_hide = true;
        for (auto& nodes : self->nodes) {
            hide_udev_node(nodes.dev);
        }
    }

    namespace
    {
        void handle_device_added(UDevSubsystem::Impl* self, udev_device* dev)
        {
#if UDEV_TRACE_EVENTS
            if (udev_device_get_devnode(dev)) {
                log_trace("+ {}", udev_device_get_syspath(dev));
                log_trace(" --> {}", udev_device_get_devnode(dev));
                auto parent = dev;
                do {
                    log_trace("    {}:{}", udev_device_get_subsystem(parent) ?: "", udev_device_get_devtype(parent) ?: "");
                } while ((parent = udev_device_get_parent(parent)));
            }
#endif

            auto hid = udev_device_get_parent_with_subsystem_devtype(dev, "hid", nullptr);
            if (!hid) return;

            if (!udev_device_get_devnode(dev)) return;

            auto& device = self->hid_devices[udev_device_get_syspath(hid)];
            if (!device.hid) {
                device.hid = udev_device_ref(hid);

                device.usb_device = udev_device_get_parent_with_subsystem_devtype(hid, "usb", "usb_device");
                if (device.usb_device) {
                    device.usb_info = UDevHidDevice::UsbInfo {
                        .manufacturer = udev_device_get_sysattr_value(device.usb_device, "manufacturer") ?: "",
                        .product_str = udev_device_get_sysattr_value(device.usb_device, "product") ?: "",
                        .vendor_id = uint32_t(strtol(udev_device_get_sysattr_value(device.usb_device, "idVendor") ?: "", nullptr, 16)),
                        .product_id = uint32_t(strtol(udev_device_get_sysattr_value(device.usb_device, "idProduct") ?: "", nullptr, 16)),
                        .version = uint32_t(strtol(udev_device_get_sysattr_value(device.usb_device, "bcdDevice") ?: "", nullptr, 16)),
                    };

                    device.usb_interface = udev_device_get_parent_with_subsystem_devtype(hid, "usb", "usb_interface");
                    if (device.usb_interface) {
                        device.usb_info->interface_number = atoi(udev_device_get_sysattr_value(device.usb_interface, "bInterfaceNumber"));
                    }
                }

                for (auto& cb : self->device_callbacks) {
                    cb(UDeviceEvent {
                        .action = UDevAction::AddHid,
                        .device = &device,
                    });
                }
            }

            auto& interface = device.nodes.emplace_back(UDevHidNode {
                .parent = &device,
                .dev = udev_device_ref(dev),
            });

            if (device._hide) {
                hide_udev_node(dev);
            }

            for (auto& cb : self->device_callbacks) {
                cb(UDeviceEvent {
                    .action = UDevAction::AddNode,
                    .device = &device,
                    .node = &interface,
                });
            }
        }

        void handle_device_removed(UDevSubsystem::Impl* self, udev_device* dev)
        {
            for (auto&[syspath, device] : self->hid_devices) {
                auto hid = device.hid;

                auto i_interface = std::ranges::find_if(device.nodes, [&](const UDevHidNode& i) {
                    return 0 == strcmp(udev_device_get_syspath(i.dev), udev_device_get_syspath(dev));
                });
                if (i_interface != device.nodes.end()) {
                    for (auto& cb : self->device_callbacks) {
                        cb(UDeviceEvent {
                            .action = UDevAction::RemoveNode,
                            .device = &device,
                            .node = &*i_interface,
                        });
                    }
                    udev_device_unref(i_interface->dev);
                    device.nodes.erase(i_interface);
                    if (device.nodes.empty()) {
                        for (auto& cb : self->device_callbacks) {
                            cb(UDeviceEvent {
                                .action = UDevAction::RemoveHid,
                                .device = &device,
                            });
                        }
                        udev_device_unref(hid);
                        self->hid_devices.erase(std::string(syspath));
                    }

                    // We matched the interface, don't look through any more HID devices
                    return;
                }
            }
        }

        void handle_udev_events(UDevSubsystem::Impl* self)
        {
            for (;;) {
                auto dev = udev_monitor_receive_device(self->mon);
                if (!dev) break;
                defer { udev_device_unref(dev); };

                auto action = udev_device_get_action(dev);
                if      ("add"sv    == action) handle_device_added(self, dev);
                else if ("remove"sv == action) handle_device_removed(self, dev);
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

            handle_device_added(self, dev);
        }
    }
}
