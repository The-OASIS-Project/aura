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

#include "arduino_secrets.h"
#include "command_processor.h"
#include "display_module.h"
#include "espnow_module.h"
#include "logger.h"

// Global variables
espnow_peer_t espnow_peers[MAX_ESPNOW_PEERS];
size_t espnow_peer_count = 0;
SemaphoreHandle_t espnowMutex = NULL;

/*
 * ESP-Now shared key (must match on all devices)
 * NOTE: If you're using this code, change this key.
 *       1) It would be insecure to keep this key since it's public.
 *       2) If you don't change it and there's someone else using this code,
 *          your devices will connect to other systems.
 *
 * SECRET_PMK is located in the arduino_secrets.h.
 */
static const uint8_t PMK[16] = SECRET_PMK;

// ESP-Now broadcast address
static const uint8_t broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-Now callback for received data
void onESPNowDataRecv(const esp_now_recv_info* info, const uint8_t* data, int len) {
  // Extract the sender's MAC address from the info structure
  const uint8_t* mac = info->src_addr;

  if (len < sizeof(espnow_message_t)) {
    //LOG_PRINTLN(F("ESP-Now: Received malformed data (too short)"));
    return;
  }

  espnow_message_t msg;
  memcpy(&msg, data, sizeof(espnow_message_t));

  // Verify the message MAC matches the sender MAC
  if (memcmp(msg.mac, mac, 6) != 0) {
    LOG_PRINTLN(F("ESP-Now: MAC address mismatch - possible spoofing attempt"));
    return;
  }

  // Get current time for comparison
  uint32_t now = millis();

  // Process message based on type
  switch (msg.type) {
    case ESPNOW_MSG_REGISTRATION:
      {
          char macStr[18];
          snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

          LOG_PRINT(F("ESP-Now: Registration request from: "));
          LOG_PRINT(msg.topic);
          LOG_PRINT(F(" ("));
          LOG_PRINT(macStr);
          LOG_PRINTLN(F(")"));

          // Check if topic already exists
          if (isTopicRegistered(msg.topic) && wasTopicUsedByDifferentMac(msg.topic, mac)) {
            LOG_PRINTLN(F("ESP-Now: Topic already registered, sending rejection"));

            // Add peer temporarily as unencrypted to send rejection
            esp_now_peer_info_t temp_peer;
            memset(&temp_peer, 0, sizeof(temp_peer));
            memcpy(temp_peer.peer_addr, mac, 6);
            temp_peer.channel = ESPNOW_CHANNEL;
            temp_peer.encrypt = false;  // MUST be unencrypted for rejection

            esp_now_del_peer(mac);  // Remove if exists
            esp_err_t result = esp_now_add_peer(&temp_peer);

            if (result == ESP_OK) {
                // Send rejection unencrypted
                sendESPNowMessage(mac, ESPNOW_MSG_REG_REJECT, msg.topic, NULL, 0);

                // Small delay to ensure message is sent
                delay(10);

                // Remove the temporary peer
                esp_now_del_peer(mac);
            } else {
                LOG_PRINT(F("ESP-Now: Failed to add temp peer for rejection: "));
                LOG_PRINTLN(esp_err_to_name(result));
            }
          } else {
            // First, add the peer temporarily as unencrypted to send the ACK
            esp_now_peer_info_t temp_peer;
            memset(&temp_peer, 0, sizeof(temp_peer));
            memcpy(temp_peer.peer_addr, mac, 6);
            temp_peer.channel = ESPNOW_CHANNEL;
            temp_peer.encrypt = false;  // Unencrypted for ACK

            esp_now_del_peer(mac);  // Remove if exists
            esp_now_add_peer(&temp_peer);

            // Send the ACK unencrypted
            LOG_PRINTLN(F("ESP-Now: Sending unencrypted ACK"));
            sendESPNowMessage(mac, ESPNOW_MSG_REG_ACK, msg.topic, NULL, 0);

            // Small delay to ensure ACK is sent
            delay(10);

            // Now remove and re-add as encrypted for future communication
            esp_now_del_peer(mac);

            // Add peer properly with encryption
            if (addPeer(mac, msg.topic)) {
                LOG_PRINTLN(F("ESP-Now: Peer added with encryption"));
                updateESPNowDisplayData();
            } else {
                LOG_PRINTLN(F("ESP-Now: Failed to add peer"));
            }
          }
      }
      break;

    case ESPNOW_MSG_DATA:
      {
        int8_t peer_idx = findPeerByMac(mac);
        if (peer_idx >= 0) {
          // Update last seen time
          if (xSemaphoreTake(espnowMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            espnow_peers[peer_idx].last_seen = now;

            // Increment received packet counter
            espnow_peers[peer_idx].packets_received++;

            // Check for missed packets by comparing sequence numbers
            uint8_t expected_seq = (espnow_peers[peer_idx].last_seq_received + 1) & 0xFF;
            if (msg.sequence != expected_seq && espnow_peers[peer_idx].last_seq_received != 0) {
              // Calculate missed packets (accounting for rollover)
              uint8_t missed = (msg.sequence - expected_seq) & 0xFF;
              espnow_peers[peer_idx].packets_missed += missed;
              LOG_PRINT(F("ESP-Now: Missed "));
              LOG_PRINT(String(missed));
              LOG_PRINT(F(" packets from "));
              LOG_PRINTLN(espnow_peers[peer_idx].topic);
            }

            espnow_peers[peer_idx].last_seq_received = msg.sequence;
            xSemaphoreGive(espnowMutex);
          }

          // Process the JSON data if present
          if (msg.data_len > 0) {
            // Ensure null termination
            uint8_t json_data[201];
            memcpy(json_data, msg.data, msg.data_len);
            json_data[msg.data_len] = '\0';

            //LOG_PRINT(F("ESP-Now: Data from "));
            //LOG_PRINT(espnow_peers[peer_idx].topic);
            //LOG_PRINT(F(": "));
            //LOG_PRINTLN((char*)json_data);

            // Process the JSON data
            processESPNowJson((char*)json_data, msg.data_len, mac);
          }

          // Send acknowledgment
          sendESPNowMessage(mac, ESPNOW_MSG_DATA_ACK, msg.topic, &msg.sequence, 1);
        } else {
          LOG_PRINTLN(F("ESP-Now: Data received from unknown peer, requesting registration"));
          // We don't know this peer, request re-registration by sending reject
          sendESPNowMessage(mac, ESPNOW_MSG_REG_REJECT, msg.topic, NULL, 0);
        }
      }
      break;

    case ESPNOW_MSG_PING:
      // Send pong response
      sendESPNowMessage(mac, ESPNOW_MSG_PONG, msg.topic, NULL, 0);
      break;

    // Add handlers for other message types as needed
    default:
      LOG_PRINT(F("ESP-Now: Unknown message type: "));
      LOG_PRINTLN(String(msg.type));
      break;
  }
}

// ESP-Now callback for send status
void onESPNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
          mac_addr[0], mac_addr[1], mac_addr[2],
          mac_addr[3], mac_addr[4], mac_addr[5]);

  if (status == ESP_NOW_SEND_SUCCESS) {
    //LOG_PRINT(F("ESP-Now: Message sent successfully to "));
    //LOG_PRINTLN(macStr);
  } else {
    LOG_PRINT(F("ESP-Now: Failed to send message to "));
    LOG_PRINTLN(macStr);
  }
}

// Initialize ESP-Now
void setupESPNow(Adafruit_NeoPixel* pixels) {
  LOG_PRINTLN(F("Setting up ESP-Now..."));

  // Create mutex for thread-safe access to peer list
  espnowMutex = xSemaphoreCreateMutex();
  if (espnowMutex == NULL) {
    LOG_PRINTLN(F("Failed to create espnowMutex"));
  }

  // Set WiFi mode properly for ESP-Now
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100); // Give WiFi time to initialize

  // Set the WiFi channel explicitly - IMPORTANT!
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Log channel information
  uint8_t channel;
  esp_wifi_get_channel(&channel, NULL);
  LOG_PRINT(F("ESP-Now operating on channel: "));
  LOG_PRINTLN(String(channel));

  // Wait for WiFi to fully initialize and verify MAC address
  uint8_t macAddr[6];
  int attempts = 0;
  bool validMac = false;

  while (!validMac && attempts < 5) {
    WiFi.macAddress(macAddr);
    // Check if MAC is all zeros
    validMac = false;
    for (int i = 0; i < 6; i++) {
      if (macAddr[i] != 0) {
        validMac = true;
        break;
      }
    }

    if (!validMac) {
      LOG_PRINTLN(F("Invalid MAC detected, waiting for WiFi initialization..."));
      delay(500);
      attempts++;
    }
  }

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
  LOG_PRINT(F("AURA MAC Address: "));
  LOG_PRINTLN(macStr);

  if (!validMac) {
    LOG_PRINTLN(F("WARNING: Could not get valid MAC address!"));
    pixels->setPixelColor(0, pixels->Color(255, 0, 0)); // Red for failure
    pixels->show();
  }

  // Initialize ESP-Now
  if (esp_now_init() != ESP_OK) {
    LOG_PRINTLN(F("ESP-Now initialization failed"));
    pixels->setPixelColor(0, pixels->Color(255, 0, 0)); // Red for failure
    pixels->show();
    return;
  }

  delay(100); // Short delay after initialization

  // Set Primary Master Key (PMK) for encryption
  if (esp_now_set_pmk((uint8_t *)PMK) != ESP_OK) {
    LOG_PRINTLN(F("Failed to set PMK for encryption"));
  } else {
    LOG_PRINTLN(F("Successfully set shared encryption key"));
  }

  // Register callbacks
  if (esp_now_register_recv_cb(onESPNowDataRecv) != ESP_OK) {
    LOG_PRINTLN(F("Failed to register receive callback!"));
  } else {
    LOG_PRINTLN(F("Successfully registered receive callback"));
  }

  if (esp_now_register_send_cb(onESPNowDataSent) != ESP_OK) {
    LOG_PRINTLN(F("Failed to register send callback!"));
  } else {
    LOG_PRINTLN(F("Successfully registered send callback"));
  }

  // Add broadcast peer for discovery (cannot be encrypted)
  esp_now_peer_info_t bc;
  memset(&bc, 0, sizeof(bc));
  memcpy(bc.peer_addr, broadcast_addr, 6);
  bc.channel = ESPNOW_CHANNEL;
  bc.encrypt = false;  // Broadcast can't be encrypted

  // First delete if it exists (in case of reinitialization)
  esp_now_del_peer(broadcast_addr);

  if (esp_now_add_peer(&bc) == ESP_OK) {
    LOG_PRINTLN(F("ESP-Now: Broadcast peer added"));
  } else {
    LOG_PRINTLN(F("ESP-Now: Failed to add broadcast peer"));
  }

  // Initialize peer list
  memset(espnow_peers, 0, sizeof(espnow_peers));
  espnow_peer_count = 0;

  LOG_PRINTLN(F("ESP-Now initialized successfully"));
  pixels->setPixelColor(0, pixels->Color(0, 0, 255)); // Blue for success
  pixels->show();
}

void monitorESPNowPeers(Adafruit_NeoPixel* pixels) {
  static unsigned long last_cleanup = 0;
  unsigned long now = millis();

  // Clean inactive peers every 10 seconds
  if (now - last_cleanup > 10000) {
    last_cleanup = now;
    cleanInactivePeers();

    // Update LED based on peer count
    if (espnow_peer_count > 0) {
      pixels->setPixelColor(0, pixels->Color(0, 255, 0)); // Green when peers connected
    } else {
      pixels->setPixelColor(0, pixels->Color(0, 0, 255)); // Blue when no peers
    }
    pixels->show();
  }
}

// Check if topic is already registered
bool isTopicRegistered(const char* topic) {
  if (xSemaphoreTake(espnowMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (size_t i = 0; i < espnow_peer_count; i++) {
      if (strcmp(espnow_peers[i].topic, topic) == 0) {
        xSemaphoreGive(espnowMutex);
        return true;
      }
    }
    xSemaphoreGive(espnowMutex);
  }
  return false;
}

// Check if topic was used by a different MAC address (for handling race conditions)
bool wasTopicUsedByDifferentMac(const char* topic, const uint8_t* mac) {
   if (xSemaphoreTake(espnowMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      for (size_t i = 0; i < espnow_peer_count; i++) {
         // If we find the topic but with a different MAC
         if (strcmp(espnow_peers[i].topic, topic) == 0 &&
             memcmp(espnow_peers[i].mac, mac, 6) != 0) {
            xSemaphoreGive(espnowMutex);
            return true;
         }
      }
      xSemaphoreGive(espnowMutex);
   }
   return false;
}

// Find peer index by topic
int8_t findPeerByTopic(const char* topic) {
  if (xSemaphoreTake(espnowMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (size_t i = 0; i < espnow_peer_count; i++) {
      if (strcmp(espnow_peers[i].topic, topic) == 0) {
        xSemaphoreGive(espnowMutex);
        return i;
      }
    }
    xSemaphoreGive(espnowMutex);
  }
  return -1; // Not found
}

// Find peer index by MAC address
int8_t findPeerByMac(const uint8_t* mac) {
  if (xSemaphoreTake(espnowMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (size_t i = 0; i < espnow_peer_count; i++) {
      if (memcmp(espnow_peers[i].mac, mac, 6) == 0) {
        xSemaphoreGive(espnowMutex);
        return i;
      }
    }
    xSemaphoreGive(espnowMutex);
  }
  return -1; // Not found
}

// Add a new peer to the list
bool addPeer(const uint8_t* mac, const char* topic) {
  // Check if we already have this peer by MAC
  int8_t existing_idx = findPeerByMac(mac);
  if (existing_idx >= 0) {
    LOG_PRINTLN(F("ESP-Now: Peer with this MAC already exists, updating topic"));

    if (xSemaphoreTake(espnowMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      espnow_peers[existing_idx].last_seen = millis();
      espnow_peers[existing_idx].last_seq_received = 0;
      espnow_peers[existing_idx].packets_received = 0;
      espnow_peers[existing_idx].packets_missed = 0;
      espnow_peers[existing_idx].active = true;
      xSemaphoreGive(espnowMutex);
    }
    return true;
  }

  // Check if we have space for a new peer
  if (espnow_peer_count >= MAX_ESPNOW_PEERS) {
    LOG_PRINTLN(F("ESP-Now: Maximum number of peers reached"));
    return false;
  }

  // Add peer to ESP-Now with encryption
  esp_now_peer_info_t peer_info;
  memset(&peer_info, 0, sizeof(peer_info));
  memcpy(peer_info.peer_addr, mac, 6);
  peer_info.channel = ESPNOW_CHANNEL;
  peer_info.encrypt = true;
  memcpy(peer_info.lmk, PMK, 16);

  esp_err_t result = esp_now_add_peer(&peer_info);
  if (result != ESP_OK) {
    LOG_PRINT(F("ESP-Now: Failed to add encrypted peer, error: "));
    LOG_PRINTLN(esp_err_to_name(result));

    return false;
  }

  // Add to our tracking list
  if (xSemaphoreTake(espnowMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    memcpy(espnow_peers[espnow_peer_count].mac, mac, 6);
    strncpy(espnow_peers[espnow_peer_count].topic, topic, sizeof(espnow_peers[espnow_peer_count].topic));
    espnow_peers[espnow_peer_count].last_seen = millis();
    espnow_peers[espnow_peer_count].last_seq_received = 0;
    espnow_peers[espnow_peer_count].active = true;
    espnow_peer_count++;
    xSemaphoreGive(espnowMutex);
  }

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
         
  LOG_PRINT(F("ESP-Now: Added peer: "));
  LOG_PRINT(topic);
  LOG_PRINT(F(" ("));
  LOG_PRINT(macStr);
  LOG_PRINTLN(F(")"));

  return true;
}

// Remove a peer from the list
void removePeer(int8_t idx) {
  if (idx < 0 || idx >= espnow_peer_count) {
    return;
  }

  if (xSemaphoreTake(espnowMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // Remove from ESP-Now
    esp_now_del_peer(espnow_peers[idx].mac);

    LOG_PRINT(F("ESP-Now: Removed peer: "));
    LOG_PRINT(espnow_peers[idx].topic);
    LOG_PRINT(F(" ("));
    for (int i = 0; i < 6; i++) {
      LOG_PRINT(String(espnow_peers[idx].mac[i], HEX));
      if (i < 5) LOG_PRINT(F(":"));
    }
    LOG_PRINTLN(F(")"));

    // Shift array to remove this peer
    for (size_t i = idx; i < espnow_peer_count - 1; i++) {
      memcpy(&espnow_peers[i], &espnow_peers[i + 1], sizeof(espnow_peer_t));
    }
    espnow_peer_count--;
    xSemaphoreGive(espnowMutex);
  }

  // Update the display
  updateESPNowDisplayData();
}

// Clean inactive peers (not seen for 30 seconds)
void cleanInactivePeers() {
  uint32_t now = millis();

  if (xSemaphoreTake(espnowMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (int8_t i = 0; i < espnow_peer_count; i++) {
      if (now - espnow_peers[i].last_seen > 30000) { // 30 seconds timeout
        // Mark the peer for removal
        esp_now_del_peer(espnow_peers[i].mac);

        LOG_PRINT(F("ESP-Now: Timeout for peer: "));
        LOG_PRINT(espnow_peers[i].topic);
        LOG_PRINT(F(" ("));
        for (int j = 0; j < 6; j++) {
          LOG_PRINT(String(espnow_peers[i].mac[j], HEX));
          if (j < 5) LOG_PRINT(F(":"));
        }
        LOG_PRINTLN(F(")"));

        // Shift array to remove this peer
        for (size_t j = i; j < espnow_peer_count - 1; j++) {
          memcpy(&espnow_peers[j], &espnow_peers[j + 1], sizeof(espnow_peer_t));
        }
        espnow_peer_count--;
        i--; // Adjust index after removal
      }
    }
    xSemaphoreGive(espnowMutex);
  }

  // Update the display
  updateESPNowDisplayData();
}

// Send an ESP-Now message
void sendESPNowMessage(const uint8_t* mac, uint8_t type, const char* topic, const uint8_t* data, size_t len) {
  espnow_message_t msg;
  memset(&msg, 0, sizeof(msg));

  // Set message fields
  msg.type = type;
  msg.sequence = 0; // Only used for data messages
  strncpy(msg.topic, topic, sizeof(msg.topic));
  msg.timestamp = millis();

  // Get our own MAC
  uint8_t self_mac[6];
  WiFi.macAddress(self_mac);
  memcpy(msg.mac, self_mac, 6);

  // Copy data if provided
  if (data != NULL && len > 0) {
    if (len > sizeof(msg.data)) {
      len = sizeof(msg.data);
    }
    memcpy(msg.data, data, len);
    msg.data_len = len;
  } else {
    msg.data_len = 0;
  }

  // Log more details about the message being sent
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Only send once
  esp_err_t result = esp_now_send(mac, (uint8_t*)&msg, sizeof(msg));

  if (result != ESP_OK) {
    LOG_PRINT(F("ESP-Now: Failed to send message, error code: "));
    LOG_PRINTLN(String(result));
  }
}

// Process JSON data received via ESP-Now
bool processESPNowJson(const char* jsonString, size_t length, const uint8_t* sender_mac) {
  // Find peer by MAC
  int8_t peer_idx = findPeerByMac(sender_mac);
  if (peer_idx < 0) {
    LOG_PRINTLN(F("ESP-Now: Received JSON from unknown peer"));
    return false;
  }

  // Parse the JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonString, length);

  if (error) {
    LOG_PRINT(F("ESP-Now: JSON parsing failed: "));
    LOG_PRINTLN(error.c_str());
    return false;
  }

  // Forward to the common command processor if it's a command
  if (doc.containsKey("action")) {
    return processCommandJson(jsonString, length);
  }

  // Otherwise, pass through to logger for normal processing
  if (doc.containsKey("device")) {
    // Process as a regular sensor data message
    logger_send_json(&doc, doc["device"]);
    return true;
  }

  return false;
}

// Update ESP-Now data for display module
void updateESPNowDisplayData() {
  if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    // Update ESP-Now peer count in display data
    display_data.espnow_peer_count = espnow_peer_count;

    // Copy up to MAX_DISPLAY_PEERS topics to display data
    int peers_to_copy = (int)espnow_peer_count < MAX_DISPLAY_PEERS ? (int)espnow_peer_count : MAX_DISPLAY_PEERS;
    for (int i = 0; i < peers_to_copy; i++) {
      strncpy(display_data.espnow_peers[i], espnow_peers[i].topic, sizeof(display_data.espnow_peers[i]));
      display_data.espnow_packets_received[i] = espnow_peers[i].packets_received;
      display_data.espnow_packets_missed[i] = espnow_peers[i].packets_missed;
    }

    xSemaphoreGive(displayMutex);
  }
}

// ESP-Now task function
void espnowTask(void* pvParameters) {
  LOG_PRINTLN(F("ESP-Now task started"));

  uint32_t last_cleanup = 0;

  while (true) {
    uint32_t now = millis();

    // Clean inactive peers every 5 seconds
    if (now - last_cleanup > 5000) {
      last_cleanup = now;
      cleanInactivePeers();
    }

    // Short delay to prevent CPU hogging
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
