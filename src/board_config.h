/*
 * frank-gamepad - Board pin configuration (M2 only)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "hardware/vreg.h"

/*
 * M2 GPIO Layout:
 *   HDMI: CLKN=12, CLKP=13, D0N=14, D0P=15, D1N=16, D1P=17, D2N=18, D2P=19
 *   SD:   CLK=6, CMD=7, DAT0=4, DAT3=5
 *   (USB HID uses the RP2350 native USB port - no GPIOs required)
 */

#ifndef CPU_CLOCK_MHZ
#define CPU_CLOCK_MHZ 252
#endif

#ifndef CPU_VOLTAGE
#define CPU_VOLTAGE VREG_VOLTAGE_1_50
#endif

// HDMI Pins (M2)
#define HDMI_PIN_CLKN 12
#define HDMI_PIN_CLKP 13
#define HDMI_PIN_D0N  14
#define HDMI_PIN_D0P  15
#define HDMI_PIN_D1N  16
#define HDMI_PIN_D1P  17
#define HDMI_PIN_D2N  18
#define HDMI_PIN_D2P  19

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins (M2)
#define SDCARD_PIN_CLK 6
#define SDCARD_PIN_CMD 7
#define SDCARD_PIN_D0  4
#define SDCARD_PIN_D3  5

#endif // BOARD_CONFIG_H
