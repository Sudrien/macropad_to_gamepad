import time
from evdev import InputDevice, categorize, ecodes, UInput, list_devices, AbsInfo

# Target HID device
TARGET_VENDOR = 0x04B4
TARGET_PRODUCT = 0x0818

# Modifier keys
MOD_KEYS = {
    ecodes.KEY_LEFTCTRL: 'ctrl',
    ecodes.KEY_RIGHTCTRL: 'ctrl',
    ecodes.KEY_LEFTALT: 'alt',
    ecodes.KEY_RIGHTALT: 'alt',
    ecodes.KEY_LEFTSHIFT: 'shift',
    ecodes.KEY_RIGHTSHIFT: 'shift',
    ecodes.KEY_LEFTMETA: 'meta', # often labeled windows logo
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
    None: 0,         # No modifier → BTN 0–11
    'meta': 0,       # No modifier → BTN 0–11
    'ctrl': 12,      # BTN 12–23
    'alt': 24,       # BTN 24–35
    'shift': 36      # BTN 36–47
}

# Custom base event code for buttons, 'BTN_TRIGGER_HAPPY1'
CUSTOM_BTN_BASE = 704

# Resolve current modifier (only one at a time, priority: ctrl > alt > shift)
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

# Map F-key + modifier to virtual button
def get_virtual_button(fkey_code, modifier):
    base_index = fkey_code - ecodes.KEY_F13  # 0–11
    offset = modifier_offsets[modifier]
    return CUSTOM_BTN_BASE + base_index + offset

# Store modifier state at key press time
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
print(f"Listening to: {device.name} ({device_path})")

# Define virtual joystick buttons starting at custom code 704, 'BTN_TRIGGER_HAPPY1'
virt_buttons = [CUSTOM_BTN_BASE + i for i in range(48)]
capabilities = {
    ecodes.EV_KEY: virt_buttons
}

ui = UInput(events=capabilities, name="LingYao ShangHai Thumb Keyboard Gamepad", version=0x3, vendor=TARGET_VENDOR, product=TARGET_PRODUCT)
print("Virtual joystick created.")
print(f"Virtual joystick vendor:product = {ui.device.info.vendor:04X}:{ui.device.info.product:04X}\n")

# Main loop
try:
    for event in device.read_loop():
        if event.type != ecodes.EV_KEY:
            continue

        code = event.code
        value = event.value  # 1 = press, 0 = release

        # Modifier handling
        if code in MOD_KEYS:
            mod_state[MOD_KEYS[code]] = bool(value)
            continue

        # F-key handling
        if code in fkey_base:
            if value == 1:  # Key press
                modifier = current_modifier()
                fkey_pressed_mods[code] = modifier
            else:  # Key release
                modifier = fkey_pressed_mods.pop(code, None)

            virt_button = get_virtual_button(code, modifier)
            ui.write(ecodes.EV_KEY, virt_button, value)
            ui.syn()
            mod = modifier or "none"
            btn_num = virt_button - CUSTOM_BTN_BASE
            print(f"{mod.upper()} + {ecodes.KEY[code]} → Button {btn_num} {'DOWN' if value else 'UP'}")

except KeyboardInterrupt:
    print("\nExiting.")
finally:
    ui.close()
