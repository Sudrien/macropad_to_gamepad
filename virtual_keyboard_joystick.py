import argparse
import os
import fcntl
import array
from evdev import InputDevice, ecodes, UInput, list_devices, AbsInfo

parser = argparse.ArgumentParser(description="Map full keyboard to joystick buttons")
parser.add_argument('--vendor', type=lambda x: int(x, 16), default=0x04B4, help='Vendor ID in hex (e.g. 04B4)')
parser.add_argument('--product', type=lambda x: int(x, 16), default=0x0818, help='Product ID in hex (e.g. 0818)')
parser.add_argument('--debug', action='store_true', help='Print debug info on key events')
args = parser.parse_args()

TARGET_VENDOR = args.vendor
TARGET_PRODUCT = args.product
CUSTOM_BTN_BASE = 704

# List of all common keyboard keys (KEY_RESERVED to KEY_MICMUTE typically covers 105 keys)
ALL_KEYS = [code for code in range(ecodes.KEY_ESC, ecodes.KEY_MICMUTE + 1)]

# Locate the physical keyboard device
device_path = None
for path in list_devices():
    dev = InputDevice(path)
    if dev.info.vendor == TARGET_VENDOR and dev.info.product == TARGET_PRODUCT:
        if any(k in dev.capabilities().get(ecodes.EV_KEY, []) for k in ALL_KEYS):
            device_path = path
            break

if not device_path:
    print("Target device not found.")
    exit(1)

device = InputDevice(device_path)
device.grab()
print(f"Listening to: {device.name} ({device_path})")

# Map keyboard key codes to virtual button codes
key_to_button = {key: CUSTOM_BTN_BASE + i for i, key in enumerate(ALL_KEYS)}
virt_buttons = list(key_to_button.values())

# Virtual joystick device setup
capabilities = {
    ecodes.EV_KEY: virt_buttons,
    ecodes.EV_ABS: {
        ecodes.ABS_Z: AbsInfo(value=0, min=0, max=255, fuzz=0, flat=15, resolution=0)
    }
}
ui = UInput(events=capabilities, name=f"{dev.name} Virtual Joystick",
            version=dev.version, vendor=dev.info.vendor, product=dev.info.product)

print("Virtual joystick created.")
print(f"Virtual joystick vendor:product = {ui.device.info.vendor:04X}:{ui.device.info.product:04X}")

# Identify virtual joystick event and js node
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

for js in sorted(os.listdir('/dev/input')):
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

print("Press Ctrl+C to exit.")
try:
    for event in device.read_loop():
        if event.type == ecodes.EV_KEY and event.code in key_to_button:
            virt_button = key_to_button[event.code]
            ui.write(ecodes.EV_KEY, virt_button, event.value)
            ui.syn()
            if args.debug:
                state = "DOWN" if event.value else "UP"
                keyname = ecodes.KEY[event.code]
                btn_num = virt_button - CUSTOM_BTN_BASE
                print(f"{keyname} → Button {btn_num} {state}")
except KeyboardInterrupt:
    print("\nExiting.")
finally:
    ui.close()
