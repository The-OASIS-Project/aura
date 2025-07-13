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

#include "enviro_module.h"
#include "logger.h"
#include "display_module.h"

// Environmental Sensors
ScioSense_ENS160 ens160(0x53);  // ENS160 sensor with I2C address 0x53
SensirionI2cScd4x scd4x;        // SCD41 CO2 sensor
bool scd41_available = false;

// Return descriptive quality string based on CO2 reading
const char* getCO2QualityDescription(uint16_t co2_ppm) {
  if (co2_ppm < CO2_EXCELLENT) return "Excellent";
  else if (co2_ppm < CO2_GOOD) return "Good";
  else if (co2_ppm < CO2_FAIR) return "Fair";
  else if (co2_ppm < CO2_POOR) return "Poor";
  else return "Very Poor";
}

#ifdef ENABLE_MQTT
extern MqttClient mqttClient;
extern const char topic[];
#endif

// Helper function for floating-point mapping (Arduino's map() only works with integers)
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void setupEnvironmental() {
  // ENS160 setup
  if (!ens160.begin()) {
    LOG_PRINTLN("Could not find a valid ENS160 sensor, check wiring!");

    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      display_data.ens160_available = false;
      display_data.air_quality_available = false;
      xSemaphoreGive(displayMutex);
    }
  } else {
    LOG_PRINTLN("ENS160 found!");

    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      display_data.ens160_available = true;
      display_data.air_quality_available = true;
      xSemaphoreGive(displayMutex);
    }

    // Set mode to standard operation
    ens160.setMode(ENS160_OPMODE_STD);

    // Give some time to warm up (sensor needs up to 1 hour for full warm-up,
    // but we'll allow a few seconds for basic operation)
    delay(2000);
  }

  // SCD41 setup following manufacturer's recommended initialization sequence
  LOG_PRINTLN("Initializing SCD41...");
  scd4x.begin(Wire, SCD41_I2C_ADDR_62);

  // Ensure sensor is in clean state by proper initialization sequence
  delay(30);  // Short delay after begin

  uint16_t error = scd4x.wakeUp();
  if (error != 0) {
    LOG_PRINT("Error during wakeUp(): ");
    LOG_PRINTLN(String(error));
  }

  error = scd4x.stopPeriodicMeasurement();
  if (error != 0) {
    LOG_PRINT("Error during stopPeriodicMeasurement(): ");
    LOG_PRINTLN(String(error));
  }

  error = scd4x.reinit();
  if (error != 0) {
    LOG_PRINT("Error during reinit(): ");
    LOG_PRINTLN(String(error));
  }

  // Read serial number to verify communication
  uint64_t serialNumber = 0;
  error = scd4x.getSerialNumber(serialNumber);

  if (error != 0) {
    LOG_PRINTLN("Could not read SCD41 serial number, sensor might not be present");
    scd41_available = false;
  } else {
    LOG_PRINT("SCD41 found! Serial: ");
    // Print serial number in hex format
    char serialStr[20];
    sprintf(serialStr, "0x%08lX%08lX",
            (unsigned long)(serialNumber >> 32),
            (unsigned long)(serialNumber & 0xFFFFFFFF));
    LOG_PRINTLN(serialStr);

    // Set additional configuration if needed
    // For example, scd4x.setAmbientPressure() if needed

    // Start periodic measurements
    error = scd4x.startPeriodicMeasurement();
    if (error != 0) {
      LOG_PRINT("Error starting periodic measurement: ");
      LOG_PRINTLN(String(error));
      scd41_available = false;
      display_data.temp_available = false;
      display_data.humidity_available = false;
    } else {
      LOG_PRINTLN("SCD41 periodic measurement started");
      scd41_available = true;
      display_data.temp_available = true;
      display_data.humidity_available = true;
    }
  }

  // Initialize the CO2 availability in display data
  if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    display_data.co2_available = scd41_available;

    // Default values if sensor not available
    if (!scd41_available) {
      display_data.co2 = 0;
      strcpy(display_data.co2_quality_description, "Unavailable");
    }

    xSemaphoreGive(displayMutex);
  }
}

void attemptEnviroReinitialization() {
  LOG_PRINTLN("Attempting to reinitialize environmental sensors...");

  bool air_quality_available = false;
  bool ens160_available = false;

  // Get current status
  if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    air_quality_available = display_data.air_quality_available;
    ens160_available = display_data.ens160_available;
    xSemaphoreGive(displayMutex);
  }

  if (!ens160_available || !air_quality_available) {
    if (ens160.begin()) {
      LOG_PRINTLN("ENS160 reinitialized successfully!");
      ens160.setMode(ENS160_OPMODE_STD);

      if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        display_data.ens160_available = true;
        display_data.air_quality_available = true;
        xSemaphoreGive(displayMutex);
      }
    }
  }

  // Check if SCD41 is unavailable
  bool co2_available = false;

  if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    co2_available = display_data.co2_available;
    xSemaphoreGive(displayMutex);
  }

  if (!co2_available) {
    // Try to reinitialize SCD41...
    // This would follow a similar pattern to your existing SCD41 initialization code
  }
}

// Textify the potential explanation of the difference in co2 to eco2
String getCO2SourceAnalysis(uint16_t co2, uint16_t eco2) {
  int diff = co2 - eco2;

  if (diff > 400) {
    return "Human respiration";
  } else if (diff > 200) {
    return "Mainly respiration";
  } else if (abs(diff) <= 200) {
    return "Mixed sources";
  } else if (diff < -200) {
    return "Chemical sources";
  }

  return "Unknown sources";
}

// Environmental sensor task runs every 5 seconds
void enviroTask(void* pvParameters) {
  float temperature = 0.0;
  float humidity = 0.0;
  float air_quality_score = 0.0;

  unsigned long last_reinit_attempt = 0;
  const unsigned long REINIT_INTERVAL = 60000;  // Try to reinitialize every minute

  uint16_t eco2 = 0;
  uint16_t tvoc = 0;
  uint16_t co2_ppm = 0;

  bool any_sensor_available = false;

  JsonDocument doc;

  LOG_PRINTLN(F("Environmental task started."));

  while (true) {
    // Check if we need to attempt reinitialization
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      any_sensor_available = display_data.temp_available || display_data.humidity_available || display_data.air_quality_available || display_data.ens160_available || display_data.co2_available;
      xSemaphoreGive(displayMutex);
    }

    if (!any_sensor_available) {
      // Try to reinitialize periodically if all sensors are unavailable
      unsigned long now = millis();
      if (now - last_reinit_attempt > REINIT_INTERVAL) {
        attemptEnviroReinitialization();
        last_reinit_attempt = now;
        // After attempting reinitialization, check if any sensor became available
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          any_sensor_available = display_data.temp_available || display_data.humidity_available || display_data.air_quality_available || display_data.ens160_available || display_data.co2_available;
          xSemaphoreGive(displayMutex);
        }

        if (!any_sensor_available) {
          vTaskDelay(pdMS_TO_TICKS(5000));  // Sleep longer if still no sensors
          continue;
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(1000));  // Sleep shorter between reinitialization attempts
        continue;
      }
    }

    /* Read air quality data from ENS160 */
    if (ens160.measure()) {
      // Read Air Quality Index (AQI) - 1 (good) to 5 (poor)
      uint8_t aqi = ens160.getAQI();

      // Read eCO2 level
      eco2 = ens160.geteCO2();

      // Read TVOC level
      tvoc = ens160.getTVOC();

      // Map AQI to our 0-100 scale (where 100 is excellent)
      // ENS160 AQI: 1=excellent, 2=good, 3=moderate, 4=poor, 5=unhealthy
      // Inverse mapping: 1->100, 2->80, 3->60, 4->40, 5->20
      air_quality_score = 120 - (aqi * 20);
    } else {
      LOG_PRINTLN("Failed to read from ENS160 sensor");
    }

    /* Read SCD41 CO2 sensor (if available) */
    if (scd41_available) {
      // Check if new data is available
      bool dataReady = false;
      uint16_t error = scd4x.getDataReadyStatus(dataReady);

      if (error != 0) {
        LOG_PRINT("Error checking data ready status: ");
        LOG_PRINTLN(String(error));
      } else if (dataReady) {
        // Read measurement
        error = scd4x.readMeasurement(co2_ppm, temperature, humidity);

        if (error != 0) {
          LOG_PRINT("Error reading measurement: ");
          LOG_PRINTLN(String(error));
        } else if (co2_ppm != 0) {  // CO2 of 0ppm indicates an invalid reading
          // We now have valid CO2 data
          // Update display data
          if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            display_data.co2 = co2_ppm;
            strncpy(display_data.co2_quality_description,
                    getCO2QualityDescription(co2_ppm),
                    sizeof(display_data.co2_quality_description));
            xSemaphoreGive(displayMutex);
          }
        }
      }
    }

    /* Prepare data for sending */
    doc["device"] = "Enviro";
    doc["temp"] = temperature;
    doc["humidity"] = humidity;
    doc["tvoc_ppb"] = tvoc;
    doc["eco2_ppm"] = eco2;

    if (scd41_available && co2_ppm != 0) {
      doc["co2_ppm"] = co2_ppm;
      doc["co2_quality"] = getCO2QualityDescription(co2_ppm);
      doc["co2_eco2_diff"] = (int)co2_ppm - eco2;
      doc["co2_source_analysis"] = getCO2SourceAnalysis(co2_ppm, eco2);
    }

    doc["air_quality"] = air_quality_score;

    // Add air quality description
    if (air_quality_score > 80)
      doc["air_quality_description"] = "Excellent";
    else if (air_quality_score > 60)
      doc["air_quality_description"] = "Good";
    else if (air_quality_score > 40)
      doc["air_quality_description"] = "Average";
    else if (air_quality_score > 20)
      doc["air_quality_description"] = "Poor";
    else
      doc["air_quality_description"] = "Very Poor";

    // Add heat index calculation (Celsius)
    if (temperature >= 26.7 && humidity >= 40) {
      // Convert temperature to Celsius for the formula
      // Using the Steadman formula adapted for Celsius
      float heat_index_c = temperature + 0.348 * humidity - 0.09 * temperature * humidity / 100 + 0.02 * temperature * temperature - 0.0312 * temperature * temperature * humidity / 100 + 0.0164 * humidity * humidity / 100 - 0.0022 * temperature * humidity * humidity / 10000 - 0.0047 * temperature * temperature * temperature / 100 + 0.001 * temperature * temperature * humidity * humidity / 10000;

      doc["heat_index_c"] = heat_index_c;
    }

    // Add dew point calculation
    float a = 17.27;
    float b = 237.7;
    float alpha = ((a * temperature) / (b + temperature)) + log(humidity / 100.0);
    float dew_point = (b * alpha) / (a - alpha);
    doc["dew_point"] = dew_point;

    // Update display data
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      display_data.temperature = temperature;
      display_data.humidity = humidity;
      display_data.air_quality = air_quality_score;

      if (air_quality_score > 80)
        strncpy(display_data.air_quality_description, "Excellent", sizeof(display_data.air_quality_description));
      else if (air_quality_score > 60)
        strncpy(display_data.air_quality_description, "Good", sizeof(display_data.air_quality_description));
      else if (air_quality_score > 40)
        strncpy(display_data.air_quality_description, "Average", sizeof(display_data.air_quality_description));
      else if (air_quality_score > 20)
        strncpy(display_data.air_quality_description, "Poor", sizeof(display_data.air_quality_description));
      else
        strncpy(display_data.air_quality_description, "Very Poor", sizeof(display_data.air_quality_description));

      // Update ENS160 specific data for display
      display_data.eco2 = eco2;
      display_data.tvoc = tvoc;

      // Update SCD41 data if available
      if (scd41_available && co2_ppm != 0) {
        display_data.co2 = co2_ppm;
        // Update CO2 quality description
        strncpy(display_data.co2_quality_description,
                getCO2QualityDescription(co2_ppm),
                sizeof(display_data.co2_quality_description));
      }

      xSemaphoreGive(displayMutex);
    }

// Thread-safe JSON sending
#ifdef ENABLE_MQTT
    logger_send_mqtt_json(&doc, "Enviro", &mqttClient, topic);
#else
    logger_send_json(&doc, "Enviro");
#endif

    doc.clear();

    vTaskDelay(pdMS_TO_TICKS(5000));  // Delay for 5 seconds
  }
}
