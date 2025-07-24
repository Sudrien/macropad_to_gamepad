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

/* Elite Dangerous Keyboard to Custom Button Converter - macOS Compatible
 * Maps A-Z and F1-F12 keys from USB keyboard to standard HID button controls
 * A-Z = Buttons 17-42, F1-F12 = Buttons 43-54
 * Uses standard HID button ranges for better macOS compatibility
 * NeoPixel status: Red=no device, Orange=connected idle, Green=key pressed
 * 
 * Modified to use extended/high standard HID button ranges instead of Linux-specific codes
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

// Button mapping constants
#define BUTTON_A_START     17  // A-Z keys start at button 17
#define BUTTON_F_START     43  // F1-F12 keys start at button 43
#define TOTAL_BUTTONS      48  // Buttons 17-64 (48 total buttons)

// Custom HID report descriptor with standard HID button usage codes
// Uses buttons 17-64 for better macOS compatibility
uint8_t const desc_hid_report[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
  0x09, 0x04,        // Usage (Gamepad)
  0xa1, 0x01,        // Collection (Application)
  
  // X and Y axes (dummy, always centered)
  0x09, 0x30,        //   Usage (X)
  0x09, 0x31,        //   Usage (Y)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x00,  //   Logical Maximum (255)
  0x75, 0x08,        //   Report Size (8)
  0x95, 0x02,        //   Report Count (2)
  0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
  
  // Z axis (dummy, always centered)
  0x09, 0x32,        //   Usage (Z)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x00,  //   Logical Maximum (255)
  0x75, 0x08,        //   Report Size (8)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
  
  // Hat Switch (D-pad) - dummy, always centered
  0x09, 0x39,        //   Usage (Hat switch)
  0x15, 0x01,        //   Logical Minimum (1)
  0x25, 0x08,        //   Logical Maximum (8)
  0x35, 0x00,        //   Physical Minimum (0)
  0x46, 0x3B, 0x01,  //   Physical Maximum (315)
  0x66, 0x14, 0x00,  //   Unit (System: English Rotation, Length: Centimeter)
  0x75, 0x04,        //   Report Size (4)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x42,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)
  
  // Padding for hat switch (4 bits)
  0x75, 0x04,        //   Report Size (4)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
  
  // Standard HID buttons 17-64 (48 buttons total) for better macOS compatibility
  0x05, 0x09,        //   Usage Page (Button)
  0x19, 0x11,        //   Usage Minimum (Button 17)
  0x29, 0x40,        //   Usage Maximum (Button 64)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x95, 0x30,        //   Report Count (48) - 48 buttons
  0x75, 0x01,        //   Report Size (1)
  0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
  
  0xc0,              // End Collection
};

// USB HID object
Adafruit_USBD_HID usb_hid;

// Custom report structure
typedef struct {
  uint8_t x_axis;     // X axis (always centered at 128)
  uint8_t y_axis;     // Y axis (always centered at 128)
  uint8_t z_axis;     // Z axis (always centered at 128)
  uint8_t hat;        // Hat switch - upper 4 bits unused, lower 4 bits for hat (always 0 = centered)
  uint8_t buttons[6]; // 48 buttons packed into 6 bytes
} __attribute__((packed)) custom_hid_report_t;

custom_hid_report_t custom_report;

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

// Map keyboard input to standard HID button controls
void map_keyboard_to_standard_buttons(hid_keyboard_report_t const *kb_report, custom_hid_report_t *custom_report) {
  // Clear custom report
  memset(custom_report, 0, sizeof(custom_hid_report_t));
  
  // Set dummy axes to center position
  custom_report->x_axis = 128;
  custom_report->y_axis = 128;
  custom_report->z_axis = 128;
  
  // Set hat switch to center (no direction pressed)
  custom_report->hat = 0; // 0 = centered/no direction
  
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
    bool valid_mapping = false;
    
    // Map A-Z (HID keycodes 0x04-0x1D) to buttons 0-25 (which correspond to HID buttons 17-42)
    if (keycode >= 0x04 && keycode <= 0x1D) {
      button_num = keycode - 0x04; // A=0, B=1, ..., Z=25 (internal numbering)
      valid_mapping = true;
    }
    // Map F1-F12 (HID keycodes 0x3A-0x45) to buttons 26-37 (HID buttons 43-54)
    else if (keycode >= 0x3A && keycode <= 0x45) {
      uint8_t f_key_index = keycode - 0x3A; // F1=0, F2=1, ..., F12=11
      button_num = 26 + f_key_index; // F1=26, F2=27, ..., F12=37 (internal numbering)
      valid_mapping = true;
    }
    
    // Apply the mapping
    if (valid_mapping && button_num < TOTAL_BUTTONS) {
      // Set the appropriate bit in the button array
      uint8_t byte_index = button_num / 8;
      uint8_t bit_index = button_num % 8;
      custom_report->buttons[byte_index] |= (1 << bit_index);
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
  Serial.println("TinyUSB Host HID Elite Dangerous Keyboard to Standard Button Converter (macOS Compatible)");
  Serial.println("Button mapping:");
  Serial.println("  A-Z keys → Standard HID buttons 17-42");
  Serial.println("  F1-F12 keys → Standard HID buttons 43-54");
  Serial.println("Added dummy D-pad and axes for better game compatibility");
  Serial.println("Using standard HID button ranges for macOS compatibility");
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

  // Just wait for keyboard input, no automatic testing
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

// Process incoming keyboard reports and convert to standard button reports
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
  
  // Map keyboard to standard HID buttons
  custom_hid_report_t custom_button_report;
  map_keyboard_to_standard_buttons(kb_report, &custom_button_report);
  
  // Debug: Print standard button mapping
  bool any_pressed = false;
  for (int i = 0; i < 6; i++) {
    if (custom_button_report.buttons[i] != 0) {
      any_pressed = true;
      break;
    }
  }
  
  if (any_pressed) {
    Serial.printf("Standard HID Buttons: ");
    for (int byte_idx = 0; byte_idx < 6; byte_idx++) {
      if (custom_button_report.buttons[byte_idx] != 0) {
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
          if (custom_button_report.buttons[byte_idx] & (1 << bit_idx)) {
            int button_num = (byte_idx * 8) + bit_idx;
            int hid_button = BUTTON_A_START + button_num; // Convert to actual HID button number
            Serial.printf("Btn%d(HID:%d) ", button_num, hid_button);
          }
        }
      }
    }
    Serial.println();
  }
  
  // Send standard button report to PC
  while (!usb_hid.ready()) {
    yield();
  }
  usb_hid.sendReport(0, &custom_button_report, sizeof(custom_button_report));

  // Update NeoPixel status after processing keys
  update_neopixel_status();

  // Continue to request next report
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    Serial.printf("Error: cannot request to receive report\r\n");
  }
}

}
