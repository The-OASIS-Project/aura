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

#include <ArduinoJson.h>
#include "network_utils.h"
#include "logger.h"
#include "faceplate_module.h"
#include "command_processor.h"
#include "config.h"

#ifdef ENABLE_MQTT
#define MAX_MESSAGE_SIZE  4096

static char message[MAX_MESSAGE_SIZE] = "";

// MQTT message callback function
void onMqttMessageReceived(int messageSize) {
  int i = 0;
  // Only process if the message isn't too large
  if (messageSize > MAX_MESSAGE_SIZE - 1) {
    LOG_PRINTLN(F("Received message is too large to process"));
    // Skip the message
    while (mqttClient.available()) {
      mqttClient.read();
    }
    return;
  }

  // Read the message
  while (mqttClient.available() && i < messageSize) {
    message[i++] = (char)mqttClient.read();
  }
  message[i] = '\0';  // Null terminate

  LOG_PRINT(F("Helmet received command \""));
  LOG_PRINT(message);
  LOG_PRINTLN(F("\". Processing..."));
  // Forward to the common command processor
  processCommandJson(message, messageSize);
}

#endif  // ENABLE_MQTT
