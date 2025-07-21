/*
 * This file is part of the OASIS Project.
 * https://github.com/orgs/The-OASIS-Project/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * By contributing to this project, you agree to license your contributions
 * under the GPLv3 (or any later version) or any future licenses chosen by
 * the project author(s). Contributions include any modifications,
 * enhancements, or additions to the project. These contributions become
 * part of the project and are adopted by the project author(s).
 */

#ifndef CONFIG_H
#define CONFIG_H

// =========== FEATURE ENABLE FLAGS ===========

// Enable MQTT support
//#define ENABLE_MQTT

// Enable Socket support (disable if using MQTT)
//#define ENABLE_SOCKET

// Enable Adafruit ESP32-S3 Reverse TFT Feather support
#define ESP32_S3_REVERSE_TFT

// =========== NETWORK CONFIGURATION ===========

// MQTT settings (when ENABLE_MQTT is defined)
#define MQTT_PORT 1883
#define MQTT_TOPIC "helmet"

// Socket settings (when ENABLE_SOCKET is defined)
#define SERVER_PORT 3000
#define SOCKET_RECONNECT_TIMEOUT_MS 5000

// =========== COMMUNICATION MODE ===========
// Choose either WIFI_MODE or ESPNOW_MODE (not both)
//#define WIFI_MODE    // Use WiFi with MQTT/Socket
#define ESPNOW_MODE  // Use ESP-Now communication

// =========== ESP-NOW CONFIGURATION ===========

// ESP-Now channel (1-14)
#define ESPNOW_CHANNEL 1

// =========== GPS CONFIGURATION ===========

// Choose ONE GPS interface method
#define GPS_USE_UART   // Use UART interface for GPS
//#define GPS_USE_I2C  // Use I2C interface for GPS

// GPS UART settings
#ifdef ESP32_S3_REVERSE_TFT
#define GPS_UART_TX_PIN 39        // TX pin (GPIO39)
#define GPS_UART_RX_PIN 38        // RX pin (GPIO38)
#else
#define GPS_UART_TX_PIN 1         // TX pin (GPIO1)
#define GPS_UART_RX_PIN 2         // RX pin (GPIO2)
#endif
#define GPS_RESET_PIN 5           // Reset pin on D5 (GPIO5)
#define GPS_BAUDRATE 38400        // Factory default u-blox baudrate
#define GPS_HIGH_BAUDRATE 115200  // High performance baudrate
#define GPS_SERIAL Serial1        // Use Serial1 for GPS communications

// GPS I2C settings 
#define GPS_I2C_ADDR 0x42  // Default u-blox I2C address

// =========== DISPLAY CONFIGURATION ===========

// TFT Display pins
#ifdef ESP32_S3_REVERSE_TFT
#define TFT_CS 42
#define TFT_DC 40
#define TFT_RST 41
#define TFT_BL 45
#define TFT_PWR 7
#else
#define TFT_CS 7
#define TFT_DC 39
#define TFT_RST 40
#define TFT_BL 45
#define TFT_PWR 21
#endif

// Display dimensions
#define TFT_WIDTH 240
#define TFT_HEIGHT 135

// Display settings
#define PAGE_CHANGE_INTERVAL 8000  // 8 seconds per page
#define DISPLAY_REFRESH_RATE_MS 500 // Anti-flickering refresh rate

// =========== FACEPLATE CONFIGURATION ===========

// Servo pins
#define SERVO1_PIN 12
#define SERVO2_PIN 13

// LED pins
#define LED1_PIN A4
#define LED2_PIN A5

// Button pins
#define BUTTON_PIN 11

#ifdef ESP32_S3_REVERSE_TFT
// Button pins for manual page control
#define PREV_PAGE_BUTTON_PIN 2  // D2 - Previous page (active HIGH)
#define NEXT_PAGE_BUTTON_PIN 1  // D1 - Next page (active HIGH)
#endif

// Define servo angle limits
#define OPEN_ANGLE_DEFAULT 0
#define CLOSED_ANGLE_DEFAULT 77
#define OPEN_STRUGGLE_BACKOFF 1

// LED configuration
//#define USE_NEOPIXELS // Uncomment to use NeoPixels instead of regular LEDs

// NeoPixel settings
#define NEOPIXEL_BRIGHTNESS 64  // 0-255
#define NEOPIXEL_COLOR_R 255    // Red component
#define NEOPIXEL_COLOR_G 255    // Green component
#define NEOPIXEL_COLOR_B 255    // Blue component

// =========== IMU CONFIGURATION ===========

// BNO086 SPI interface pins
#define BNO086_INT 6    // INT pin on D6 (GPIO6)
#define BNO086_RST 9    // RST pin on D9 (GPIO9)
#define BNO086_WAKE 10  // WAKE pin (PS0) on D10 (GPIO10)

// Build it myself secondary SPI
#define BNO086_SCK A0   // SCK pin on A0 (GPIO18)
#define BNO086_MISO A1  // MISO pin on A1 (GPIO17)
#define BNO086_MOSI A2  // MOSI pin on A2 (GPIO16)
#define BNO086_CS A3    // CS pin on A3 (GPIO15)

// Reset Holds
#define RESET_HOLD 15   // Time to hold reset line (10ms min)
#define RESET_WAIT 150  // Time to wait after reset (90ms + 4ms min)

// BNO086 SPI configuration
#define BNO086_SPI_HIGH_SPEED 2000000  // 2MHz for dedicated SPI bus
#define BNO086_SPI_LOW_SPEED 500000    // 500KHz for dedicated SPI bus

// General Config
#define IMU_TIME_BETWEEN_REPORTS 100   // Time in ms between IMU reports

// =========== ENVIRONMENTAL SENSORS CONFIGURATION ===========

// CO2 thresholds (in ppm)
#define CO2_EXCELLENT 600
#define CO2_GOOD 800
#define CO2_FAIR 1000
#define CO2_POOR 1500

// I2C pins for ESP32-S3 TFT Feather
#ifdef ESP32_S3_REVERSE_TFT
#define SDA_PIN 3
#define SCL_PIN 4
#else
#define SDA_PIN 42
#define SCL_PIN 41
#endif

#endif // CONFIG_H
