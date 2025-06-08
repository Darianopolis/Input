#pragma once

#include "fd_event_bus.hpp"

#include <libudev.h>

#include <list>

namespace input
{
    enum class UDevAction
    {
        AddHid,
        AddNode,
        RemoveNode,
        RemoveHid,
    };

    enum class UDevHidInterfaceType
    {
        EvDev,
        Hidraw,
        Joydev,
    };

    struct UDevHidDevice;

    struct UDevHidNode
    {
        UDevHidDevice* parent;

        udev_device* dev;
    };

    struct UDevHidDevice
    {
        udev_device* hid;
        udev_device* usb_interface;
        udev_device* usb_device;

        struct UsbInfo {
            std::string manufacturer;
            std::string product_str;
            uint32_t vendor_id;
            uint32_t product_id;
            uint32_t version;
            uint32_t interface_number;
        };
        std::optional<UsbInfo> usb_info;

        std::list<UDevHidNode> nodes;

        bool _hide = false;

        void hide();
    };

    struct UDeviceEvent
    {
        UDevAction action;
        UDevHidDevice* device;
        UDevHidNode* node;
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
