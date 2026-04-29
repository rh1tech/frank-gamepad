/*
 * frank-gamepad - UI primitives: text rendering and SNES schematic.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>

#define UI_SCREEN_WIDTH  256
#define UI_SCREEN_HEIGHT 224

// Palette indices (initialized by ui_init_palette)
#define UI_COLOR_BG       1
#define UI_COLOR_TEXT     63
#define UI_COLOR_DIM      42
#define UI_COLOR_OUTLINE  42
#define UI_COLOR_HIGHLIGHT 48   // prompted button (yellow)
#define UI_COLOR_PRESSED  32    // currently-pressed (red)
#define UI_COLOR_OK       56    // success (green)

#define UI_FONT_W 6  // 5px glyph + 1px spacing
#define UI_FONT_H 7
#define UI_LINE_H 10

// Logical SNES buttons captured by the wizard.
typedef enum {
    UI_BTN_UP = 0,
    UI_BTN_DOWN,
    UI_BTN_LEFT,
    UI_BTN_RIGHT,
    UI_BTN_SELECT,
    UI_BTN_START,
    UI_BTN_Y,
    UI_BTN_B,
    UI_BTN_A,
    UI_BTN_X,
    UI_BTN_L,
    UI_BTN_R,
    UI_BTN_COUNT
} ui_button_t;

void ui_init_palette(void);

void ui_clear(uint8_t *screen, uint8_t color);
void ui_fill_rect(uint8_t *screen, int x, int y, int w, int h, uint8_t color);
void ui_draw_rect(uint8_t *screen, int x, int y, int w, int h, uint8_t color);
void ui_draw_char(uint8_t *screen, int x, int y, char ch, uint8_t color);
void ui_draw_text(uint8_t *screen, int x, int y, const char *text, uint8_t color);
void ui_draw_text_centered(uint8_t *screen, int y, const char *text, uint8_t color);
int  ui_text_width(const char *text);

// Draw the static SNES controller schematic.
void ui_draw_schematic(uint8_t *screen);

// Highlight one logical button (e.g. currently prompted).
void ui_highlight_button(uint8_t *screen, ui_button_t btn, uint8_t color);

// Return a short label for a button (e.g. "UP", "START", "A").
const char *ui_button_label(ui_button_t btn);

#endif // UI_H
