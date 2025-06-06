# Devices

- Taranis: 0x0483 / 0x5710
- Stadia:  0x18d1 / 0x9400

Steam command line: `MANGOHUD_CONFIG=position=top-right SDL_GAMECONTROLLER_IGNORE_DEVICES=0x0483/0x5710,0x18d1/0x9400 %command%`

# Input

- evdev
  - Generally preferred source
- hidraw
  - For specialized devices and additional functionality
  - Needs to be disabled if not used, SDL3 bypasses evdev and pulls directly from hidraw for some devices
- joydev
  - Never used, but need to disable anyway so that older programs don't pick up masked devices via their joydev device

# Output

- evdev emulation via uinput
  - Preferred, simple and closest to high level layer
- hidraw emulation via uhid
  - Nichely useful for emulating very particular devices
- HID device emulation via USB Gadget system
  - Loopback via dummy_hcd for testing locally
  - Required for spoofing HID devices over HID
  - Basic driver configuration only allows spoofing a single (potentially composite) HID device with a very limited number of interfaces, which complicates configuration

# UDev

Tracking logs
```
# udevadm control --log-priority=debug
$ journalctl -f

# udevadm control --log-priority=info
```

Updating rules
```
# udevadm control --reload-rules
# udevadm trigger
```
