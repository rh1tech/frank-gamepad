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
#include "xinput_host.h"
#include <stdio.h>
#include <string.h>

#if CFG_TUH_ENABLED

//--------------------------------------------------------------------
// Device state
//--------------------------------------------------------------------

static volatile int g_connected = 0;
static usbhid_source_t g_source = USBHID_SOURCE_NONE;
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
// XInput class driver registration.
//
// TinyUSB looks up extra host class drivers via usbh_app_driver_get_cb().
// Xbox 360 / Xbox One / XboxOG controllers do not expose a HID interface,
// so without this callback the mount path above never fires for them.
//--------------------------------------------------------------------
usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &usbh_xinput_driver;
}

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
    g_source = USBHID_SOURCE_HID;

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
    if (g_connected && g_source == USBHID_SOURCE_HID &&
        dev_addr == g_dev_addr && instance == g_instance) {
        printf("[usbhid] unmount addr=%u\n", dev_addr);
        g_connected = 0;
        g_source = USBHID_SOURCE_NONE;
        g_report_len = 0;
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    // Silent hot path — logging the report from inside the HID IRQ context
    // blocks on UART TX at 9600 baud and wedges everything. The main loop
    // consumes reports via usbhid_get_device() and can log if it wants.
    if (g_connected && g_source == USBHID_SOURCE_HID &&
        dev_addr == g_dev_addr && instance == g_instance && report && len > 0) {
        uint16_t n = len > USBHID_MAX_REPORT_LEN ? USBHID_MAX_REPORT_LEN : len;
        memcpy(g_report, report, n);
        g_report_len = n;
        g_report_seq++;
    }
    tuh_hid_receive_report(dev_addr, instance);
}

//--------------------------------------------------------------------
// XInput callbacks.
//
// Build a stable 8-byte pseudo-report so the rest of the capture pipeline
// (byte-wise baseline diffing in main.c) works unchanged:
//
//   byte[0] = wButtons low  (DPAD + START/BACK + LS/RS + LB/RB + GUIDE/SHARE)
//   byte[1] = wButtons high (A / B / X / Y)
//   byte[2] = bLeftTrigger
//   byte[3] = bRightTrigger
//   byte[4] = sThumbLX high byte (centre ≈ 0x00, +/- 0x80)
//   byte[5] = sThumbLY high byte
//   byte[6] = sThumbRX high byte
//   byte[7] = sThumbRY high byte
//
// This keeps the saved file's "byte[N]:+0xMM" format portable between
// HID and XInput pads — downstream firmware can interpret either the
// raw HID report or the canonical 8-byte synthetic XInput frame.
//--------------------------------------------------------------------
#define USBHID_XINPUT_REPORT_LEN 8

void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xid_itf) {
    printf("[usbhid] xinput mount addr=%u inst=%u type=%u\n",
           dev_addr, instance, (unsigned)xid_itf->type);

    if (g_connected) {
        printf("[usbhid]   already have a device, ignoring xinput\n");
        // Wireless Xbox 360 pads need a present-inquiry before they stream
        // reports — keep polling either way so the existing device isn't
        // disturbed.
        tuh_xinput_receive_report(dev_addr, instance);
        return;
    }

    g_dev_addr = dev_addr;
    g_instance = instance;
    g_source = USBHID_SOURCE_XINPUT;

    g_vid = 0;
    g_pid = 0;
    tuh_vid_pid_get(dev_addr, &g_vid, &g_pid);
    g_manufacturer[0] = 0;
    g_product[0] = 0;
    g_serial[0] = 0;

    printf("[usbhid]   xinput VID=0x%04X PID=0x%04X\n", g_vid, g_pid);

    g_report_len = 0;
    g_report_seq = 0;
    g_connected = 1;

    // Xbox 360 wired/wireless: show the "player 1" LED quadrant so the user
    // can visually confirm the pad associated. Xbox One pads ignore it.
    tuh_xinput_set_led(dev_addr, instance, 1, true);
    tuh_xinput_receive_report(dev_addr, instance);
    printf("[usbhid]   xinput armed\n");
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (g_connected && g_source == USBHID_SOURCE_XINPUT &&
        dev_addr == g_dev_addr && instance == g_instance) {
        printf("[usbhid] xinput unmount addr=%u\n", dev_addr);
        g_connected = 0;
        g_source = USBHID_SOURCE_NONE;
        g_report_len = 0;
    }
}

void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                   xinputh_interface_t const *xid_itf,
                                   uint16_t len) {
    (void)len;

    if (g_connected && g_source == USBHID_SOURCE_XINPUT &&
        dev_addr == g_dev_addr && instance == g_instance && xid_itf) {
        const xinput_gamepad_t *p = &xid_itf->pad;
        uint8_t synth[USBHID_XINPUT_REPORT_LEN];
        synth[0] = (uint8_t)(p->wButtons & 0xFF);
        synth[1] = (uint8_t)((p->wButtons >> 8) & 0xFF);
        synth[2] = p->bLeftTrigger;
        synth[3] = p->bRightTrigger;
        synth[4] = (uint8_t)((uint16_t)p->sThumbLX >> 8);
        synth[5] = (uint8_t)((uint16_t)p->sThumbLY >> 8);
        synth[6] = (uint8_t)((uint16_t)p->sThumbRX >> 8);
        synth[7] = (uint8_t)((uint16_t)p->sThumbRY >> 8);

        memcpy(g_report, synth, sizeof(synth));
        g_report_len = sizeof(synth);
        g_report_seq++;
    }
    tuh_xinput_receive_report(dev_addr, instance);
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
    out->source = g_source;
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
