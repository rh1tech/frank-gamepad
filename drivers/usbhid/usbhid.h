/*
 * USB HID host driver
 * Based on TinyUSB HID host example
 * SPDX-License-Identifier: MIT
 */

#ifndef USBHID_H
#define USBHID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// frank-gamepad minimal API — exposes raw HID reports and device info
// so the capture tool can map any gamepad's buttons.
//--------------------------------------------------------------------

#define USBHID_MAX_REPORT_LEN 64
#define USBHID_MAX_STRING_LEN 64

typedef enum {
    USBHID_SOURCE_NONE = 0,
    USBHID_SOURCE_HID,
    USBHID_SOURCE_XINPUT,
} usbhid_source_t;

typedef struct {
    int connected;                              // non-zero when a gamepad/joystick is attached
    usbhid_source_t source;                     // transport that delivered the report
    uint16_t vid;
    uint16_t pid;
    char manufacturer[USBHID_MAX_STRING_LEN];
    char product[USBHID_MAX_STRING_LEN];
    char serial[USBHID_MAX_STRING_LEN];
    uint16_t report_len;                        // length of latest report (synthetic for XInput)
    uint8_t report[USBHID_MAX_REPORT_LEN];      // latest raw report bytes
    uint32_t report_seq;                        // increments on every new report
} usbhid_device_t;

#ifdef USB_HID_ENABLED

// Bring up TinyUSB host stack
void usbhid_init(void);

// Poll TinyUSB - call this frequently from the main loop
void usbhid_task(void);

// Non-zero if a HID gamepad/joystick is currently attached
int usbhid_gamepad_connected(void);

// Copy a snapshot of the connected device's info into *out.
// Returns non-zero if a device is present (out is valid), zero otherwise.
int usbhid_get_device(usbhid_device_t *out);

#else

// Debug build (CDC stdio instead of USB host): USB gamepad is unavailable.
// Provide inline no-ops so callers don't need #ifdef USB_HID_ENABLED sprinkled
// through the state machine.
#include <string.h>
static inline void usbhid_init(void) {}
static inline void usbhid_task(void) {}
static inline int  usbhid_gamepad_connected(void) { return 0; }
static inline int  usbhid_get_device(usbhid_device_t *out) {
    if (out) memset(out, 0, sizeof(*out));
    return 0;
}

#endif // USB_HID_ENABLED

#ifdef __cplusplus
}
#endif

#endif /* USBHID_H */
