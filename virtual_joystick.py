import time
import argparse
import os
import fcntl
import array
import time as systime
from evdev import InputDevice, categorize, ecodes, UInput, list_devices, AbsInfo

# Argument parsing
parser = argparse.ArgumentParser(description="Virtual Joystick")
parser.add_argument('--vendor', type=lambda x: int(x, 16), default=0x04B4, help='Vendor ID in hex (e.g. 04B4)')
parser.add_argument('--product', type=lambda x: int(x, 16), default=0x0818, help='Product ID in hex (e.g. 0818)')
parser.add_argument('--debug', action='store_true', help='Suppress button press/release output for debugging')
args = parser.parse_args()

# Target HID device
TARGET_VENDOR = args.vendor
TARGET_PRODUCT = args.product

# Modifier keys
MOD_KEYS = {
    ecodes.KEY_LEFTCTRL: 'ctrl',
    ecodes.KEY_RIGHTCTRL: 'ctrl',
    ecodes.KEY_LEFTALT: 'alt',
    ecodes.KEY_RIGHTALT: 'alt',
    ecodes.KEY_LEFTSHIFT: 'shift',
    ecodes.KEY_RIGHTSHIFT: 'shift',
    ecodes.KEY_LEFTMETA: 'meta',
    ecodes.KEY_RIGHTMETA: 'meta',
}

mod_state = {'ctrl': False, 'alt': False, 'shift': False, 'meta': False}

# Base F-keys
fkey_base = [
    ecodes.KEY_F13, ecodes.KEY_F14, ecodes.KEY_F15, ecodes.KEY_F16,
    ecodes.KEY_F17, ecodes.KEY_F18, ecodes.KEY_F19, ecodes.KEY_F20,
    ecodes.KEY_F21, ecodes.KEY_F22, ecodes.KEY_F23, ecodes.KEY_F24
]

# Button offsets per modifier
modifier_offsets = {
    'meta': 0,
    'ctrl': 12,
    'alt': 24,
    'shift': 36
}

# Custom base event code for buttons
CUSTOM_BTN_BASE = 704

def current_modifier():
    if mod_state['meta']:
        return 'meta'
    if mod_state['ctrl']:
        return 'ctrl'
    if mod_state['alt']:
        return 'alt'
    if mod_state['shift']:
        return 'shift'
    return None

def get_virtual_button(fkey_code, modifier):
    base_index = fkey_code - ecodes.KEY_F13
    offset = modifier_offsets[modifier]
    return CUSTOM_BTN_BASE + base_index + offset

fkey_pressed_mods = {}

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

for path in list_devices():
    try:
        test_dev = InputDevice(path)
        if test_dev.name == f"{dev.name} Virtual Joystick" and \
           test_dev.info.vendor == dev.info.vendor and \
           test_dev.info.product == dev.info.product:
            print(f"Virtual device event node: {path}")
            break
    except Exception:
        continue

def get_js_device_name(fd):
    buf = array.array('B', [0] * 128)
    fcntl.ioctl(fd, 0x80006a13 + (128 << 16), buf)
    return buf.tobytes().rstrip(b'\x00').decode('utf-8')

js_paths = sorted(os.listdir('/dev/input'))
for js in js_paths:
    if js.startswith("js"):
        full_path = f"/dev/input/{js}"
        try:
            with open(full_path, 'rb') as fd:
                name = get_js_device_name(fd.fileno())
                if name == f"{dev.name} Virtual Joystick":
                    print(f"Virtual device joystick node: {full_path}\n")
                    break
        except Exception:
            continue

# Main loop
print("Press Ctrl+C to exit.")
try:
    for event in device.read_loop():
        if event.type != ecodes.EV_KEY:
            continue

        code = event.code
        value = event.value

        if code in MOD_KEYS:
            mod_state[MOD_KEYS[code]] = bool(value)
            continue

        if code in fkey_base:
            if value == 1:
                modifier = current_modifier()
                fkey_pressed_mods[code] = modifier
            else:
                modifier = fkey_pressed_mods.pop(code, None)

            virt_button = get_virtual_button(code, modifier)
            ui.write(ecodes.EV_KEY, virt_button, value)
            ui.syn()
            if args.debug:
                mod = modifier or "none"
                btn_num = virt_button - CUSTOM_BTN_BASE
                print(f"{mod.upper()} + {ecodes.KEY[code]} → Button {btn_num} {'DOWN' if value else 'UP'}")

except KeyboardInterrupt:
    print("\nExiting.")
finally:
    ui.close()
