/*
 * frank-gamepad - UI rendering primitives + SNES controller schematic.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui.h"
#include "HDMI.h"
#include <string.h>

void ui_init_palette(void) {
    graphics_set_palette(UI_COLOR_BG,        0x101828);   // deep navy background
    graphics_set_palette(UI_COLOR_TEXT,      0xFFFFFF);
    graphics_set_palette(UI_COLOR_DIM,       0x808A9C);
    graphics_set_palette(UI_COLOR_HIGHLIGHT, 0xFFD23F);   // amber
    graphics_set_palette(UI_COLOR_PRESSED,   0xE63946);   // red
    graphics_set_palette(UI_COLOR_OK,        0x4ADE80);   // green
    graphics_set_palette(0,                  0x101828);   // keep index 0 safe
    graphics_restore_sync_colors();
}

// ---- 5x7 font (uppercase + digits + a handful of punctuation) -----

static const uint8_t GLYPH_SPACE[7] = {0,0,0,0,0,0,0};
static const uint8_t GLYPH_DOT[7]   = {0,0,0,0,0,0x0C,0x0C};
static const uint8_t GLYPH_COMMA[7] = {0,0,0,0,0,0x06,0x04};
static const uint8_t GLYPH_HYPHEN[7]= {0,0,0,0x1F,0,0,0};
static const uint8_t GLYPH_UNDER[7] = {0,0,0,0,0,0,0x1F};
static const uint8_t GLYPH_COLON[7] = {0,0x0C,0x0C,0,0x0C,0x0C,0};
static const uint8_t GLYPH_SLASH[7] = {0x01,0x02,0x04,0x08,0x10,0,0};
static const uint8_t GLYPH_LPAREN[7]= {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
static const uint8_t GLYPH_RPAREN[7]= {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
static const uint8_t GLYPH_EQUAL[7] = {0,0,0x1F,0,0x1F,0,0};
static const uint8_t GLYPH_PLUS[7]  = {0,0x04,0x04,0x1F,0x04,0x04,0};
static const uint8_t GLYPH_QUEST[7] = {0x0E,0x11,0x01,0x02,0x04,0,0x04};
static const uint8_t GLYPH_EXCL[7]  = {0x04,0x04,0x04,0x04,0x04,0,0x04};
static const uint8_t GLYPH_HASH[7]  = {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A};
static const uint8_t GLYPH_ARROW_R[7]={0,0x04,0x06,0x1F,0x06,0x04,0};

static const uint8_t GLYPH_0[7]={0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
static const uint8_t GLYPH_1[7]={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
static const uint8_t GLYPH_2[7]={0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
static const uint8_t GLYPH_3[7]={0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};
static const uint8_t GLYPH_4[7]={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
static const uint8_t GLYPH_5[7]={0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E};
static const uint8_t GLYPH_6[7]={0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E};
static const uint8_t GLYPH_7[7]={0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
static const uint8_t GLYPH_8[7]={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
static const uint8_t GLYPH_9[7]={0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E};

static const uint8_t GLYPH_A[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
static const uint8_t GLYPH_B[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
static const uint8_t GLYPH_C[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E};
static const uint8_t GLYPH_D[7]={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
static const uint8_t GLYPH_E[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
static const uint8_t GLYPH_F[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
static const uint8_t GLYPH_G[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0E};
static const uint8_t GLYPH_H[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
static const uint8_t GLYPH_I[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x1F};
static const uint8_t GLYPH_J[7]={0x07,0x02,0x02,0x02,0x12,0x12,0x0C};
static const uint8_t GLYPH_K[7]={0x11,0x12,0x14,0x18,0x14,0x12,0x11};
static const uint8_t GLYPH_L[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
static const uint8_t GLYPH_M[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
static const uint8_t GLYPH_N[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11};
static const uint8_t GLYPH_O[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
static const uint8_t GLYPH_P[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
static const uint8_t GLYPH_Q[7]={0x0E,0x11,0x11,0x11,0x15,0x12,0x0D};
static const uint8_t GLYPH_R[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
static const uint8_t GLYPH_S[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
static const uint8_t GLYPH_T[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
static const uint8_t GLYPH_U[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
static const uint8_t GLYPH_V[7]={0x11,0x11,0x11,0x11,0x0A,0x0A,0x04};
static const uint8_t GLYPH_W[7]={0x11,0x11,0x11,0x15,0x15,0x15,0x0A};
static const uint8_t GLYPH_X[7]={0x11,0x0A,0x04,0x04,0x04,0x0A,0x11};
static const uint8_t GLYPH_Y[7]={0x11,0x0A,0x04,0x04,0x04,0x04,0x04};
static const uint8_t GLYPH_Z[7]={0x1F,0x02,0x04,0x08,0x10,0x10,0x1F};

static const uint8_t *glyph(char ch) {
    int c = (unsigned char)ch;
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    switch (c) {
        case ' ': return GLYPH_SPACE;
        case '.': return GLYPH_DOT;
        case ',': return GLYPH_COMMA;
        case '-': return GLYPH_HYPHEN;
        case '_': return GLYPH_UNDER;
        case ':': return GLYPH_COLON;
        case '/': return GLYPH_SLASH;
        case '(': return GLYPH_LPAREN;
        case ')': return GLYPH_RPAREN;
        case '=': return GLYPH_EQUAL;
        case '+': return GLYPH_PLUS;
        case '?': return GLYPH_QUEST;
        case '!': return GLYPH_EXCL;
        case '#': return GLYPH_HASH;
        case '>': return GLYPH_ARROW_R;
        case '0': return GLYPH_0; case '1': return GLYPH_1;
        case '2': return GLYPH_2; case '3': return GLYPH_3;
        case '4': return GLYPH_4; case '5': return GLYPH_5;
        case '6': return GLYPH_6; case '7': return GLYPH_7;
        case '8': return GLYPH_8; case '9': return GLYPH_9;
        case 'A': return GLYPH_A; case 'B': return GLYPH_B;
        case 'C': return GLYPH_C; case 'D': return GLYPH_D;
        case 'E': return GLYPH_E; case 'F': return GLYPH_F;
        case 'G': return GLYPH_G; case 'H': return GLYPH_H;
        case 'I': return GLYPH_I; case 'J': return GLYPH_J;
        case 'K': return GLYPH_K; case 'L': return GLYPH_L;
        case 'M': return GLYPH_M; case 'N': return GLYPH_N;
        case 'O': return GLYPH_O; case 'P': return GLYPH_P;
        case 'Q': return GLYPH_Q; case 'R': return GLYPH_R;
        case 'S': return GLYPH_S; case 'T': return GLYPH_T;
        case 'U': return GLYPH_U; case 'V': return GLYPH_V;
        case 'W': return GLYPH_W; case 'X': return GLYPH_X;
        case 'Y': return GLYPH_Y; case 'Z': return GLYPH_Z;
        default:  return GLYPH_SPACE;
    }
}

// ---- primitives -------------------------------------------------

void ui_clear(uint8_t *screen, uint8_t color) {
    memset(screen, color, UI_SCREEN_WIDTH * UI_SCREEN_HEIGHT);
}

void ui_fill_rect(uint8_t *screen, int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > UI_SCREEN_WIDTH)  w = UI_SCREEN_WIDTH - x;
    if (y + h > UI_SCREEN_HEIGHT) h = UI_SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++) {
        memset(&screen[yy * UI_SCREEN_WIDTH + x], color, (size_t)w);
    }
}

void ui_draw_rect(uint8_t *screen, int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    ui_fill_rect(screen, x, y, w, 1, color);
    ui_fill_rect(screen, x, y + h - 1, w, 1, color);
    ui_fill_rect(screen, x, y, 1, h, color);
    ui_fill_rect(screen, x + w - 1, y, 1, h, color);
}

void ui_draw_char(uint8_t *screen, int x, int y, char ch, uint8_t color) {
    const uint8_t *rows = glyph(ch);
    for (int row = 0; row < UI_FONT_H; row++) {
        int yy = y + row;
        if (yy < 0 || yy >= UI_SCREEN_HEIGHT) continue;
        uint8_t bits = rows[row];
        for (int col = 0; col < 5; col++) {
            int xx = x + col;
            if (xx < 0 || xx >= UI_SCREEN_WIDTH) continue;
            if (bits & (1u << (4 - col))) {
                screen[yy * UI_SCREEN_WIDTH + xx] = color;
            }
        }
    }
}

void ui_draw_text(uint8_t *screen, int x, int y, const char *text, uint8_t color) {
    for (const char *p = text; *p; p++) {
        ui_draw_char(screen, x, y, *p, color);
        x += UI_FONT_W;
    }
}

int ui_text_width(const char *text) {
    int n = 0;
    for (const char *p = text; *p; p++) n++;
    return n * UI_FONT_W;
}

void ui_draw_text_centered(uint8_t *screen, int y, const char *text, uint8_t color) {
    int w = ui_text_width(text);
    int x = (UI_SCREEN_WIDTH - w) / 2;
    if (x < 0) x = 0;
    ui_draw_text(screen, x, y, text, color);
}

// ---- SNES controller schematic ----------------------------------
//
// Screen layout (256x224):
//
//   [Title text]
//   [       Gamepad silhouette (220 x 100)       ]
//   [Prompt: "Press <BUTTON>"]
//   [Device info line]

// Geometry — the "body" of the pad.
#define PAD_X      18
#define PAD_Y      72
#define PAD_W      220
#define PAD_H      100

// D-pad (left cluster)
#define DPAD_CX    (PAD_X + 38)
#define DPAD_CY    (PAD_Y + 54)
#define DPAD_ARM_W 10   // thickness of each arm
#define DPAD_ARM_L 14   // length of each arm from center

// Right face buttons (A B X Y)
//    Y       X          Y up, A right, B down, X left (SNES layout)
//      Y  X
//      B  A
//      and in classic SNES: X top, A right, B bottom, Y left.
// We'll use SNES canonical: X=top, A=right, B=bottom, Y=left.
#define FACE_CX    (PAD_X + PAD_W - 40)
#define FACE_CY    (PAD_Y + 54)
#define FACE_R     8
#define FACE_OFF   14   // radial offset from centre of cluster

// Start / Select (center)
#define SS_Y       (PAD_Y + 60)
#define SS_W       22
#define SS_H       6
#define SELECT_X   (PAD_X + 78)
#define START_X    (PAD_X + 120)

// Shoulders (top corners)
#define SHO_Y      (PAD_Y - 6)
#define SHO_H      10
#define L_X        (PAD_X + 6)
#define R_X        (PAD_X + PAD_W - 6 - 34)
#define SHO_W      34

static void draw_circle(uint8_t *screen, int cx, int cy, int r, uint8_t color, bool filled) {
    int r2 = r * r;
    int r2_in = (r - 1) * (r - 1);
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d = dx*dx + dy*dy;
            if (d <= r2) {
                if (filled || d >= r2_in) {
                    int x = cx + dx, y = cy + dy;
                    if (x >= 0 && x < UI_SCREEN_WIDTH && y >= 0 && y < UI_SCREEN_HEIGHT) {
                        screen[y * UI_SCREEN_WIDTH + x] = color;
                    }
                }
            }
        }
    }
}

// Get geometry for each logical button as a bounding rect.
// Used by highlight pass to redraw in a different color.
static void button_rect(ui_button_t btn, int *x, int *y, int *w, int *h, int *is_circle, int *cx, int *cy, int *r) {
    *is_circle = 0;
    *cx = *cy = *r = 0;
    switch (btn) {
        case UI_BTN_UP:
            *x = DPAD_CX - DPAD_ARM_W/2;
            *y = DPAD_CY - DPAD_ARM_L - DPAD_ARM_W/2;
            *w = DPAD_ARM_W;
            *h = DPAD_ARM_L;
            break;
        case UI_BTN_DOWN:
            *x = DPAD_CX - DPAD_ARM_W/2;
            *y = DPAD_CY + DPAD_ARM_W/2;
            *w = DPAD_ARM_W;
            *h = DPAD_ARM_L;
            break;
        case UI_BTN_LEFT:
            *x = DPAD_CX - DPAD_ARM_L - DPAD_ARM_W/2;
            *y = DPAD_CY - DPAD_ARM_W/2;
            *w = DPAD_ARM_L;
            *h = DPAD_ARM_W;
            break;
        case UI_BTN_RIGHT:
            *x = DPAD_CX + DPAD_ARM_W/2;
            *y = DPAD_CY - DPAD_ARM_W/2;
            *w = DPAD_ARM_L;
            *h = DPAD_ARM_W;
            break;
        case UI_BTN_SELECT:
            *x = SELECT_X; *y = SS_Y; *w = SS_W; *h = SS_H;
            break;
        case UI_BTN_START:
            *x = START_X; *y = SS_Y; *w = SS_W; *h = SS_H;
            break;
        case UI_BTN_X:
            *is_circle = 1; *cx = FACE_CX;          *cy = FACE_CY - FACE_OFF; *r = FACE_R; break;
        case UI_BTN_A:
            *is_circle = 1; *cx = FACE_CX + FACE_OFF;*cy = FACE_CY;            *r = FACE_R; break;
        case UI_BTN_B:
            *is_circle = 1; *cx = FACE_CX;          *cy = FACE_CY + FACE_OFF; *r = FACE_R; break;
        case UI_BTN_Y:
            *is_circle = 1; *cx = FACE_CX - FACE_OFF;*cy = FACE_CY;            *r = FACE_R; break;
        case UI_BTN_L:
            *x = L_X; *y = SHO_Y; *w = SHO_W; *h = SHO_H;
            break;
        case UI_BTN_R:
            *x = R_X; *y = SHO_Y; *w = SHO_W; *h = SHO_H;
            break;
        default:
            *x = *y = *w = *h = 0;
            break;
    }
}

static void draw_button(uint8_t *screen, ui_button_t btn, uint8_t color_fill, uint8_t color_outline) {
    int x, y, w, h, is_circle, cx, cy, r;
    button_rect(btn, &x, &y, &w, &h, &is_circle, &cx, &cy, &r);
    if (is_circle) {
        draw_circle(screen, cx, cy, r, color_fill, true);
        draw_circle(screen, cx, cy, r, color_outline, false);
        // Letter label inside circle.
        const char *lbl = ui_button_label(btn);
        int lx = cx - 2;
        int ly = cy - 3;
        ui_draw_char(screen, lx, ly, lbl[0], UI_COLOR_BG);
    } else if (w > 0 && h > 0) {
        ui_fill_rect(screen, x, y, w, h, color_fill);
        ui_draw_rect(screen, x, y, w, h, color_outline);
    }
}

const char *ui_button_label(ui_button_t btn) {
    switch (btn) {
        case UI_BTN_UP:     return "UP";
        case UI_BTN_DOWN:   return "DOWN";
        case UI_BTN_LEFT:   return "LEFT";
        case UI_BTN_RIGHT:  return "RIGHT";
        case UI_BTN_SELECT: return "SELECT";
        case UI_BTN_START:  return "START";
        case UI_BTN_Y:      return "Y";
        case UI_BTN_B:      return "B";
        case UI_BTN_A:      return "A";
        case UI_BTN_X:      return "X";
        case UI_BTN_L:      return "L";
        case UI_BTN_R:      return "R";
        default:            return "?";
    }
}

void ui_draw_schematic(uint8_t *screen) {
    // Pad body: rounded-ish rectangle (just a rectangle with corners chipped).
    ui_fill_rect(screen, PAD_X + 4, PAD_Y, PAD_W - 8, PAD_H, UI_COLOR_DIM);
    ui_fill_rect(screen, PAD_X, PAD_Y + 6, PAD_W, PAD_H - 12, UI_COLOR_DIM);
    // Inner highlight
    ui_fill_rect(screen, PAD_X + 6, PAD_Y + 3, PAD_W - 12, PAD_H - 6, UI_COLOR_BG);

    // Shoulder buttons
    for (ui_button_t b = UI_BTN_L; b <= UI_BTN_R; b++) {
        draw_button(screen, b, UI_COLOR_DIM, UI_COLOR_TEXT);
        int x, y, w, h, ic, cx, cy, r;
        button_rect(b, &x, &y, &w, &h, &ic, &cx, &cy, &r);
        const char *lbl = ui_button_label(b);
        int tx = x + (w - ui_text_width(lbl)) / 2;
        int ty = y + (h - UI_FONT_H) / 2;
        ui_draw_text(screen, tx, ty, lbl, UI_COLOR_TEXT);
    }

    // D-pad arms
    for (ui_button_t b = UI_BTN_UP; b <= UI_BTN_RIGHT; b++) {
        draw_button(screen, b, UI_COLOR_DIM, UI_COLOR_TEXT);
    }
    // D-pad centre hub
    ui_fill_rect(screen, DPAD_CX - DPAD_ARM_W/2, DPAD_CY - DPAD_ARM_W/2,
                 DPAD_ARM_W, DPAD_ARM_W, UI_COLOR_DIM);

    // Select / Start — centre each label under its slot.
    draw_button(screen, UI_BTN_SELECT, UI_COLOR_DIM, UI_COLOR_TEXT);
    draw_button(screen, UI_BTN_START,  UI_COLOR_DIM, UI_COLOR_TEXT);
    {
        const char *sel = "SELECT";
        const char *st  = "START";
        int sx = SELECT_X + (SS_W - ui_text_width(sel)) / 2;
        int tx = START_X  + (SS_W - ui_text_width(st))  / 2;
        ui_draw_text(screen, sx, SS_Y + SS_H + 2, sel, UI_COLOR_TEXT);
        ui_draw_text(screen, tx, SS_Y + SS_H + 2, st,  UI_COLOR_TEXT);
    }

    // Face buttons (X top, A right, B bottom, Y left)
    draw_button(screen, UI_BTN_X, UI_COLOR_DIM, UI_COLOR_TEXT);
    draw_button(screen, UI_BTN_A, UI_COLOR_DIM, UI_COLOR_TEXT);
    draw_button(screen, UI_BTN_B, UI_COLOR_DIM, UI_COLOR_TEXT);
    draw_button(screen, UI_BTN_Y, UI_COLOR_DIM, UI_COLOR_TEXT);
}

void ui_highlight_button(uint8_t *screen, ui_button_t btn, uint8_t color) {
    draw_button(screen, btn, color, UI_COLOR_TEXT);
    // Re-stamp shoulder label (draw_button handles circle labels internally).
    if (btn == UI_BTN_L || btn == UI_BTN_R) {
        int x, y, w, h, ic, cx, cy, r;
        button_rect(btn, &x, &y, &w, &h, &ic, &cx, &cy, &r);
        const char *lbl = ui_button_label(btn);
        int tx = x + (w - ui_text_width(lbl)) / 2;
        int ty = y + (h - UI_FONT_H) / 2;
        ui_draw_text(screen, tx, ty, lbl, UI_COLOR_BG);
    }
}
