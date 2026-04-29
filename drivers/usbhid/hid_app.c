/*
 * USB HID host driver - frank-gamepad capture variant.
 *
 * Exposes raw HID reports plus the attached device's VID/PID/strings so a
 * button-mapping tool can diff reports and persist a per-device log.
 *
 * Based on TinyUSB HID host example.
 * SPDX-License-Identifier: MIT
 */

#include "tusb.h"
#include "usbhid.h"
#include <stdio.h>
#include <string.h>

#if CFG_TUH_ENABLED

//--------------------------------------------------------------------
// Device state
//--------------------------------------------------------------------

static volatile int g_connected = 0;
static uint8_t g_dev_addr = 0;
static uint8_t g_instance = 0;

static uint16_t g_vid = 0;
static uint16_t g_pid = 0;
static char g_manufacturer[USBHID_MAX_STRING_LEN] = {0};
static char g_product[USBHID_MAX_STRING_LEN] = {0};
static char g_serial[USBHID_MAX_STRING_LEN] = {0};

static uint8_t g_report[USBHID_MAX_REPORT_LEN];
static volatile uint16_t g_report_len = 0;
static volatile uint32_t g_report_seq = 0;

//--------------------------------------------------------------------
// Mount / unmount callbacks
//
// Rule: do NOT call tuh_descriptor_get_*_sync() here. Those functions
// internally re-enter tuh_task(), and this callback is already running
// inside tuh_task() — the reentrancy hangs the USB stack and starves the
// HDMI scanline ISR (symptom: TV loses signal seconds after a gamepad is
// plugged in). VID/PID is cheap and safe; strings are dropped.
//--------------------------------------------------------------------

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    (void)desc_report;
    (void)desc_len;

    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    printf("[usbhid] mount addr=%u inst=%u proto=%u\n", dev_addr, instance, itf_protocol);

    // Skip boot-protocol keyboards/mice — we only care about gamepads.
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD || itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        printf("[usbhid]   kbd/mouse, skipping\n");
        tuh_hid_receive_report(dev_addr, instance);
        return;
    }

    if (g_connected) {
        printf("[usbhid]   already have a device, ignoring\n");
        tuh_hid_receive_report(dev_addr, instance);
        return;
    }

    g_dev_addr = dev_addr;
    g_instance = instance;

    g_vid = 0;
    g_pid = 0;
    tuh_vid_pid_get(dev_addr, &g_vid, &g_pid);
    g_manufacturer[0] = 0;
    g_product[0] = 0;
    g_serial[0] = 0;

    printf("[usbhid]   VID=0x%04X PID=0x%04X\n", g_vid, g_pid);

    g_report_len = 0;
    g_report_seq = 0;
    g_connected = 1;

    tuh_hid_receive_report(dev_addr, instance);
    printf("[usbhid]   armed\n");
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (g_connected && dev_addr == g_dev_addr && instance == g_instance) {
        printf("[usbhid] unmount addr=%u\n", dev_addr);
        g_connected = 0;
        g_report_len = 0;
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    // Silent hot path — logging the report from inside the HID IRQ context
    // blocks on UART TX at 9600 baud and wedges everything. The main loop
    // consumes reports via usbhid_get_device() and can log if it wants.
    if (g_connected && dev_addr == g_dev_addr && instance == g_instance && report && len > 0) {
        uint16_t n = len > USBHID_MAX_REPORT_LEN ? USBHID_MAX_REPORT_LEN : len;
        memcpy(g_report, report, n);
        g_report_len = n;
        g_report_seq++;
    }
    tuh_hid_receive_report(dev_addr, instance);
}

//--------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------

void usbhid_init(void) {
    tuh_init(BOARD_TUH_RHPORT);
    g_connected = 0;
    g_report_len = 0;
    g_report_seq = 0;
}

void usbhid_task(void) {
    tuh_task();
}

int usbhid_gamepad_connected(void) {
    return g_connected;
}

int usbhid_get_device(usbhid_device_t *out) {
    if (!out) return 0;
    if (!g_connected) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    out->connected = 1;
    out->vid = g_vid;
    out->pid = g_pid;
    strncpy(out->manufacturer, g_manufacturer, sizeof(out->manufacturer) - 1);
    out->manufacturer[sizeof(out->manufacturer) - 1] = 0;
    strncpy(out->product, g_product, sizeof(out->product) - 1);
    out->product[sizeof(out->product) - 1] = 0;
    strncpy(out->serial, g_serial, sizeof(out->serial) - 1);
    out->serial[sizeof(out->serial) - 1] = 0;
    // Snapshot report; retry if an ISR races with us (report_seq changed).
    uint32_t seq_before;
    do {
        seq_before = g_report_seq;
        out->report_len = g_report_len;
        if (out->report_len > USBHID_MAX_REPORT_LEN) out->report_len = USBHID_MAX_REPORT_LEN;
        memcpy(out->report, g_report, out->report_len);
        out->report_seq = seq_before;
    } while (seq_before != g_report_seq);
    return 1;
}

#endif // CFG_TUH_ENABLED
