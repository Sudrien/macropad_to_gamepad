import xml.etree.ElementTree as ET
from evdev import InputDevice, list_devices
import re

# Load and parse the binds file
tree = ET.parse('~/.local/share/Steam/steamapps/compatdata/359320/pfx/drive_c/users/steamuser/AppData/Local/Frontier Developments/Elite Dangerous/Options/Bindings/Custom.4.2.binds')
root = tree.getroot()

# Collect all unique device IDs from <Binding Device="...">
device_ids = set()
for binding in root.findall('.//Binding'):
    device = binding.get('Device')
    if device and device != "{NoDevice}":
        match = re.match(r'[0-9A-Fa-f]{8}', device.replace('{', '').replace('}', ''))
        if match:
            device_ids.add(match.group().upper())

print("Unique device IDs in binds file:")
for d in device_ids:
    print(f"  {d}")

# Use evdev to check connected input devices
devices = [InputDevice(path) for path in list_devices()]
available_ids = {}
print("\nConnected devices:")
for dev in devices:
    vendor = f"{dev.info.vendor:04X}"
    product = f"{dev.info.product:04X}"
    device_id = vendor + product
    available_ids[device_id] = dev.name
    print(f"  {device_id}: {dev.name}")

# Compare
missing = device_ids - set(available_ids.keys())
if missing:
    print("\nMissing devices:")
    for d in missing:
        print(f"  {d}")
else:
    print("\nAll devices from the binds file are currently connected.")
