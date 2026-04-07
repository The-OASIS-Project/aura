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

#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include "config.h"

#ifdef ENABLE_MQTT
#include <ArduinoMqttClient.h>

#if defined(MQTT_USE_TLS)
#include <WiFiClientSecure.h>
extern WiFiClientSecure mqttWifiClient;
#endif

// Declare the global MQTT client
extern MqttClient mqttClient;
extern const char topic[];
#endif

// Function prototypes
void setupNetworking(const char* ssid, const char* pass, int retry_attempts, WiFiClient* wifiClient, Adafruit_NeoPixel* pixels);
void monitorConnection(WiFiClient* wifiClient, Adafruit_NeoPixel* pixels);

#ifdef ENABLE_MQTT
void setupMQTT(WiFiClient* wifiClient, Adafruit_NeoPixel* pixels);
void onMqttMessageReceived(int messageSize);  // MQTT message callback
#endif

#ifdef ENABLE_SOCKET
void setupSocket(WiFiClient* wifiClient, Adafruit_NeoPixel* pixels);
bool reconnectSocket(WiFiClient* wifiClient, Adafruit_NeoPixel* pixels);
#endif

void updateWiFiDisplayData(WiFiClient* wifiClient, Adafruit_NeoPixel* pixels);

#endif  // NETWORK_UTILS_H
