#!/usr/bin/env python3

import sys
import time
import argparse
import os
import fcntl
import array
import time as systime
import yaml
from evdev import InputDevice, categorize, ecodes, UInput, list_devices, AbsInfo

os.chdir(sys.path[0])

# Argument parsing
parser = argparse.ArgumentParser(description="Virtual Joystick")
parser.add_argument('--vendor', type=lambda x: int(x, 16), default=0x04B4, help='Vendor ID in hex (e.g. 04B4)')
parser.add_argument('--product', type=lambda x: int(x, 16), default=0x0818, help='Product ID in hex (e.g. 0818)')
parser.add_argument('--debug', action='store_true', help='Show button press/release output for debugging')
args = parser.parse_args()

# Load YAML config
config_filename = f"{args.vendor:04X}{args.product:04X}.yml"
if not os.path.exists(config_filename):
    print(f"Configuration file {config_filename} not found.")
    exit(1)

with open(config_filename, 'r') as f:
    config = yaml.safe_load(f)

combo_map = {}
for entry in config.get('key_mappings', []):
    mod_combo = entry['combo'][0].split('|')
    key = entry['combo'][1]
    action = entry['action']
    combo_map[(frozenset(mod_combo), key)] = action

# Target HID device
TARGET_VENDOR = args.vendor
TARGET_PRODUCT = args.product

# Modifier keys
MOD_KEYS = {
    ecodes.KEY_LEFTCTRL: 'KEY_LEFTCTRL',
    ecodes.KEY_RIGHTCTRL: 'KEY_RIGHTCTRL',
    ecodes.KEY_LEFTALT: 'KEY_LEFTALT',
    ecodes.KEY_RIGHTALT: 'KEY_RIGHTALT',
    ecodes.KEY_LEFTSHIFT: 'KEY_LEFTSHIFT',
    ecodes.KEY_RIGHTSHIFT: 'KEY_RIGHTSHIFT',
    ecodes.KEY_LEFTMETA: 'KEY_LEFTMETA',
    ecodes.KEY_RIGHTMETA: 'KEY_RIGHTMETA',
}

mod_state = {k: False for k in MOD_KEYS.values()}

# Custom base event code for buttons
CUSTOM_BTN_BASE = 704

# Find the device
device_path = None
for path in list_devices():
    dev = InputDevice(path)
    if dev.info.vendor == TARGET_VENDOR and dev.info.product == TARGET_PRODUCT and ecodes.KEY_F13 in dev.capabilities().get(ecodes.EV_KEY, []):
        device_path = path
        break

if not device_path:
    print("Target device not found.")
    exit(1)

device = InputDevice(device_path)
device.grab()
print(f"Listening to: {device.name} ({device_path})")

# Define virtual joystick buttons starting at custom code 704 and add dummy axis
virt_buttons = [CUSTOM_BTN_BASE + i for i in range(48)]

capabilities = {
    ecodes.EV_KEY: virt_buttons,
    ecodes.EV_ABS: {
        ecodes.ABS_Z: AbsInfo(value=0, min=0, max=255, fuzz=0, flat=15, resolution=0)
    }
}

ui = UInput(events=capabilities, name=f"{dev.name} Virtual Joystick", version=dev.version, vendor=dev.info.vendor, product=dev.info.product)
print("Virtual joystick created.")
print(f"Virtual joystick vendor:product = {ui.device.info.vendor:04X}:{ui.device.info.product:04X}")

# Track state of each button
button_states = {}
for (_, _), action in combo_map.items():
    btn_num = int(action.replace('btn_', ''))
    button_states[btn_num] = 'released'

# Press tracking
mod_keys_down = set()
fkeys_down = set()

print("Press Ctrl+C to exit.")
try:
    for event in device.read_loop():
        if event.type != ecodes.EV_KEY:
            continue

        code = event.code
        value = event.value

        key_name = ecodes.KEY.get(code, str(code))

        if key_name in mod_state:
            mod_state[key_name] = bool(value)
            if value:
                mod_keys_down.add(key_name)
            else:
                mod_keys_down.discard(key_name)

            # Check all buttons in 'releasing' to see if we should finalize them
            for (mods, fkey), action in combo_map.items():
                btn_num = int(action.replace('btn_', ''))
                if button_states[btn_num] == 'releasing' and not (set(mods) & mod_keys_down):
                    button_states[btn_num] = 'released'
                    ui.write(ecodes.EV_KEY, CUSTOM_BTN_BASE + btn_num, 0)
                    ui.syn()
                    if args.debug:
                        print(f"{key_name} released, Button {btn_num} → RELEASED")
            continue

        if key_name.startswith("KEY_F"):
            if value:
                fkeys_down.add(key_name)
            else:
                fkeys_down.discard(key_name)

        active_mods = frozenset([k for k, v in mod_state.items() if v])

        if value == 1:  # key press
            # transition pressing
            for (mods, fkey), action in combo_map.items():
                btn_num = int(action.replace('btn_', ''))
                # Check if any modifier key in the combination is pressed
                if any(mod in active_mods for mod in mods) and button_states[btn_num] == 'released':
                    button_states[btn_num] = 'pressing'
                    if args.debug:
                        print(f"Key {key_name} → Button {btn_num} PRESSING")  # Show the key being pressed
            # transition to pressed
            for (mods, fkey), action in combo_map.items():
                if fkey == key_name:
                    btn_num = int(action.replace('btn_', ''))
                    if button_states[btn_num] == 'pressing':
                        button_states[btn_num] = 'pressed'
                        ui.write(ecodes.EV_KEY, CUSTOM_BTN_BASE + btn_num, 1)
                        ui.syn()
                        if args.debug:
                            print(f"Key {key_name} → Button {btn_num} PRESSED")  # Show the key being pressed
                else:
                    # cancel non-matching pressing
                    btn_num = int(combo_map[(mods, fkey)].replace('btn_', ''))
                    if button_states[btn_num] == 'pressing':
                        button_states[btn_num] = 'released'
                        if args.debug:
                            print(f"Key {key_name} → Button {btn_num} CANCELLED")  # Show the key being pressed
        elif value == 0:  # key release
            # F-key released triggers releasing state
            for (mods, fkey), action in combo_map.items():
                if fkey == key_name:
                    btn_num = int(action.replace('btn_', ''))
                    if button_states[btn_num] == 'pressed':
                        button_states[btn_num] = 'releasing'
                        if args.debug:
                            print(f"Key {key_name} → Button {btn_num} RELEASING")  # Show the key being pressed
            # Now finalize releasing state if no other key is pressed
            for btn_num, state in button_states.items():
                if state == 'releasing' and not mod_keys_down:
                    button_states[btn_num] = 'released'
                    ui.write(ecodes.EV_KEY, CUSTOM_BTN_BASE + btn_num, 0)
                    ui.syn()
                    if args.debug:
                        print(f"Key {key_name} → Button {btn_num} RELEASED")  # Show the key being released

except KeyboardInterrupt:
    print("\nExiting.")
finally:
    ui.close()
