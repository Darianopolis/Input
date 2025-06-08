#include "evdev_subsystem.hpp"

#include <memory>
#include <thread>

#include <libevdev/libevdev.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace input
{
    struct EvDevSubsystem::Impl : EvDevSubsystem
    {
        FdEventBus* event_bus;
        std::vector<std::unique_ptr<EvInputDevice::Impl>> devices;
        std::vector<EvDevDeviceFilter> device_filters;
    };

    struct EvInputDevice::Impl : EvInputDevice
    {
        std::string devnode;
        libevdev* device = nullptr;
        UDevHidNode* node = nullptr;
        int fd = -1;

        bool needs_sync = false;

        std::vector<EvDevInputDeviceEventCallback> event_callbacks;

        ~Impl()
        {
            log_trace("Freeing libevdev device (device = {}, fd = {})", (void*)device, fd);
            if (device) libevdev_free(device);
            close(fd);
        }
    };

    void EvInputDevice::grab(bool state)
    {
        decl_self(this);

        unix_check_ne(libevdev_grab(self->device, state ? LIBEVDEV_GRAB : LIBEVDEV_UNGRAB));
    }

    UDevHidNode* EvInputDevice::get_udev_node() { return get_impl(this)->node; }

    libevdev*   EvInputDevice::get_device()   { return get_impl(this)->device;          }
    const char* EvInputDevice::get_devnode()  { return get_impl(this)->devnode.c_str(); }

    const char* EvInputDevice::get_name()     { return libevdev_get_name(      get_impl(this)->device); }
    int         EvInputDevice::get_vid()      { return libevdev_get_id_vendor( get_impl(this)->device); }
    int         EvInputDevice::get_pid()      { return libevdev_get_id_product(get_impl(this)->device); }

    bool        EvInputDevice::has_gamepad()  { return libevdev_has_event_code(get_impl(this)->device, EV_KEY, BTN_GAMEPAD);  }
    bool        EvInputDevice::has_joystick() { return libevdev_has_event_code(get_impl(this)->device, EV_KEY, BTN_JOYSTICK); }
    bool        EvInputDevice::has_mouse()    { return libevdev_has_event_code(get_impl(this)->device, EV_KEY, BTN_MOUSE);    }
    bool        EvInputDevice::has_keyboard() { return libevdev_has_event_code(get_impl(this)->device, EV_KEY, KEY_ENTER);    }
    bool        EvInputDevice::has_cctrl()
    {
        decl_self(this);

        static constexpr int ConsumerControlKeys[] {
            KEY_MUTE,
            KEY_VOLUMEDOWN,
            KEY_VOLUMEUP,
            KEY_STOP,
            KEY_CALC,
            KEY_FILE,
            KEY_MAIL,
            KEY_BOOKMARKS,
            KEY_BACK,
            KEY_FORWARD,
            KEY_EJECTCD,
            KEY_NEXTSONG,
            KEY_PLAYPAUSE,
            KEY_PREVIOUSSONG,
            KEY_STOPCD,
            KEY_REWIND,
            KEY_CONFIG,
            KEY_HOMEPAGE,
            KEY_REFRESH,
            KEY_FASTFORWARD,
            KEY_SEARCH,
        };

        static constexpr int ConsumerControlAbs[] {
            ABS_VOLUME,
        };

        for (auto key : ConsumerControlKeys) if (libevdev_has_event_code(self->device, EV_KEY, key)) return true;
        for (auto abs : ConsumerControlAbs)  if (libevdev_has_event_code(self->device, EV_ABS, abs)) return true;
        return false;
    }

    namespace
    {
        void handle_evdev_input_event(EvDevSubsystem::Impl* self, EvInputDevice::Impl* device)
        {
            input_event ev = {};

            for (;;) {
                auto res = unix_check_ne(libevdev_next_event(device->device,
                    device->needs_sync ? LIBEVDEV_READ_FLAG_SYNC : LIBEVDEV_READ_FLAG_NORMAL, &ev), EAGAIN, ENODEV);

                if (res == LIBEVDEV_READ_STATUS_SYNC) {
                    device->needs_sync = true;
                    if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
                        log_debug("Sync required");
                        continue;
                    } else {
                        log_debug("Sync ({}) = {}", libevdev_event_code_get_name(ev.type, ev.code), ev.value);
                    }
                }
                else if (res == -EAGAIN) {
                    if (device->needs_sync) {
                        log_debug("Sync completed!");
                        device->needs_sync = false;
                    }
                    return;
                }
                else if (res == -ENODEV) {
                    log_debug("Device [{}] disconnected", device->get_name());
                    for (auto& cb : device->event_callbacks) {
                        cb(device, EvDevInputDeviceEventType::DeviceRemoved, {});
                    }
                    self->event_bus->unregister_fd_listener(device->fd);
                    self->devices.erase(std::ranges::find(self->devices, device, [](auto& ptr) { return ptr.get(); }));
                    log_debug("Erased device");
                    return;
                }
#define NOISY_EVDEV_EVENTS 0
#if     NOISY_EVDEV_EVENTS
                else {
                    if (ev.type != EV_REL && ev.type != EV_SYN) {
                        log_trace("Event ({}) = {}", libevdev_event_code_get_name(ev.type, ev.code), ev.value);
                    }
                }
#endif

                for (auto& cb : device->event_callbacks) {
                    cb(device, EvDevInputDeviceEventType::InputEvent, ev);
                }
            }
        }

        void handle_udev_event(EvDevSubsystem::Impl* self, const UDeviceEvent& event)
        {
            if (!event.node) return;
            if (event.action == UDevAction::RemoveNode) {
                for (auto iter = self->devices.begin(); iter != self->devices.end(); ++iter) {
                    auto& evdev = *iter;
                    if (evdev->node == event.node) {
                        log_warn("EVDEV DEVICE FORCEFULLY REMOVED VIA UDEV EVENT");
                        for (auto& cb : evdev->event_callbacks) {
                            cb(evdev.get(), EvDevInputDeviceEventType::DeviceRemoved, {});
                        }
                        evdev.reset();
                        self->devices.erase(iter);
                        break;
                    }
                }
                return;
            }
            if ("input"sv != udev_device_get_subsystem(event.node->dev)) return;

            auto devnode = udev_device_get_devnode(event.node->dev);
            if (!devnode) return;

            auto evdev = std::make_unique<EvInputDevice::Impl>();
            evdev->devnode = devnode;
            evdev->node = event.node;

            evdev->fd = open(devnode, O_RDONLY | O_NONBLOCK);
            if (evdev->fd == -1) return;

            if (unix_check_ne(libevdev_new_from_fd(evdev->fd, &evdev->device), ENOTTY, EINVAL) < 0)
                return;

            // Detect device type

            auto has_gamepad  = evdev->has_gamepad();
            auto has_joystick = evdev->has_joystick();
            auto has_mouse    = evdev->has_mouse();
            auto has_keyboard = evdev->has_keyboard();
            auto has_cctrl = evdev->has_cctrl();

            if (!(has_mouse || has_keyboard || has_gamepad || has_joystick || has_cctrl)) return;

#define DUMP_EVDEV_INFO 1
#define DUMP_EVDEV_CODES 0

#if  DUMP_EVDEV_INFO

            log_debug("evdev = {}", libevdev_get_name(evdev->device));
            log_debug("  vid = {:#06x}", libevdev_get_id_vendor(evdev->device));
            log_debug("  pid = {:#06x}", libevdev_get_id_product(evdev->device));

            if (has_gamepad)  log_debug("  evdev.gamepad = true");
            if (has_joystick) log_debug("  evdev.joystick = true");
            if (has_mouse)    log_debug("  evdev.mouse = true");
            if (has_keyboard) log_debug("  evdev.keyboard = true");
            if (has_cctrl)    log_debug("  evdev.consumer_control = true");

            // List device codes

            log_debug("  codes");

            auto count_codes = [&](int type, int code_min, int code_max) {
                uint32_t count = 0;
#if DUMP_EVDEV_CODES
                for (int code = code_min; code <= code_max; ++code) {
                    if (libevdev_has_event_code(evdev->device, type, code)) {
                        if (!count++) log_trace("    BEGIN {}", libevdev_event_type_get_name(type));
                        log_trace("      {}", libevdev_event_code_get_name(type, code));
                    }
                }
                if (count) log_trace("    END {} (count = {})", libevdev_event_type_get_name(type), count);
#else
            for (int code = code_min; code <= code_max; ++code) {
                if (libevdev_has_event_code(evdev->device, type, code)) count++;
            }
            if (count) log_debug("    {} - {}", libevdev_event_type_get_name(type), count);
#endif
            };

            count_codes(EV_ABS, ABS_X, ABS_MAX);
            count_codes(EV_REL, REL_X, REL_MAX);
            count_codes(EV_KEY, KEY_RESERVED, KEY_MAX);
#endif

            bool add_device = false;
            for (auto& filter : self->device_filters) {
                add_device |= filter(evdev.get());
            }

            if (add_device) {
                log_debug("Listening to device [{}] (fd = {})", evdev->get_name(), evdev->fd);
                self->event_bus->register_fd_listener(evdev->fd, EPOLLIN, [self, evdev = evdev.get()](FdEventData data) {
                    handle_evdev_input_event(self, evdev);
                });
                self->devices.emplace_back(std::move(evdev));
            }
        }
    }

    EvDevSubsystem* EvDevSubsystem::create(FdEventBus* bus, UDevSubsystem* udev)
    {
        auto self = new EvDevSubsystem::Impl;
        defer { unref(self); };

        self->event_bus = bus;

        udev->watch_subsystem("input");
        udev->register_device_listener([self](UDeviceEvent event) {
            handle_udev_event(self, event);
        });

        return take(self);
    }

    void EvDevSubsystem::destroy(EvDevSubsystem* _self)
    {
        decl_self(_self);

        delete self;
    }

    void EvDevSubsystem::register_device_filter(EvDevDeviceFilter&& callback)
    {
        decl_self(this);

        self->device_filters.emplace_back(std::move(callback));
    }

    void EvDevSubsystem::register_input_device_event_callback(EvInputDevice* device, EvDevInputDeviceEventCallback&& callback)
    {
        decl_self(this);

        get_impl(device)->event_callbacks.emplace_back(std::move(callback));
    }
}
