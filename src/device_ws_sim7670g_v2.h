// SPDX-License-Identifier: GPL-3.0-or-later
// Waveshare ESP32-S3-A-SIM7670X-4G-V2, populated with SIM7670G.
// Pin names below are from the ESP32's perspective.
#pragma once

#if !defined(TINY_GSM_MODEM_SIM7670G) || defined(TINY_GSM_MODEM_A7670) || defined(TINY_GSM_MODEM_A76XXSSL)
#error "WS_SIM7670G_V2 requires only the TinyGSM SIM7670G driver"
#endif

#define MODEM_BAUDRATE 115200
#define MODEM_TX_PIN 18
#define MODEM_RX_PIN 17
#define MODEM_DTR_PIN 45
#define MODEM_RING_PIN 40
#define BOARD_POWERON_PIN 21
#define MAX17048_I2C_ADDRESS 0x36
#define MAX17048_I2C_SDA 15
#define MAX17048_I2C_SCL 16

// Q9 asserts PWRKEY when VVBAT is powered. RESET has no ESP32 connection.
// Deliberately do not define BOARD_PWRKEY_PIN or MODEM_RESET_PIN.
// GPIO33-37 are reserved for the R8's Octal PSRAM.

// CGNSSINFO format: 0 = decimal degrees, 1 = degrees/minutes,
// 2 = auto (uses degrees/minutes only if decimal coordinates exceed limits).
// See SIM7670G-V2.md before changing this for different modem firmware.
#ifndef WS_GNSS_COORDINATE_FORMAT
#define WS_GNSS_COORDINATE_FORMAT 2
#endif
