#include "example.hpp"

#include <sstream>
#include <regex>

namespace input::example
{
    static
    auto report_sysattrs(const char* name, udev_device* dev, const char* prefix = "  ")
    {
        if (name)
            log_info("{}{}:", prefix, name);
        udev_list_entry* entry;
        udev_list_entry_foreach(entry, udev_device_get_sysattr_list_entry(dev)) {
            auto entry_name = udev_list_entry_get_name(entry);
            if (!entry_name) continue;
            log_info("{}  {} = {}", prefix, entry_name, udev_device_get_sysattr_value(dev, entry_name) ?: "");
        }
    };

    struct HidDetails
    {
        std::string name;
        uint32_t bus_type;
        uint32_t vendor_id;
        uint32_t product_id;
    };

    HidDetails parse_uevent(const char* uevent)
    {
        std::stringstream ss{ uevent };
        std::string line;
        uint32_t line_idx = 1;

        HidDetails details;

        constexpr auto HID_NAME = "HID_NAME="sv;
        constexpr auto HID_ID = "HID_ID="sv;

        while (std::getline(ss, line)) {
            log_info("uevent[{}] {}", line_idx++, line);
            if (line.starts_with(HID_NAME)) {
                details.name = line.substr(HID_NAME.size());
            } else if (line.starts_with(HID_ID)) {
                auto sub = line.substr(HID_ID.size());
                std::regex r{"([0-9a-fA-F]+):([0-9a-fA-F]+):([0-9a-fA-F]+)"};
                auto im = std::sregex_iterator(sub.begin(), sub.end(), r);
                if (im == std::sregex_iterator{}) break;

                auto& m = *im;
                log_info("bus_type = {}", m[1].str());
                log_info("vendor_id = {}", m[2].str());
                log_info("product_id = {}", m[3].str());
            }
        }

        return details;
    }

    void init_udev_watch(int argc, char* argv[])
    {
        udev_subsystem->register_device_listener([](UDeviceEvent event) {
            if (event.action == UDevAction::AddHid) {
                log_info("+HID");
                log_info("  {}", udev_device_get_syspath(event.device->hid));
                // if (event.device->usb_info) {
                //     log_info("  manufacturer = {}", event.device->usb_info->manufacturer);
                //     log_info("  product      = {}", event.device->usb_info->product_str);
                //     log_info("  vid/pid/ver  = {:04x}/{:04x}/{:04x}",
                //         event.device->usb_info->vendor_id,
                //         event.device->usb_info->product_id,
                //         event.device->usb_info->version);
                //     log_info("  interface    = {} ({})", event.device->usb_info->interface_number,
                //         strtol(udev_device_get_sysattr_value(event.device->usb_device, "bNumInterfaces") ?: "", nullptr, 10));
                // }

                // report_sysattrs("hid", event.device->hid);
                // if (event.device->usb_device) {
                //     report_sysattrs("usb_device", event.device->usb_device);
                // }
                // if (event.device->usb_interface) {
                //     report_sysattrs("usb_interface", event.device->usb_interface);
                // }

                auto uevent = udev_device_get_sysattr_value(event.device->hid, "uevent");
                parse_uevent(uevent);

            } else if (event.action == UDevAction::AddNode) {
                log_info("+NODE");
                log_info("  {}", udev_device_get_devnode(event.node->dev));

                // report_sysattrs(nullptr, event.node->dev);
            } else if (event.action == UDevAction::RemoveNode) {
                log_info("-NODE");
                log_info("  {}", udev_device_get_devnode(event.node->dev));
            } else if (event.action == UDevAction::RemoveHid) {
                log_info("-HID");
                log_info("  {}", udev_device_get_syspath(event.device->hid));
            }
        });
    }
}
