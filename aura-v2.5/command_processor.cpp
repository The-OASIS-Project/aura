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

#include "command_processor.h"
#include "faceplate_module.h"
#include "logger.h"

// Define a buffer for serial commands
#define SERIAL_BUFFER_SIZE 512
static char serialBuffer[SERIAL_BUFFER_SIZE];
static size_t serialBufferIndex = 0;

// Setup the serial command processor
void setupSerialCommands() {
  // Initialize the buffer
  memset(serialBuffer, 0, SERIAL_BUFFER_SIZE);
  serialBufferIndex = 0;

  // Also clear any existing data in the Serial buffer
  while (Serial.available()) {
    Serial.read();
  }

  LOG_PRINTLN(F("Serial command processor initialized"));
}

// Common function to process command JSON regardless of source (MQTT or Serial)
bool processCommandJson(const char* jsonString, size_t length) {
  // Only process if the message isn't too large
  if (length > SERIAL_BUFFER_SIZE) {
    LOG_PRINTLN(F("Received message is too large to process"));
    return false;
  }

  // Log the received message
  LOG_PRINT(F("Processing command: "));
  LOG_PRINTLN(jsonString);

  // Parse the JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonString, length);

  if (error) {
    LOG_PRINT(F("JSON parsing failed: "));
    LOG_PRINTLN(error.c_str());
    return false;
  }

  // Check for faceplate action
  if (doc.containsKey("device") && strcmp(doc["device"], "faceplate") == 0) {
    if (doc.containsKey("action")) {
      // Get the action
      const char* action = doc["action"];

      LOG_PRINT(F("Faceplate command received: "));
      LOG_PRINTLN(action);

      // Act based on the action
      if ((strcmp(action, "open") == 0) || (strcmp(action, "enable") == 0)) {
        // Open faceplate command
        LOG_PRINTLN(F("Command: Opening faceplate"));
        openFaceplate();
        return true;
      } else if ((strcmp(action, "close") == 0) || (strcmp(action, "disable") == 0)) {
        // Close faceplate command
        LOG_PRINTLN(F("Command: Closing faceplate"));
        closeFaceplate();
        return true;
      } else if (strcmp(action, "toggle") == 0) {
        // Toggle faceplate command
        LOG_PRINTLN(F("Command: Toggling faceplate"));
        toggleFaceplateState();
        return true;
      } else {
        LOG_PRINT(F("Unknown faceplate action: "));
        LOG_PRINTLN(action);
      }
    }
  }
  
  // Add other command types here as needed
  
  return false;
}

// Function to check and process serial commands
void checkSerialCommands() {
  // Check if there's data available on the Serial port
  while (Serial.available() > 0) {
    // Read a character
    char c = Serial.read();

    // Check for newline or maximum buffer size
    if (c == '\n' || c == '\r') {
      // If we have data in the buffer, process it
      if (serialBufferIndex > 0) {
        // Null-terminate the string
        serialBuffer[serialBufferIndex] = '\0';
        
        // Process the command - pass ONLY the valid portion
        processCommandJson(serialBuffer, serialBufferIndex);
        
        // Clear the entire buffer explicitly
        memset(serialBuffer, 0, SERIAL_BUFFER_SIZE);
        serialBufferIndex = 0;
      }
    } else if (serialBufferIndex < SERIAL_BUFFER_SIZE - 1) {
      // Add character to buffer if there's space
      serialBuffer[serialBufferIndex++] = c;
    } else {
      // Buffer overflow - reset everything
      LOG_PRINTLN(F("Serial buffer overflow, resetting"));
      memset(serialBuffer, 0, SERIAL_BUFFER_SIZE);
      serialBufferIndex = 0;
    }
  }
}
