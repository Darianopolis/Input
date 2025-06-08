#include "example.hpp"

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

    void init_udev_watch(int argc, char* argv[])
    {

        udev_subsystem->register_device_listener([](UDeviceEvent event) {
            if (event.action == UDevAction::AddHid) {
                log_info("+HID");
                log_info("  {}", udev_device_get_syspath(event.device->hid));
                if (event.device->usb_info) {
                    log_info("  manufacturer = {}", event.device->usb_info->manufacturer);
                    log_info("  product      = {}", event.device->usb_info->product_str);
                    log_info("  vid/pid/ver  = {:04x}/{:04x}/{:04x}",
                        event.device->usb_info->vendor_id,
                        event.device->usb_info->product_id,
                        event.device->usb_info->version);
                    log_info("  interface    = {} ({})", event.device->usb_info->interface_number,
                        strtol(udev_device_get_sysattr_value(event.device->usb_device, "bNumInterfaces") ?: "", nullptr, 10));
                }

                // report_sysattrs("hid", event.device->hid);

                // if (event.device->usb_device) {
                //     report_sysattrs("usb_device", event.device->usb_device);
                // }
                // if (event.device->usb_interface) {
                //     report_sysattrs("usb_interface", event.device->usb_interface);
                // }
            } else if (event.action == UDevAction::AddNode) {
                log_info("+NODE");
                log_info("  {}", udev_device_get_devnode(event.node->dev));

                // report_sysattrs(nullptr, event.interface->dev);
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
