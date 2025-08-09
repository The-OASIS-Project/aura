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

#ifndef ESPNOW_MODULE_H
#define ESPNOW_MODULE_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

// Maximum number of ESP-Now peers
#define MAX_ESPNOW_PEERS 20

// ESP-Now message types
#define ESPNOW_MSG_REGISTRATION    1  // Registration request from SPARK
#define ESPNOW_MSG_REG_ACK         2  // Registration acknowledgment
#define ESPNOW_MSG_REG_REJECT      3  // Registration rejection (topic already exists)
#define ESPNOW_MSG_DATA            4  // Data message from SPARK
#define ESPNOW_MSG_DATA_ACK        5  // Data acknowledgment
#define ESPNOW_MSG_PING            6  // Ping to check connectivity
#define ESPNOW_MSG_PONG            7  // Pong response

// NOTE: The system uses regular data messages as implicit heartbeats.
// SPARK devices are expected to send updates every 5 seconds (see sendInterval in spark-generic-v1.ino)
// This eliminates the need for explicit ping/pong heartbeat messages.
// The 30-second timeout is designed to allow for occasional packet loss while still
// detecting actual disconnections.

// ESP-Now message structure
typedef struct {
  uint8_t type;                      // Message type
  uint8_t sequence;                  // Sequence number
  char topic[32];                    // Device topic/identifier
  uint8_t mac[6];                    // Sender MAC address
  uint32_t timestamp;                // Message timestamp (millis)
  uint8_t data_len;                  // Length of data payload
  uint8_t data[200];                 // Data payload (JSON or other)
} espnow_message_t;

// ESP-Now peer information
typedef struct {
  uint8_t mac[6];                    // Peer MAC address
  char topic[32];                    // Peer topic/identifier
  uint32_t last_seen;                // Last seen timestamp
  uint8_t last_seq_received;         // Last sequence number received
  uint32_t packets_received;         // Total packets received
  uint32_t packets_missed;           // Packets missed (sequence gaps)
  bool active;                       // Whether peer is active
} espnow_peer_t;

// Semaphore for thread-safe access to ESP-Now peer list
extern SemaphoreHandle_t espnowMutex;

// Task handle for ESP-Now processing
extern TaskHandle_t espnowTaskHandle;

// Function prototypes
void setupESPNow(Adafruit_NeoPixel* pixels);
void monitorESPNowPeers(Adafruit_NeoPixel* pixels);
void espnowTask(void* pvParameters);
bool isTopicRegistered(const char* topic);
bool wasTopicUsedByDifferentMac(const char* topic, const uint8_t* mac);
int8_t findPeerByTopic(const char* topic);
int8_t findPeerByMac(const uint8_t* mac);
bool addPeer(const uint8_t* mac, const char* topic);
void removePeer(int8_t idx);
void cleanInactivePeers();
void updateESPNowDisplayData();
void sendESPNowMessage(const uint8_t* mac, uint8_t type, const char* topic, const uint8_t* data, size_t len);
bool processESPNowJson(const char* jsonString, size_t length, const uint8_t* sender_mac);

// Global data accessible to other modules
extern espnow_peer_t espnow_peers[MAX_ESPNOW_PEERS];
extern size_t espnow_peer_count;

#endif // ESPNOW_MODULE_H