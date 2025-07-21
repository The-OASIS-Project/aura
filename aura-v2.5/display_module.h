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

#ifndef DISPLAY_MODULE_H
#define DISPLAY_MODULE_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include "config.h"

// Maximum number of ESP-Now peers to display
// If this is changed, other display elements must change too.
#define MAX_DISPLAY_PEERS 4

// Enum for display pages
typedef enum {
  PAGE_IMU = 0,
  PAGE_GPS,
  PAGE_ENVIRO,
#ifdef WIFI_MODE
  PAGE_WIFI,
#endif
#ifdef ESPNOW_MODE
  PAGE_ESPNOW,
#endif
  PAGE_COUNT  // Always keep this as the last item
} display_page_t;

// Function prototypes
void setupDisplay(void);
void displayTask(void *pvParameters);
void updateDisplayData(void);
void displayIMUPage(void);
void displayGPSPage(void);
void displayEnviroPage(void);
void displayWiFiPage(void);
void resetPageDrawFlags(void);

#ifdef ESP32_S3_REVERSE_TFT
// Button handling for manual page control
void setupPageButtons(void);
void prevPageButtonHandler(void);
void nextPageButtonHandler(void);
#endif

// Struct for holding sensor data for display
typedef struct {
  // IMU data
  float roll;
  float pitch;
  float heading;
  bool imu_available;

  // GPS data
  char time[12];
  char date[14];
  int fix;
  float latitude;
  float longitude;
  int speed;
  int altitude;
  int satellites;
  bool gps_available;

  // Environmental data
  float temperature;
  float humidity;
  float air_quality;
  char air_quality_description[15];
  bool temp_available;
  bool humidity_available;
  bool air_quality_available;

  // ENS160 specific data
  int eco2;  // Equivalent CO2 in ppm
  int tvoc;  // Total Volatile Organic Compounds in ppb
  bool ens160_available;

  // SCD41 CO2 sensor data
  uint16_t co2;  // CO2 concentration in ppm
  bool co2_available;
  char co2_quality_description[15];

#ifdef WIFI_MODE
  // WiFi data
  char ssid[33];
  int rssi;
  char ip_address[16];
  bool is_connected;
#endif

#ifdef ESPNOW_MODE
  // ESP-Now data
  size_t espnow_peer_count;
  char espnow_peers[MAX_DISPLAY_PEERS][32];  // Array of peer topics
  uint32_t espnow_packets_received[MAX_DISPLAY_PEERS]; // Packets received
  uint32_t espnow_packets_missed[MAX_DISPLAY_PEERS];   // Packets missed
#endif
} display_data_t;

// Global variables to be exposed
extern SemaphoreHandle_t displayMutex;
extern display_data_t display_data;
extern int current_page;

// First-draw flags
extern bool imu_page_first_draw;
extern bool gps_page_first_draw;
extern bool enviro_page_first_draw;
extern bool wifi_page_first_draw;

#endif  // DISPLAY_MODULE_H
