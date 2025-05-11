#!/usr/bin/env python3

# The point here is to clear up an incongruency of a HOTASAS setup:
# In Elite Dangerous, 6DOF only makes sense in real space, while 4DOF makes sense in Supercruise
# Thuster controls don't work in supercruise, but Throttle works real space.
# This attempts to fix that with a virtual controller that only enables it's throttle axis
# when the game's journals say it's in supercruise mode, or the game is off.
# This single axis is reported as the X axis in game.

import os
import time
import json
import threading
from pathlib import Path
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
from evdev import InputDevice, UInput, ecodes, AbsInfo

# CONFIGURATION -----------------------------------------

# Path to Elite Dangerous journal logs (Proton version)
journal_dir = Path.home() / ".local/share/Steam/steamapps/compatdata/359320/pfx/drive_c/users/steamuser/Saved Games/Frontier Developments/Elite Dangerous"

# Path to physical joystick device (use `evtest` to find correct one)
device_path = "/dev/input/event7"  # Replace this with your actual event device

# --------------------------------------------------------

SUPERCRUISE_ACTIVE = False
GAME_RUNNING = False
last_log_update = time.time()
last_throttle_value = 0

# Open real joystick
try:
    dev = InputDevice(device_path)
except FileNotFoundError:
    print(f"Device {device_path} not found.")
    exit(1)

print(f"Opened input device: {dev.name} {dev.info.vendor:04X}:{dev.info.product:04X}")

# Create virtual joystick throttle device (0–1023)
# Two buttons required to be recognized by Steam & Elite Dangerous. They will never be pressed.
capabilities = {
    ecodes.EV_KEY: [740, 741],
    ecodes.EV_ABS: [
        (ecodes.ABS_THROTTLE, AbsInfo(value=1, min=0, max=1023, fuzz=0, flat=0, resolution=0)),
    ]
}

ui = UInput(capabilities, name='R THQ Toggle', version=0x1, vendor=0x1234, product=0x5678)
print(f"Created virtual device: {ui.device.name}")

# Function to forward or block throttle input
def forward_throttle(value=None, force=False):
    global last_throttle_value
    if value is not None:
        last_throttle_value = value
    value_to_use = last_throttle_value

    if SUPERCRUISE_ACTIVE or not GAME_RUNNING or force:
        ui.write(ecodes.EV_ABS, ecodes.ABS_THROTTLE, value_to_use)
    else:
        ui.write(ecodes.EV_ABS, ecodes.ABS_THROTTLE, 0)
    ui.syn()

# Send initial value
forward_throttle(force=True)

# Watchdog handler to monitor journal logs
class JournalHandler(FileSystemEventHandler):
    def on_modified(self, event):
        global SUPERCRUISE_ACTIVE, GAME_RUNNING, last_log_update
        if not event.is_directory and event.src_path.endswith(".log"):
            last_log_update = time.time()
            if not GAME_RUNNING:
                print("[ED] Game journal detected.")
            GAME_RUNNING = True
            try:
                with open(event.src_path, 'r', encoding='utf-8') as f:
                    lines = f.readlines()[-10:]  # Check last few lines
                    for line in lines:
                        if not line.strip():
                            continue
                        data = json.loads(line)
                        if data.get("event") == "SupercruiseEntry":
                            SUPERCRUISE_ACTIVE = True
                            print("[ED] Entered Supercruise")
                            forward_throttle(force=True)
                        elif data.get("event") == "SupercruiseExit":
                            SUPERCRUISE_ACTIVE = False
                            print("[ED] Exited Supercruise")
                            forward_throttle(0, force=True)
            except Exception as e:
                print(f"Error reading journal: {e}")

# Monitor game status separately
def monitor_game_status():
    global GAME_RUNNING
    while True:
        if time.time() - last_log_update > 15:
            if GAME_RUNNING:
                print("[ED] Game not running or journal inactive.")
            GAME_RUNNING = False
        time.sleep(5)

# Start background thread
threading.Thread(target=monitor_game_status, daemon=True).start()

# Start journal monitor
observer = Observer()
observer.schedule(JournalHandler(), str(journal_dir), recursive=False)
observer.start()
print("Monitoring Elite Dangerous journal...")

# Start input loop
try:
    print("Reading throttle input...")
    for event in dev.read_loop():
        if event.type == ecodes.EV_ABS and event.code == ecodes.ABS_THROTTLE:
            forward_throttle(event.value)
except KeyboardInterrupt:
    print("Interrupted by user.")
finally:
    print("Shutting down...")
    observer.stop()
    observer.join()
    ui.close()
