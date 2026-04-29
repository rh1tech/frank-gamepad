/*
 * frank-gamepad — USB gamepad button-mapping capture tool for RP2350 (M2).
 *
 * Displays a SNES-controller schematic on HDMI, prompts the user to press
 * each of the 12 SNES buttons in order, diffs the HID reports to identify
 * which bytes/bits change, then writes the mapping to SD as a plain text
 * file named after the device (VID_PID).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#ifndef USB_HID_ENABLED
#include "pico/stdio_usb.h"
#endif

#include "board_config.h"
#include "HDMI.h"
#include "ff.h"
#include "ui.h"
#include "usbhid/usbhid.h"

#define SCREEN_W UI_SCREEN_WIDTH
#define SCREEN_H UI_SCREEN_HEIGHT

// True double-buffering (page flip, no copy).
//
// HDMI ISR reads `SCREEN[!current_buffer]` every scanline. The CPU draws
// into `SCREEN[draw_buf]` (which is always the NON-displayed slot) and then
// atomically flips `current_buffer`. After the flip, what the CPU used to
// draw into is now being shown, and the slot that was shown becomes the
// new back buffer — it is explicitly cleared and rebuilt by the caller on
// every present, so there is never any copy between buffers.
uint8_t __attribute__((aligned(4))) SCREEN[2][SCREEN_W * SCREEN_H];
volatile uint32_t current_buffer = 0;
static uint32_t draw_buf = 1;               // start writing to slot 1; slot 0 shown
#define FRAME (SCREEN[draw_buf])

static void flip_buffers(void) {
    // Flip: publish draw_buf to the ISR, then start drawing into the other.
    current_buffer = !draw_buf;
    draw_buf ^= 1;
}

//-----------------------------------------------------------------------------
// Scene model: whatever we want the user to see is described by g_scene,
// and render_scene() draws the ENTIRE frame from scratch each time. No
// helper function ever flips buffers on its own — only render_scene() does.
//-----------------------------------------------------------------------------

typedef enum {
    SCENE_WAITING,          // "PLUG IN A USB GAMEPAD"
    SCENE_BASELINE,         // "CAPTURING BASELINE"
    SCENE_BASELINE_FAIL,    // "BASELINE TIMEOUT"
    SCENE_PROMPT,           // highlight one button, prompt press
    SCENE_CAPTURED,         // button captured, green flash
    SCENE_SAVING,
    SCENE_SAVED,
    SCENE_SAVE_FAIL,
} scene_kind_t;

typedef struct {
    scene_kind_t kind;
    const usbhid_device_t *dev;     // may be NULL
    ui_button_t prompt_btn;         // for PROMPT / CAPTURED
    int prompt_idx;                 // 1..12
    int prompt_total;
} scene_t;

static scene_t g_scene;

static FATFS g_fs;

// Incremented by Core 1 ~every 100 ms. Core 0 samples this; if it stops
// moving we know Core 1 itself has crashed vs. just HDMI dying.
static volatile uint32_t core1_heartbeat = 0;

//=============================================================================
// Capture logic
//=============================================================================

// Represents what changed between the baseline and the pressed report.
typedef struct {
    // For each byte of the report, how did the value change?
    //   mask_high: bits that went 0->1  (button pressed)
    //   mask_low:  bits that went 1->0  (button released from default high)
    //   value:     raw value observed when pressed
    //   delta:     value - baseline (for analog/axis bytes, useful context)
    uint8_t mask_high[USBHID_MAX_REPORT_LEN];
    uint8_t mask_low[USBHID_MAX_REPORT_LEN];
    uint8_t value[USBHID_MAX_REPORT_LEN];
    int16_t delta[USBHID_MAX_REPORT_LEN];
    uint16_t len;
    bool captured;
} button_capture_t;

static const ui_button_t CAPTURE_ORDER[UI_BTN_COUNT] = {
    UI_BTN_UP, UI_BTN_DOWN, UI_BTN_LEFT, UI_BTN_RIGHT,
    UI_BTN_SELECT, UI_BTN_START,
    UI_BTN_Y, UI_BTN_B, UI_BTN_A, UI_BTN_X,
    UI_BTN_L, UI_BTN_R,
};

//-----------------------------------------------------------------------------
// Rendering helpers
//-----------------------------------------------------------------------------

// Paint the header strip (title + separator).
static void paint_header(void) {
    ui_draw_text_centered(FRAME, 3, "FRANK-GAMEPAD CAPTURE", UI_COLOR_TEXT);
    ui_fill_rect(FRAME, 0, 13, SCREEN_W, 1, UI_COLOR_DIM);
}

static void paint_status(const char *msg, uint8_t color) {
    ui_draw_text_centered(FRAME, 184, msg, color);
}

static void paint_device_line(const usbhid_device_t *dev) {
    char line[80];
    if (dev && dev->connected) {
        snprintf(line, sizeof(line), "VID=%04X PID=%04X", dev->vid, dev->pid);
    } else {
        snprintf(line, sizeof(line), "NO DEVICE");
    }
    ui_draw_text_centered(FRAME, 208, line, UI_COLOR_DIM);
}

// Compose the complete frame described by g_scene into the back buffer,
// then flip. This is the ONLY function that ever touches buffers directly
// from the scene layer — every other caller just updates g_scene and calls
// render_scene().
static void render_scene(void) {
    ui_clear(FRAME, UI_COLOR_BG);
    paint_header();
    ui_draw_schematic(FRAME);

    switch (g_scene.kind) {
        case SCENE_WAITING:
            paint_status("PLUG IN A USB GAMEPAD", UI_COLOR_HIGHLIGHT);
            break;

        case SCENE_BASELINE:
            paint_status("CAPTURING BASELINE... HANDS OFF", UI_COLOR_HIGHLIGHT);
            break;

        case SCENE_BASELINE_FAIL:
            paint_status("BASELINE TIMEOUT - RETRY", UI_COLOR_PRESSED);
            break;

        case SCENE_PROMPT: {
            ui_highlight_button(FRAME, g_scene.prompt_btn, UI_COLOR_HIGHLIGHT);
            char prompt[64];
            snprintf(prompt, sizeof(prompt), "PRESS %s  (%d/%d)",
                     ui_button_label(g_scene.prompt_btn),
                     g_scene.prompt_idx, g_scene.prompt_total);
            paint_status(prompt, UI_COLOR_TEXT);
            break;
        }

        case SCENE_CAPTURED:
            ui_highlight_button(FRAME, g_scene.prompt_btn, UI_COLOR_OK);
            paint_status("CAPTURED", UI_COLOR_OK);
            break;

        case SCENE_SAVING:
            paint_status("SAVING TO SD CARD...", UI_COLOR_HIGHLIGHT);
            break;

        case SCENE_SAVED:
            paint_status("SAVED - STARTING AGAIN", UI_COLOR_OK);
            break;

        case SCENE_SAVE_FAIL:
            paint_status("SD WRITE FAILED", UI_COLOR_PRESSED);
            break;
    }

    paint_device_line(g_scene.dev);

    flip_buffers();
}

//-----------------------------------------------------------------------------
// Baseline + diff
//-----------------------------------------------------------------------------

// Poll USB and return the latest report snapshot.
// Returns 1 if a new report was observed since *last_seq (updating it).
static int poll_report(usbhid_device_t *dev, uint32_t *last_seq) {
    usbhid_task();
    if (!usbhid_get_device(dev)) return 0;
    if (dev->report_seq == *last_seq) return 0;
    *last_seq = dev->report_seq;
    return 1;
}

// Capture the gamepad's quiescent baseline. Many cheap HID gamepads only
// send reports on state change, so we cannot wait for N consecutive
// identical reports. Instead: grab the first report we see, then watch for
// a settle window (settle_ms) during which nothing new arrives. If a new
// report arrives within the window, adopt it and restart the timer.
//
// LED blinks each loop iteration so we can tell from the outside whether
// the CPU is still running if HDMI signal drops.
static int capture_baseline(usbhid_device_t *dev, uint8_t *baseline, uint32_t timeout_ms) {
    const uint32_t settle_ms = 300;
    uint32_t last_seq = 0;
    uint32_t deadline = to_ms_since_boot(get_absolute_time()) + timeout_ms;
    uint16_t expected_len = 0;
    uint32_t settle_deadline = 0;
    uint32_t tick = 0;

    printf("[cap] baseline start, timeout=%u ms, core1_hb=%u\n",
           (unsigned)timeout_ms, (unsigned)core1_heartbeat);

    // First: wait for any report at all.
    while (to_ms_since_boot(get_absolute_time()) < deadline) {
        gpio_put(PICO_DEFAULT_LED_PIN, (tick++ >> 4) & 1);
        if (poll_report(dev, &last_seq) && dev->report_len > 0) {
            expected_len = dev->report_len;
            memcpy(baseline, dev->report, expected_len);
            settle_deadline = to_ms_since_boot(get_absolute_time()) + settle_ms;
            printf("[cap] first report len=%u, settling %u ms\n",
                   (unsigned)expected_len, (unsigned)settle_ms);
            break;
        }
        sleep_ms(16);
    }
    if (expected_len == 0) { printf("[cap] no first report\n"); return 0; }

    // Second: wait for the settle window to expire with no changes.
    while (to_ms_since_boot(get_absolute_time()) < deadline) {
        gpio_put(PICO_DEFAULT_LED_PIN, (tick++ >> 4) & 1);
        if (to_ms_since_boot(get_absolute_time()) >= settle_deadline) {
            printf("[cap] baseline settled len=%u, core1_hb=%u\n",
                   (unsigned)expected_len, (unsigned)core1_heartbeat);
            return expected_len;
        }
        if (poll_report(dev, &last_seq)) {
            if (dev->report_len == expected_len &&
                memcmp(baseline, dev->report, expected_len) == 0) {
                // Same bytes — no change, keep waiting.
            } else {
                expected_len = dev->report_len;
                if (expected_len > USBHID_MAX_REPORT_LEN) expected_len = USBHID_MAX_REPORT_LEN;
                memcpy(baseline, dev->report, expected_len);
                settle_deadline = to_ms_since_boot(get_absolute_time()) + settle_ms;
            }
        }
        sleep_ms(16);
    }
    return 0;
}

// Returns true once a non-baseline press was observed, and fills *cap.
// Waits indefinitely (caller is responsible for cancel path via disconnect).
static bool capture_one_button(usbhid_device_t *dev,
                               const uint8_t *baseline,
                               uint16_t baseline_len,
                               button_capture_t *cap) {
    uint32_t last_seq = dev->report_seq;

    // Step 1: wait for the report to differ from baseline (press).
    // Always sleep between iterations — even when reports are streaming,
    // otherwise we hammer tuh_task() at ~1 MHz and starve HDMI DMA.
    while (true) {
        usbhid_task();
        if (!usbhid_gamepad_connected()) return false;
        bool got_new = poll_report(dev, &last_seq);
        if (got_new && dev->report_len == baseline_len &&
            memcmp(dev->report, baseline, baseline_len) != 0) {
            break;
        }
        sleep_ms(16);
    }

    // Step 2: debounce — wait ~80ms then take a "stable press" snapshot.
    uint8_t stable[USBHID_MAX_REPORT_LEN];
    memcpy(stable, dev->report, baseline_len);

    uint32_t deadline = to_ms_since_boot(get_absolute_time()) + 80;
    while (to_ms_since_boot(get_absolute_time()) < deadline) {
        usbhid_task();
        if (!usbhid_gamepad_connected()) return false;
        if (poll_report(dev, &last_seq)) {
            if (dev->report_len == baseline_len &&
                memcmp(dev->report, baseline, baseline_len) != 0) {
                memcpy(stable, dev->report, baseline_len);
            }
        }
        sleep_ms(16);
    }

    // Step 3: compute diff.
    memset(cap, 0, sizeof(*cap));
    cap->len = baseline_len;
    for (int i = 0; i < baseline_len; i++) {
        uint8_t b = baseline[i], s = stable[i];
        cap->value[i]     = s;
        cap->mask_high[i] = (uint8_t)(~b & s);   // bits 0->1
        cap->mask_low[i]  = (uint8_t)(b & ~s);   // bits 1->0
        cap->delta[i]     = (int16_t)s - (int16_t)b;
    }
    cap->captured = true;

    // Step 4: wait for release back to baseline (best-effort, with timeout).
    deadline = to_ms_since_boot(get_absolute_time()) + 3000;
    while (to_ms_since_boot(get_absolute_time()) < deadline) {
        usbhid_task();
        if (!usbhid_gamepad_connected()) return false;
        if (poll_report(dev, &last_seq)) {
            if (memcmp(dev->report, baseline, baseline_len) == 0) break;
        }
        sleep_ms(16);
    }
    return true;
}

//-----------------------------------------------------------------------------
// Output — describe a capture as a single human-readable line.
//-----------------------------------------------------------------------------

static void format_capture(const button_capture_t *cap, char *buf, size_t sz) {
    // Produce something like:  byte[5]:+0x20 byte[6]:+0x01
    // For bytes whose change is a value rather than a bitflip, include the
    // absolute value too.
    size_t pos = 0;
    bool any = false;
    for (int i = 0; i < cap->len && pos + 32 < sz; i++) {
        if (cap->mask_high[i] == 0 && cap->mask_low[i] == 0) continue;
        any = true;
        if (cap->mask_high[i] && !cap->mask_low[i]) {
            pos += (size_t)snprintf(buf + pos, sz - pos,
                                    "byte[%d]:+0x%02X ", i, cap->mask_high[i]);
        } else if (cap->mask_low[i] && !cap->mask_high[i]) {
            pos += (size_t)snprintf(buf + pos, sz - pos,
                                    "byte[%d]:-0x%02X ", i, cap->mask_low[i]);
        } else {
            pos += (size_t)snprintf(buf + pos, sz - pos,
                                    "byte[%d]:=0x%02X ", i, cap->value[i]);
        }
    }
    if (!any) {
        snprintf(buf, sz, "(no change detected)");
    } else if (pos > 0 && pos < sz) {
        buf[pos - 1] = 0;   // trim trailing space
    }
}

static void format_bytes(const uint8_t *data, uint16_t len, char *buf, size_t sz) {
    size_t pos = 0;
    for (int i = 0; i < len && pos + 4 < sz; i++) {
        pos += (size_t)snprintf(buf + pos, sz - pos, "%02X ", data[i]);
    }
    if (pos > 0 && pos < sz) buf[pos - 1] = 0;
}

//-----------------------------------------------------------------------------
// Write file to SD
//-----------------------------------------------------------------------------

static bool save_log(const usbhid_device_t *dev,
                     const uint8_t *baseline,
                     uint16_t baseline_len,
                     const button_capture_t *caps) {
    char path[40];
    snprintf(path, sizeof(path), "gamepad_%04X_%04X.txt", dev->vid, dev->pid);

    FIL f;
    FRESULT r = f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (r != FR_OK) {
        printf("f_open(%s) failed: %d\n", path, r);
        return false;
    }

    char line[160];
    char bytes[3 * USBHID_MAX_REPORT_LEN + 1];

    f_printf(&f, "# frank-gamepad capture log\n");
    f_printf(&f, "VID=0x%04X\n", dev->vid);
    f_printf(&f, "PID=0x%04X\n", dev->pid);
    f_printf(&f, "Manufacturer=%s\n", dev->manufacturer[0] ? dev->manufacturer : "(none)");
    f_printf(&f, "Product=%s\n",      dev->product[0]      ? dev->product      : "(none)");
    f_printf(&f, "Serial=%s\n",       dev->serial[0]       ? dev->serial       : "(none)");
    f_printf(&f, "ReportLen=%u\n", baseline_len);
    format_bytes(baseline, baseline_len, bytes, sizeof(bytes));
    f_printf(&f, "Baseline=%s\n\n", bytes);

    for (int i = 0; i < UI_BTN_COUNT; i++) {
        ui_button_t b = CAPTURE_ORDER[i];
        const button_capture_t *c = &caps[b];
        if (c->captured) {
            format_capture(c, line, sizeof(line));
            format_bytes(c->value, c->len, bytes, sizeof(bytes));
            f_printf(&f, "%s=%s\n", ui_button_label(b), line);
            f_printf(&f, "#   raw: %s\n", bytes);
        } else {
            f_printf(&f, "%s=(skipped)\n", ui_button_label(b));
        }
    }

    f_close(&f);
    printf("Wrote %s\n", path);
    return true;
}

//-----------------------------------------------------------------------------
// Top-level capture session — runs once per detected device.
//-----------------------------------------------------------------------------

static void wait_for_device(usbhid_device_t *dev) {
    g_scene.kind = SCENE_WAITING;
    g_scene.dev = NULL;
    render_scene();

    while (true) {
        usbhid_task();
        if (usbhid_gamepad_connected() && usbhid_get_device(dev) && dev->report_len > 0) {
            return;
        }
        sleep_ms(100);
    }
}

// The capture_t array is ~4 KB (12 * sizeof(button_capture_t)), which blows
// the default 2 KB per-core stack on RP2350 — stack-allocated it hardfaults
// on return from capture_baseline. Move to .bss instead.
static button_capture_t s_caps[UI_BTN_COUNT];
static uint8_t s_baseline[USBHID_MAX_REPORT_LEN];

static void run_session(void) {
    usbhid_device_t dev;

    wait_for_device(&dev);

    g_scene.kind = SCENE_BASELINE;
    g_scene.dev = &dev;
    render_scene();

    int baseline_len = capture_baseline(&dev, s_baseline, 3000);
    if (baseline_len <= 0) {
        g_scene.kind = SCENE_BASELINE_FAIL;
        render_scene();
        sleep_ms(2000);
        return;
    }

    memset(s_caps, 0, sizeof(s_caps));

    for (int i = 0; i < UI_BTN_COUNT; i++) {
        if (!usbhid_gamepad_connected()) return;
        ui_button_t b = CAPTURE_ORDER[i];
        printf("[cap] prompt %d/%d = %s, core1_hb=%u\n",
               i + 1, UI_BTN_COUNT, ui_button_label(b), (unsigned)core1_heartbeat);

        g_scene.kind = SCENE_PROMPT;
        g_scene.dev = &dev;
        g_scene.prompt_btn = b;
        g_scene.prompt_idx = i + 1;
        g_scene.prompt_total = UI_BTN_COUNT;
        render_scene();

        if (!capture_one_button(&dev, s_baseline, baseline_len, &s_caps[b])) {
            return;
        }

        g_scene.kind = SCENE_CAPTURED;
        render_scene();
        sleep_ms(250);
    }

    g_scene.kind = SCENE_SAVING;
    render_scene();
    bool ok = save_log(&dev, s_baseline, baseline_len, s_caps);
    g_scene.kind = ok ? SCENE_SAVED : SCENE_SAVE_FAIL;
    render_scene();
    sleep_ms(ok ? 1500 : 3000);
}

//=============================================================================
// Core 1: owns HDMI.
//
// The HDMI driver installs its DMA-complete IRQ handler on whichever core
// calls graphics_init(). We put HDMI on Core 1 so the per-scanline IRQ
// (~31 us period) never contends with TinyUSB's USBCTRL_IRQ on Core 0 —
// heavy USB handlers were blocking the HDMI handler long enough for the
// TV to drop sync during enumeration.
//=============================================================================

static volatile bool core1_ready = false;

static void core1_entry(void) {
    graphics_init(g_out_HDMI);
    graphics_set_buffer(SCREEN[0]);
    graphics_set_res(SCREEN_W, SCREEN_H);
    graphics_set_shift(32, 0);
    graphics_set_mode(GRAPHICSMODE_DEFAULT);
    ui_init_palette();
    core1_ready = true;
    while (true) {
        core1_heartbeat++;
        sleep_ms(100);
    }
}

//=============================================================================
// Boot
//=============================================================================

int main(void) {
    // HDMI TMDS timing driver was tuned around 252 MHz sys_clk. 251.75 MHz
    // gives a mathematically-perfect 25.175 MHz VGA pixel clock but the PLL
    // may not lock cleanly to that fractional target on every board/chip
    // rev, which manifests as the monitor refusing the sync after a second
    // or two. 252 MHz is the empirically-proven sys_clk on this HDMI driver.
    set_sys_clock_khz(252000, true);

    // LED first - visible heartbeat before any other init can stall.
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);

    stdio_init_all();

    // Re-init UART at 9600 baud (the adapter in use is 9600 8-N-1). At
    // sys_clk = 251.75 MHz the Pico SDK's default UART divisor is also
    // off-spec, so we re-run uart_init with the explicit target rate so
    // the divisor is recomputed from the current clk_peri.
#ifdef USB_HID_ENABLED
    uart_init(uart_default, 9600);
    gpio_set_function(PICO_DEFAULT_UART_TX_PIN, UART_FUNCSEL_NUM(uart_default, PICO_DEFAULT_UART_TX_PIN));
    gpio_set_function(PICO_DEFAULT_UART_RX_PIN, UART_FUNCSEL_NUM(uart_default, PICO_DEFAULT_UART_RX_PIN));
    uart_set_baudrate(uart_default, 9600);
    uart_set_format(uart_default, 8, 1, UART_PARITY_NONE);
#endif

    // Bring HDMI up on Core 0 (single-core mode for this diagnostic).
    graphics_init(g_out_HDMI);
    graphics_set_buffer(SCREEN[0]);
    graphics_set_res(SCREEN_W, SCREEN_H);
    graphics_set_shift(32, 0);
    graphics_set_mode(GRAPHICSMODE_DEFAULT);
    ui_init_palette();
    core1_ready = true;
    (void)core1_entry;   // keep symbol, unused in this build
    (void)core1_heartbeat;

    // Paint an initial frame before we block on CDC enumeration so the TV
    // shows something immediately. We reuse SCENE_WAITING here - the status
    // line is "PLUG IN A USB GAMEPAD" which is accurate in both build flavors.
    g_scene.kind = SCENE_WAITING;
    g_scene.dev = NULL;
    render_scene();

#ifndef USB_HID_ENABLED
    // Debug build: USB CDC is the stdio sink. Wait up to 10 s for the host
    // terminal to open the port, blinking the LED so we can tell the chip
    // is alive. Bail out after the timeout so the UI still runs even if no
    // host ever attaches.
    for (int i = 0; i < 1000; i++) {
        if (stdio_usb_connected()) break;
        gpio_put(PICO_DEFAULT_LED_PIN, (i & 4) ? 1 : 0);
        sleep_ms(10);
    }
    if (stdio_usb_connected()) sleep_ms(1500);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
#else
    // Release build: stdio goes to UART0 (GPIO0 TX, GPIO1 RX @ 115200 baud).
    // Give the operator's terminal a moment to attach before we start
    // printing (mirrors the 1.5 s delay used by frank-wolf). Without this
    // the boot log is clipped or the UART FIFO stalls under back-pressure.
    for (int i = 0; i < 3; i++) sleep_ms(500);
#endif

    printf("\n\n");
    printf("========================================\n");
    printf("   frank-gamepad capture tool\n");
    printf("========================================\n");
    printf("System clock: %lu MHz\n", clock_get_hz(clk_sys) / 1000000);
#ifdef USB_HID_ENABLED
    printf("USB HID host: ENABLED (gamepad capture active)\n");
#else
    printf("USB HID host: DISABLED (debug build, CDC stdio only)\n");
    printf("CDC connected: %d\n", stdio_usb_connected() ? 1 : 0);
#endif
    printf("HDMI initialized (%dx%d)\n", SCREEN_W, SCREEN_H);

    printf("Mounting SD card...\n");
    FRESULT r = f_mount(&g_fs, "", 1);
    if (r != FR_OK) {
        printf("  f_mount failed: %d (continuing without SD)\n", r);
    } else {
        printf("  SD card mounted\n");
    }

    printf("Initializing USB HID host...\n");
    usbhid_init();
    printf("  USB HID host ready\n");

    printf("Entering main capture loop\n");
    while (true) {
        run_session();
    }
}

static void main_unused(void) {
    // Keep stepped diagnostic code accessible if we need it again.
    usbhid_device_t dev;
    while (!(usbhid_gamepad_connected() && usbhid_get_device(&dev) && dev.report_len > 0)) {
        usbhid_task();
        sleep_ms(16);
    }
    printf("[step] A: device present, HDMI? (pause 3s)\n");
    sleep_ms(3000);

    // Step B: render SCENE_BASELINE once.
    g_scene.kind = SCENE_BASELINE;
    g_scene.dev = &dev;
    render_scene();
    printf("[step] B: rendered BASELINE scene, HDMI? (pause 3s)\n");
    sleep_ms(3000);

    // Step C: run capture_baseline.
    uint8_t baseline[USBHID_MAX_REPORT_LEN];
    int baseline_len = capture_baseline(&dev, baseline, 3000);
    printf("[step] C: capture_baseline returned len=%d, HDMI? (pause 3s)\n", baseline_len);
    sleep_ms(3000);

    // Step D: render SCENE_PROMPT.
    g_scene.kind = SCENE_PROMPT;
    g_scene.prompt_btn = UI_BTN_UP;
    g_scene.prompt_idx = 1;
    g_scene.prompt_total = 12;
    render_scene();
    printf("[step] D: rendered PROMPT scene, HDMI? (pause 5s)\n");
    sleep_ms(5000);

    // Step E: call capture_one_button exactly like run_session would.
    printf("[step] E: entering capture_one_button (waiting for UP press)\n");
    button_capture_t cap;
    bool ok = capture_one_button(&dev, baseline, baseline_len, &cap);
    printf("[step] E done: ok=%d, HDMI?\n", (int)ok);
    while (true) {
        usbhid_task();
        sleep_ms(16);
    }
}
