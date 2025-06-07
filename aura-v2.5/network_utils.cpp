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

#include "network_utils.h"
#include "logger.h"
#include "display_module.h"
#include "faceplate_module.h"  // Added for faceplate control

#ifdef ENABLE_MQTT
// MQTT configuration
extern WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
String brokerString;
const char* broker = NULL;
int port = MQTT_PORT;
const char topic[] = MQTT_TOPIC;

// Forward declaration of callback function
void onMqttMessageReceived(int messageSize);
#endif

#ifdef ENABLE_SOCKET
IPAddress serverIP;
#endif

void setupNetworking(const char* ssid, const char* pass, int retry_attempts, WiFiClient* wifiClient, Adafruit_NeoPixel* pixels) {
  int wifiRetries = 0;

  // attempt to connect to WiFi network:
  LOG_PRINT("Attempting to connect to WPA SSID: ");
  LOG_PRINTLN(ssid);
  WiFi.begin(ssid, pass);
  while ((WiFi.status() != WL_CONNECTED) && (wifiRetries < retry_attempts)) {
    // failed, retry
    LOG_PRINT(".");
    delay(5000);
    wifiRetries++;
  }

  if (wifiRetries == retry_attempts) {
    LOG_PRINTLN("Retry timeout.");
    return;
  }

  pixels->setPixelColor(0, pixels->Color(0, 255, 0));  // Green on successful connection
  pixels->show();

  LOG_PRINTLN("WiFi connected");
  LOG_PRINTLN("IP address: ");
  LOG_PRINTLN(WiFi.localIP().toString());

  LOG_PRINT("GATEWAY: ");
  LOG_PRINTLN(WiFi.gatewayIP().toString());

  // Set maximum WiFi transmission power
  esp_wifi_set_max_tx_power(84);  // 84 corresponds to 21 dBm, which is the maximum for ESP32-S3

#ifdef ENABLE_MQTT
  setupMQTT(wifiClient, pixels);
#endif

#ifdef ENABLE_SOCKET
  setupSocket(wifiClient, pixels);
#endif
}

#ifdef ENABLE_MQTT
void setupMQTT(WiFiClient* wifiClient, Adafruit_NeoPixel* pixels) {
  mqttClient.setId("helmet");

  // You can provide a username and password for authentication
  // mqttClient.setUsernamePassword("username", "password");

  brokerString = WiFi.gatewayIP().toString();
  broker = brokerString.c_str();

  LOG_PRINT("Attempting to connect to the MQTT broker: ");
  LOG_PRINTLN(broker);

  if (!mqttClient.connect(broker, port)) {
    LOG_PRINT("MQTT connection failed! Error code = ");
    LOG_PRINTLN(String(mqttClient.connectError()));
    return;
  }

  LOG_PRINTLN("You're connected to the MQTT broker!");
  LOG_PRINTLN("");

  pixels->setPixelColor(0, pixels->Color(0, 0, 255));  // Blue on MQTT success
  pixels->show();

  mqttClient.setKeepAliveInterval(5);
  mqttClient.setCleanSession(true);

  // Set the callback for incoming messages
  mqttClient.onMessage(onMqttMessageReceived);

  LOG_PRINT("Subscribing to topic: ");
  LOG_PRINTLN(topic);
  LOG_PRINTLN("");

  // Subscribe to the helmet topic for receiving commands
  mqttClient.subscribe(topic);

  LOG_PRINT("Waiting for messages on topic: ");
  LOG_PRINTLN(topic);
  LOG_PRINTLN("");
}
#endif

#ifdef ENABLE_SOCKET
void setupSocket(WiFiClient* wifiClient, Adafruit_NeoPixel* pixels) {
  serverIP = WiFi.gatewayIP();

  // Connect to the server
  reconnectSocket(wifiClient, pixels);
}

bool reconnectSocket(WiFiClient* wifiClient, Adafruit_NeoPixel* pixels) {
  unsigned long startAttemptTime = millis();

  while (!wifiClient->connect(serverIP, SERVER_PORT)) {
    if (millis() - startAttemptTime > SOCKET_RECONNECT_TIMEOUT_MS) {
      LOG_PRINTLN("Reconnection timeout reached, giving up.");
      pixels->setPixelColor(0, pixels->Color(0, 255, 0));  // Set to green on failure
      pixels->show();
      return false;  // Return false if reconnection fails within the timeout period
    }

    LOG_PRINTLN("Connection failed, retrying...");
    delay(1000);  // Wait 1 second before retrying
  }

  LOG_PRINTLN("Reconnected to server");
  pixels->setPixelColor(0, pixels->Color(0, 0, 255));  // Set to blue on success
  pixels->show();
  return true;
}
#endif

void monitorConnection(WiFiClient* wifiClient, Adafruit_NeoPixel* pixels) {
#ifdef ENABLE_MQTT
  // Poll for new MQTT messages
  mqttClient.poll();
#endif

#ifdef ENABLE_SOCKET
  if (!wifiClient->connected()) {
    pixels->setPixelColor(0, pixels->Color(0, 255, 0));  // Set to red on failure
    pixels->show();

    // Attempt to reconnect if the client is disconnected
    if (!reconnectSocket(wifiClient, pixels)) {
      // Handle failure to reconnect, if necessary
      LOG_PRINTLN("Failed to reconnect. Entering an error state.");
    }
  }
#endif

  // Update WiFi status for display
  updateWiFiDisplayData(wifiClient, pixels);
}

#ifdef WIFI_MODE
void updateWiFiDisplayData(WiFiClient* wifiClient, Adafruit_NeoPixel* pixels) {
  if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    display_data.is_connected = (WiFi.status() == WL_CONNECTED);
    display_data.rssi = WiFi.RSSI();

    if (display_data.is_connected) {
      strcpy(display_data.ip_address, WiFi.localIP().toString().c_str());
    } else {
      strcpy(display_data.ip_address, "0.0.0.0");
    }

    xSemaphoreGive(displayMutex);
  }
}
#endif

// MQTT message callback function - implemented in mqtt_callback.cpp
#ifdef ENABLE_MQTT
extern void onMqttMessageReceived(int messageSize);
#endif
