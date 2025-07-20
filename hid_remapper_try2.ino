/*********************************************************************
 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 Copyright (c) 2021 NeKuNeKo for Adafruit Industries
 Copyright (c) 2019 Ha Thach for Adafruit Industries
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/

/*
  Based on 
  https://learn.adafruit.com/adafruit-feather-rp2040-with-usb-type-a-host
  https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/examples/DualRole/HID/hid_remapper/hid_remapper.ino
  https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/examples/HID/hid_gamepad/hid_gamepad.ino

  Included support files are unmodified from those repositories.
*/

/* Elite Dangerous Keyboard to Gamepad Converter
 * Maps A-Z and F1-F12 keys from USB keyboard to gamepad controls
 * A-Z = Buttons 1-26, F1-F6 = Buttons 27-32, F7-F12 = D-pad directions
 * NeoPixel status: Red=no device, Orange=connected idle, Green=key pressed
 */

// USBHost is defined in usbh_helper.h
#include "usbh_helper.h"
#include <Adafruit_NeoPixel.h>

// NeoPixel setup for Adafruit Feather
#define NEOPIXEL_PIN PIN_NEOPIXEL
#define NEOPIXEL_COUNT 1
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Status tracking variables
bool usb_device_connected = false;
bool key_currently_pressed = false;
unsigned long last_key_time = 0;
const unsigned long KEY_TIMEOUT = 100; // ms to wait before going back to orange

// Status colors for Neopixel
#define COLOR_NO_DEVICE    0xFF0000   // Red (RGB: 255, 0, 0)
#define COLOR_DEVICE_IDLE  0xFFA500   // Orange (RGB: 255, 165, 0)
#define COLOR_KEY_PRESSED  0x00FF00   // Green (RGB: 0, 255, 0)

// HID report descriptor using TinyUSB's template
// Single Report (no ID) descriptor
// provides 32 generic buttons, so we also use dpad
uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_GAMEPAD()
};

// USB HID object
Adafruit_USBD_HID usb_hid;

// Report payload defined in src/class/hid/hid.h
// - For Gamepad Button Bit Mask see  hid_gamepad_button_bm_t
// - For Gamepad Hat    Bit Mask see  hid_gamepad_hat_t
hid_gamepad_report_t gp;

// NeoPixel status update function
void update_neopixel_status() {
  uint32_t color;
  
  if (!usb_device_connected) {
    color = COLOR_NO_DEVICE;  // Red - no USB device
  } else if (key_currently_pressed || (millis() - last_key_time < KEY_TIMEOUT)) {
    color = COLOR_KEY_PRESSED;  // Green - key active
  } else {
    color = COLOR_DEVICE_IDLE;  // Orange - device connected but idle
  }
  
  pixels.setPixelColor(0, color);
  pixels.show();
}

// Map keyboard input to gamepad controls
void map_keyboard_to_gamepad(hid_keyboard_report_t const *kb_report, hid_gamepad_report_t *gp_report) {
  // Clear gamepad report
  memset(gp_report, 0, sizeof(hid_gamepad_report_t));
  
  // Check if any keys are pressed for NeoPixel status
  key_currently_pressed = false;
  for (uint8_t i = 0; i < 6; i++) {
    if (kb_report->keycode[i] != 0) {
      key_currently_pressed = true;
      last_key_time = millis();
      break;
    }
  }
  
  // Process each key in the keyboard report
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t keycode = kb_report->keycode[i];
    if (keycode == 0) continue; // Empty slot
    
    uint8_t button_num = 0;
    bool is_dpad = false;
    uint8_t dpad_direction = 0;
    
    // Map A-Z (HID keycodes 0x04-0x1D) to buttons 1-26
    if (keycode >= 0x04 && keycode <= 0x1D) {
      button_num = keycode - 0x04 + 1; // A=1, B=2, ..., Z=26
    }
    // Map F1-F12 (HID keycodes 0x3A-0x45) to buttons 27-32, then dpad
    else if (keycode >= 0x3A && keycode <= 0x45) {
      uint8_t f_key_index = keycode - 0x3A; // F1=0, F2=1, ..., F12=11
      if (f_key_index < 6) {
        button_num = 27 + f_key_index; // F1=27, F2=28, ..., F6=32
      } else {
        // F7-F12 map to dpad directions
        is_dpad = true;
        switch (f_key_index) {
          case 6:  dpad_direction = 1; break; // F7 = UP
          case 7:  dpad_direction = 2; break; // F8 = UP_RIGHT  
          case 8:  dpad_direction = 3; break; // F9 = RIGHT
          case 9:  dpad_direction = 4; break; // F10 = DOWN_RIGHT
          case 10: dpad_direction = 5; break; // F11 = DOWN
          case 11: dpad_direction = 6; break; // F12 = DOWN_LEFT
        }
      }
    }
    
    // Apply the mapping
    if (is_dpad) {
      gp_report->hat = dpad_direction;
    } else if (button_num > 0 && button_num <= 32) {
      gp_report->buttons |= (1UL << (button_num - 1)); // Convert to 0-based bit position
    }
  }
}

void setup() {
  // Manual begin() is required on core without built-in support e.g. mbed rp2040
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  Serial.begin(115200);
  
  // Initialize NeoPixel
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH); // Turn on NeoPixel power
  pixels.begin();
  pixels.setBrightness(50); // Adjust brightness (0-255)
  pixels.setPixelColor(0, COLOR_NO_DEVICE); // Start with red (no device)
  pixels.show();
 
  // Setup HID
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  // If already enumerated, additional class driverr begin() e.g msc, hid, midi won't take effect until re-enumeration
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  //while ( !Serial ) delay(10);   // wait for native usb
  Serial.println("TinyUSB Host HID Elite Dangerous Keyboard to Gamepad Converter");
}

//--------------------------------------------------------------------+
// For RP2040 use both core0 for device stack, core1 for host stack
//--------------------------------------------------------------------+

void loop() {
  #ifdef TINYUSB_NEED_POLLING_TASK
  // Manual call tud_task since it isn't called by Core's background
  TinyUSBDevice.task();
  #endif

  // not enumerated()/mounted() yet: nothing to do
  if (!TinyUSBDevice.mounted()) {
    return;
  }

  // Update NeoPixel status
  update_neopixel_status();

  // Just wait for keyboard input, no automatic gamepad testing
  delay(10);
}

//------------- Core1 -------------//
void setup1() {
  // configure pio-usb: defined in usbh_helper.h
  rp2040_configure_pio_usb();

  // run host stack on controller (rhport) 1
  // Note: For rp2040 pico-pio-usb, calling USBHost.begin() on core1 will have most of the
  // host bit-banging processing works done in core1 to free up core0 for other works
  USBHost.begin(1);
}

void loop1() {
  USBHost.task();
}

//--------------------------------------------------------------------+
// TinyUSB Host callbacks
//--------------------------------------------------------------------+
extern "C"
{

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use.
// tuh_hid_parse_report_descriptor() can be used to parse common/simple enough
// descriptor. Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE,
// it will be skipped therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
  (void) desc_report;
  (void) desc_len;
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  Serial.printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
  Serial.printf("VID = %04x, PID = %04x\r\n", vid, pid);

  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
    Serial.printf("HID Keyboard\r\n");
    usb_device_connected = true; // Update status for NeoPixel
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      Serial.printf("Error: cannot request to receive report\r\n");
    }
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  Serial.printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
  usb_device_connected = false; // Update status for NeoPixel
  key_currently_pressed = false; // Clear key state
}

// Process incoming keyboard reports and convert to gamepad
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
  Serial.printf("Report (%d bytes): ", len);
  for(int i = 0; i < len; i++) {
    Serial.printf("%02x ", report[i]);
  }
  Serial.println();

  hid_keyboard_report_t *kb_report = nullptr;
  
  // Handle different report lengths
  if (len == 8) {
    kb_report = (hid_keyboard_report_t*)report;
  } else if (len == 18) {
    // For 18-byte reports, keyboard data usually starts at beginning
    kb_report = (hid_keyboard_report_t*)report;
  } else {
    Serial.printf("Unsupported report length: %d\r\n", len);
    // Still continue to request next report
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      Serial.printf("Error: cannot request to receive report\r\n");
    }
    return;
  }
  
  // Debug: Print parsed keyboard data
  Serial.printf("Keyboard - Modifier: 0x%02x, Keys: ", kb_report->modifier);
  for (int i = 0; i < 6; i++) {
    if (kb_report->keycode[i] != 0) {
      Serial.printf("0x%02x ", kb_report->keycode[i]);
    }
  }
  Serial.println();
  
  // Map keyboard to gamepad
  hid_gamepad_report_t gamepad_report;
  map_keyboard_to_gamepad(kb_report, &gamepad_report);
  
  // Debug: Print gamepad mapping
  if (gamepad_report.buttons != 0) {
    Serial.printf("Gamepad Buttons: 0x%08lx\r\n", gamepad_report.buttons);
  }
  if (gamepad_report.hat != 0) {
    Serial.printf("Gamepad Hat: %d\r\n", gamepad_report.hat);
  }
  
  // Send gamepad report to PC
  while (!usb_hid.ready()) {
    yield();
  }
  usb_hid.sendReport(0, &gamepad_report, sizeof(gamepad_report));

  // Update NeoPixel status after processing keys
  update_neopixel_status();

  // Continue to request next report
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    Serial.printf("Error: cannot request to receive report\r\n");
  }
}

}
