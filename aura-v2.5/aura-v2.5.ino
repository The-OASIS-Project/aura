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

// I2C
#include <Wire.h>

// Common Includes
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "arduino_secrets.h"

// Logger
#include "logger.h"

// Component Headers
#include "espnow_module.h"
#include "gps_module.h"
#include "imu_module.h"
#include "enviro_module.h"
#include "network_utils.h"
#include "display_module.h"
#include "faceplate_module.h"
#include "command_processor.h"

// NeoPixel
#include <Adafruit_NeoPixel.h>

// NeoPixel
// PIN_NEOPIXEL and NEOPIXEL_POWER are defined
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// WiFi
#define CONN_RETRY_ATTEMPTS 10

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;  // your network SSID (name)
char pass[] = SECRET_PASS;  // your network password (use for WPA, or use as key for WEP)

WiFiClient wifiClient;

// Task handles
TaskHandle_t gpsTaskHandle;
TaskHandle_t imuTaskHandle;
TaskHandle_t enviroTaskHandle;
TaskHandle_t displayTaskHandle;
TaskHandle_t messagingTaskHandle;
TaskHandle_t servoTaskHandle;
TaskHandle_t espnowTaskHandle;

void setup() {
  unsigned long startTime = millis();

  Serial.begin(115200);
  while (!Serial && (millis() - startTime < 2000)) {  // Wait up to 2 seconds for Serial to connect
    delay(10);
  }

  // Initialize thread-safe logging
  logger_init();

  LOG_PRINTLN("AURA Initialized. Beginning power on sequence...");

  // NeoPixel
  pixels.begin();
  pixels.setBrightness(64);  // Set to 25% brightness
  pixels.clear();

  // Initialize I2C with specified pins and speed
  if (!Wire.begin(SDA_PIN, SCL_PIN, 350000)) {
    LOG_PRINTLN("I2C initialization failed!");
  } else {
    LOG_PRINTLN("I2C initialization success!");
  }

  // Initialize the display
  setupDisplay();

  // Initialize display data availability flags before sensor initialization
  if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    display_data.imu_available = false;
    display_data.gps_available = false;
    display_data.temp_available = false;
    display_data.humidity_available = false;
    display_data.air_quality_available = false;
    display_data.ens160_available = false;
    // display_data.co2_available is already initialized in enviro_module.cpp
    xSemaphoreGive(displayMutex);
  }

  // Initialize components
  setupFaceplate();
  setupIMU();
  setupGPS();
  setupEnvironmental();

  // Clear any garbage in serial buffer
  while (Serial.available()) {
    Serial.read();
  }
  delay(100);

  setupSerialCommands();

  pixels.setPixelColor(0, pixels.Color(255, 0, 0));
  pixels.show();

  // Initialize communication based on selected mode
#ifdef WIFI_MODE
  // Initialize networking
  setupNetworking(ssid, pass, CONN_RETRY_ATTEMPTS, &wifiClient, &pixels);
#endif

#ifdef ESPNOW_MODE
  // Initialize ESP-Now
  // First set WiFi mode to STA (no connection)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Configure ESP-Now
  setupESPNow(&pixels);
#endif

  // Create tasks
  if (xTaskCreatePinnedToCore(
        gpsTask,         // Task function
        "GPS Task",      // Name of the task
        10000,           // Stack size (in words)
        NULL,            // Task input parameter
        1,               // Priority of the task
        &gpsTaskHandle,  // Task handle
        1                // Core 1
        )
      != pdPASS) {
    LOG_PRINTLN("Failed to create GPS Task");
  }

  if (xTaskCreatePinnedToCore(
        imuTask,         // Task function
        "IMU Task",      // Name of the task
        10000,           // Stack size (in words)
        NULL,            // Task input parameter
        2,               // Priority of the task
        &imuTaskHandle,  // Task handle
        0                // Core 0
        )
      != pdPASS) {
    LOG_PRINTLN("Failed to create IMU Task");
  }

  if (xTaskCreatePinnedToCore(
        enviroTask,            // Task function
        "Environmental Task",  // Name of the task
        10000,                 // Stack size (in words)
        NULL,                  // Task input parameter
        1,                     // Priority of the task
        &enviroTaskHandle,     // Task handle
        1                      // Core 1
        )
      != pdPASS) {
    LOG_PRINTLN("Failed to create Environmental Task");
  }

  if (xTaskCreatePinnedToCore(
        displayTask,         // Task function
        "Display Task",      // Name of the task
        10000,               // Stack size (in words)
        NULL,                // Task input parameter
        1,                   // Priority of the task
        &displayTaskHandle,  // Task handle
        0                    // Core 0
        )
      != pdPASS) {
    LOG_PRINTLN("Failed to create Display Task");
  }

  if (xTaskCreatePinnedToCore(
        messagingTask,         // Task function
        "Messaging Task",      // Name of the task
        10000,                 // Stack size (in words)
        NULL,                  // Task input parameter
        1,                     // Priority of the task
        &messagingTaskHandle,  // Task handle
        1                      // Core 1
        )
      != pdPASS) {
    LOG_PRINTLN("Failed to create Network Task");
  }

  if (xTaskCreatePinnedToCore(
        servoTask,         // Task function
        "Servo Task",      // Name of the task
        10000,             // Stack size (in words)
        NULL,              // Task input parameter
        1,                 // Priority of the task
        &servoTaskHandle,  // Task handle
        0                  // Core 0
        )
      != pdPASS) {
    LOG_PRINTLN("Failed to create Servo Task");
  }

#ifdef ESPNOW_MODE
  if (xTaskCreatePinnedToCore(
        espnowTask,         // Task function
        "ESP-Now Task",     // Name of the task
        10000,              // Stack size (in words)
        NULL,               // Task input parameter
        1,                  // Priority of the task
        &espnowTaskHandle,  // Task handle
        1                   // Core 1
        )
      != pdPASS) {
    LOG_PRINTLN("Failed to create ESP-Now Task");
  }
#endif
}

void loop() {
  // Network monitoring based on mode
#ifdef WIFI_MODE
  // Monitor the network connection
  monitorConnection(&wifiClient, &pixels);
#endif

#ifdef ESPNOW_MODE
  // Monitor ESP-Now connections
  monitorESPNowPeers(&pixels);
#endif

  // Check for serial commands
  checkSerialCommands();

  // Use a small delay to prevent tight loop
  vTaskDelay(pdMS_TO_TICKS(100));
}
