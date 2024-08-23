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
 * All contributions to this project are agreed to be licensed under the
 * GPLv3 or any later version. Contributions are understood to be
 * any modifications, enhancements, or additions to the project
 * and become the property of the original author Kris Kersey.
 *
 * This code is a healthy mix of example code from various libraries with a special
 * thanks to Adafruit for their awesome hardware and software.
 *
 * Hardware components currently used:
 * - ESP32-C3
 * - Adafruit ICM26948 IMU
 * - SparkFun u-blox NEO-M9N
 * - SparkFun BME688
 *
 */

// I2C
#include <Wire.h>

// GPS
#include <SparkFun_u-blox_GNSS_Arduino_Library.h> //http://librarymanager/All#SparkFun_u-blox_GNSS

// IMU
#include <Adafruit_ICM20948.h>
#include <Adafruit_ICM20X.h>
#include <Adafruit_AHRS.h>
//#define AHRS_DEBUG_OUTPUT

// Environmental Sensor
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>

// Data
#include <ArduinoJson.h>

// NeoPixel
#include <Adafruit_NeoPixel.h>

// WiFi, MQTT, Sockets
#include <WiFi.h>
//#define ENABLE_MQTT
#ifdef ENABLE_MQTT
#include <ArduinoMqttClient.h>
#endif
#define ENABLE_SOCKET
#ifdef ENABLE_SOCKET
#define SERVER_PORT 3000
#define SOCKET_RECONNECT_TIMEOUT_MS 5000
#endif
#include "arduino_secrets.h"

// General Purpose
#define SERIAL_PRINT(x)    if (Serial) { Serial.print(x); }
#define SERIAL_PRINTLN(x)  if (Serial) { Serial.println(x); }

// I2C pins for ESP32
#define SDA_PIN 3
#define SCL_PIN 4

// GPS
SFE_UBLOX_GNSS myGNSS;

// IMU/AHRS
Adafruit_ICM20948 icm;
Adafruit_Sensor *accelerometer, *gyroscope, *magnetometer, *temperature;
#define FILTER_UPDATE_RATE_HZ 100

// pick your filter! slower == better quality output
Adafruit_NXPSensorFusion filter; // slowest
//Adafruit_Madgwick filter;      // faster than NXP
//Adafruit_Mahony filter;        // fastest/smalleset

// Enviro Sensor
Adafruit_BME680 bme;
#define SEALEVELPRESSURE_HPA (1018)

// NeoPixel
// PIN_NEOPIXEL and NEOPIXEL_POWER are defined
#define NUMPIXELS     1
Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// WiFi and MQTT
#define CONN_RETRY_ATTEMPTS  5
SemaphoreHandle_t clientMutex; // Mutex to protect access to the client
#ifdef ENABLE_SOCKET
IPAddress serverIP;
#endif

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;    // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)

WiFiClient wifiClient;
#ifdef ENABLE_MQTT
MqttClient mqttClient(wifiClient);

String brokerString;
const char *broker = NULL;
int port = 1883;

const char topic[]  = "helmet";
#endif

// Task handles
TaskHandle_t gpsTaskHandle;
TaskHandle_t imuTaskHandle;
TaskHandle_t enviroTaskHandle;

// Function prototypes
void gpsTask(void *pvParameters);
void imuTask(void *pvParameters);
void enviroTask(void *pvParameters);

// GPS task, pinned to Core 0, runs every 1 second
void gpsTask(void *pvParameters) {
  char time[9] = "00:00:00";
  char date[11] = "2024/01/01";
  int32_t altitude_mm = 0;
  int32_t speed_mmps = 0;
  float altitude_feet = 0.0f;
  float speed_mph = 0.0f;
  JsonDocument doc;
  size_t len = 0;
  char output_json[256];

  SERIAL_PRINTLN(F("GPS task started."));

  while (true) {
    if (myGNSS.getTimeValid()) {
      snprintf(time, 9, "%02d:%02d:%02d", myGNSS.getHour(), myGNSS.getMinute(), myGNSS.getSecond());
    } else {
      SERIAL_PRINTLN("Time is not valid.");
    }
    if (myGNSS.getDateValid()) {
      snprintf(date, 11, "%04d/%02d/%02d", myGNSS.getYear(), myGNSS.getMonth(), myGNSS.getDay());
    } else {
      SERIAL_PRINTLN("Date is not valid.");
    }

    altitude_mm = myGNSS.getAltitudeMSL();
    altitude_feet = static_cast<float>(altitude_mm) / 304.8f;

    speed_mmps = myGNSS.getGroundSpeed();
    speed_mph = static_cast<float>(speed_mmps) * 0.00223694f;

    doc["device"] =     "GPS";
    doc["time"] =       time;
    doc["date"] =       date;
    doc["fix"] =        (int)myGNSS.getGnssFixOk();
    if (myGNSS.getGnssFixOk()) {
      doc["latitudeDegrees"] = round(myGNSS.getLatitude() / 10000000.0 * 1e6) / 1e6;
      doc["longitudeDegrees"] = round(myGNSS.getLongitude() / 10000000.0 * 1e6) / 1e6;
      doc["speed"] =      static_cast<int32_t>(speed_mph);
      doc["angle"] =      static_cast<int32_t>(round(myGNSS.getHeading() / 100000.0));
      doc["altitude"] =   static_cast<int32_t>(altitude_feet);
      doc["satellites"] = myGNSS.getSIV();
    }
    if (Serial) serializeJson(doc, Serial);
    SERIAL_PRINTLN();

    len = serializeJson(doc, output_json, 256);

    // Take the mutex before writing to the client
    if (xSemaphoreTake(clientMutex, portMAX_DELAY) == pdTRUE) {
#ifdef ENABLE_MQTT
      // send message, the Print interface can be used to set the message contents
      mqttClient.beginMessage(topic);
      mqttClient.print(output_json);
      mqttClient.endMessage();
#endif

#ifdef ENABLE_SOCKET
      if (wifiClient.connected()) {
        wifiClient.write(output_json, len);
      }
#endif
      xSemaphoreGive(clientMutex); // Release the mutex after writing
    }

    doc.clear();

    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
  }
}

// IMU task, pinned to Core 0, runs every 100 ms
void imuTask(void *pvParameters) {
  sensors_event_t accel, gyro, mag, temp;
  float roll, pitch, heading;
  float gx, gy, gz;
  JsonDocument doc;
  size_t len = 0;
  char output_json[256];

  SERIAL_PRINTLN(F("IMU task started."));

  while (true) {
    /* Read the motion sensors */
    accelerometer->getEvent(&accel);
    gyroscope->getEvent(&gyro);
    magnetometer->getEvent(&mag);
    temperature->getEvent(&temp);
#if defined(AHRS_DEBUG_OUTPUT)
    SERIAL_PRINT("I2C took "); SERIAL_PRINT(millis()-timestamp); SERIAL_PRINTLN(" ms");
#endif

    /* Calculate motion information */
#if 0
    cal.calibrate(mag);
    cal.calibrate(accel);
    cal.calibrate(gyro);
#endif
    // Gyroscope needs to be converted from Rad/s to Degree/s
    // the rest are not unit-important
    gx = gyro.gyro.x * SENSORS_RADS_TO_DPS;
    gy = gyro.gyro.y * SENSORS_RADS_TO_DPS;
    gz = gyro.gyro.z * SENSORS_RADS_TO_DPS;

    // Update the SensorFusion filter
    filter.update(gx, gy, gz, 
                  accel.acceleration.x, accel.acceleration.y, accel.acceleration.z, 
                  mag.magnetic.x, mag.magnetic.y, mag.magnetic.z);
#if defined(AHRS_DEBUG_OUTPUT)
    SERIAL_PRINT("Update took "); SERIAL_PRINT(millis()-timestamp); SERIAL_PRINTLN(" ms");
#endif

    // get the heading, pitch and roll
    roll = filter.getRoll();
    pitch = filter.getPitch();
    heading = filter.getYaw();

    doc["device"] = "Motion";
    doc["format"] = "Orientation";
    // The following will remap the compass in case it's backwards.
    //doc["heading"] = map(heading, 0, 360, 360, 0);
    doc["heading"] = heading;
    doc["pitch"] = -1.0f * pitch;
    doc["roll"] = roll;
    if (Serial) serializeJson(doc, Serial);
    SERIAL_PRINTLN();

    len = serializeJson(doc, output_json, 256);

#ifdef ENABLE_MQTT
    // send message, the Print interface can be used to set the message contents
    mqttClient.beginMessage(topic);
    mqttClient.print(output_json);
    mqttClient.endMessage();
#endif

#ifdef ENABLE_SOCKET
    if (wifiClient.connected()) {
      wifiClient.write(output_json, len);
    }
#endif

    doc.clear();

    vTaskDelay(pdMS_TO_TICKS(1000 / FILTER_UPDATE_RATE_HZ)); // Delay for 100 ms
  }
}

// Environmental sensor task (BME680), pinned to Core 1, runs every 5 seconds
void enviroTask(void *pvParameters) {
  float temp2;
  //uint32_t pressure;
  float humidity;
  //uint32_t gas_resistance;
  //float altitude;
  uint32_t timestamp = 0;
  unsigned long bmeEndTime = 0L;
  JsonDocument doc;
  size_t len = 0;
  char output_json[256];

  SERIAL_PRINTLN(F("Environmental task started."));

  while (true) {
    timestamp = millis();

    /* Read environmental sensors */
    if (bmeEndTime == 0) {
      bmeEndTime = bme.beginReading();
      if (bmeEndTime == 0) {
        SERIAL_PRINTLN(F("Failed to begin reading :("));
      } else {
        bmeEndTime += 5000; // I add padding here so we don't block on the reading.
      }
    }

    if (bmeEndTime != 0 && timestamp > bmeEndTime) {
      SERIAL_PRINTLN("Reading BME");
      if (bme.endReading()) {
        temp2 = bme.temperature; // C
        //pressure = bme.pressure / 100.0; // hPa
        humidity = bme.humidity; // %
        //gas_resistance = bme.gas_resistance / 1000.0; // KOhms
        //altitude = bme.readAltitude(SEALEVELPRESSURE_HPA); // m

        bmeEndTime = 0L;
      }

      /* From dedicated sensor */
      doc["device"] = "Enviro";
      doc["temp"] = temp2;
      doc["humidity"] = humidity;
      if (Serial) serializeJson(doc, Serial);
      SERIAL_PRINTLN();

      len = serializeJson(doc, output_json, 256);

  #ifdef ENABLE_MQTT
      // send message, the Print interface can be used to set the message contents
      mqttClient.beginMessage(topic);
      mqttClient.print(output_json);
      mqttClient.endMessage();
  #endif

  #ifdef ENABLE_SOCKET
    if (wifiClient.connected()) {
      wifiClient.write(output_json, len);
    }
  #endif

      doc.clear();
    }

    vTaskDelay(pdMS_TO_TICKS(5000)); // Delay for 5 seconds
  }
}

#ifdef ENABLE_SOCKET
bool reconnect() {
    unsigned long startAttemptTime = millis();

    while (!wifiClient.connect(serverIP, SERVER_PORT)) {
        if (millis() - startAttemptTime > SOCKET_RECONNECT_TIMEOUT_MS) {
            Serial.println("Reconnection timeout reached, giving up.");
            pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Set to green on failure
            pixels.show();
            return false; // Return false if reconnection fails within the timeout period
        }

        Serial.println("Connection failed, retrying...");
        delay(1000); // Wait 1 second before retrying
    }

    Serial.println("Reconnected to server");
    pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Set to blue on success
    pixels.show();
    return true; // Return true if reconnection is successful
}
#endif

void setup() {
  unsigned long startTime = millis();
  int wifiRetries = 0;

  Serial.begin(115200);
  while (!Serial && (millis() - startTime < 2000)) { // Wait up to 2 seconds for Serial to connect
    delay(10);
  }

  SERIAL_PRINTLN("AURA Initialized. Beginning power on sequence...");

  // NeoPixel
  pixels.begin();
  pixels.clear();
  
  // Initialize I2C with specified pins and speed
  if (!Wire.begin(SDA_PIN, SCL_PIN, 350000)) {
    SERIAL_PRINTLN("I2C initialization failed!");
  } else {
    SERIAL_PRINTLN("I2C initialization success!");
  }
  
  // IMU
  if (!icm.begin_I2C()) {
    SERIAL_PRINTLN("Failed to find ICM20948 chip");
    while (1) {
      delay(10);
    }
  }

  SERIAL_PRINTLN("ICM20948 Found!");
  temperature = icm.getTemperatureSensor();
  temperature->printSensorDetails();

  accelerometer = icm.getAccelerometerSensor();
  accelerometer->printSensorDetails();

  gyroscope = icm.getGyroSensor();
  gyroscope->printSensorDetails();

  magnetometer = icm.getMagnetometerSensor();
  magnetometer->printSensorDetails();

  filter.begin(FILTER_UPDATE_RATE_HZ);

  // GPS
  if (myGNSS.begin() == false)
  {
    SERIAL_PRINTLN(F("u-blox GNSS not detected at default I2C address."));
  } else {
    SERIAL_PRINTLN("GPS found!");
  }

  myGNSS.setI2COutput(COM_TYPE_UBX); //Set the I2C port to output only UBX messages

  SERIAL_PRINTLN("GPS initialized.");

  // Enviro
  if (!bme.begin(0x76, true)) {
    SERIAL_PRINTLN("Could not find a valid BME688 sensor, check wiring!");
  }
  SERIAL_PRINTLN("BME688 found!");

  // Set up oversampling and filter initialization
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms

  pixels.setPixelColor(0, pixels.Color(255, 0, 0));
  pixels.show();

  // attempt to connect to WiFi network:
  SERIAL_PRINT("Attempting to connect to WPA SSID: ");
  SERIAL_PRINTLN(ssid);
  WiFi.begin(ssid, pass);
  while ((WiFi.status() != WL_CONNECTED) && (wifiRetries < CONN_RETRY_ATTEMPTS)) {
    // failed, retry
    SERIAL_PRINT(".");
    delay(5000);
    wifiRetries++;
  }

  if (wifiRetries == CONN_RETRY_ATTEMPTS) {
    SERIAL_PRINTLN("Retry timeout.");

    goto start_threads;
  } else {
    pixels.setPixelColor(0, pixels.Color(0, 255, 0));
    pixels.show();
  }
  
  SERIAL_PRINTLN("WiFi connected");
  SERIAL_PRINTLN("IP address: ");
  SERIAL_PRINTLN(WiFi.localIP());

  SERIAL_PRINT("GATEWAY: ");
  SERIAL_PRINTLN(WiFi.gatewayIP());

#ifdef ENABLE_MQTT
  // You can provide a unique client ID, if not set the library uses Arduino-millis()
  // Each client must have a unique client ID
  // mqttClient.setId("clientId");

  // You can provide a username and password for authentication
  // mqttClient.setUsernamePassword("username", "password");

  brokerString = WiFi.gatewayIP().toString();
  broker = brokerString.c_str();

  SERIAL_PRINT("Attempting to connect to the MQTT broker: ");
  SERIAL_PRINTLN(broker);

  if (!mqttClient.connect(broker, port)) {
    SERIAL_PRINT("MQTT connection failed! Error code = ");
    SERIAL_PRINTLN(mqttClient.connectError());

    goto start_threads;
  } else {
    SERIAL_PRINTLN("You're connected to the MQTT broker!");
    SERIAL_PRINTLN();

    pixels.setPixelColor(0, pixels.Color(0, 0, 255));
    pixels.show();
  }

  mqttClient.setKeepAliveInterval(5);
  mqttClient.setCleanSession(true);
  //mqttClient.setMaxPacketSize(512);
  //mqttClient.setQos(0);

  SERIAL_PRINT("Subscribing to topic: ");
  SERIAL_PRINTLN(topic);
  SERIAL_PRINTLN();

  // subscribe to a topic
  // mqttClient.subscribe(topic);

  // topics can be unsubscribed using:
  // mqttClient.unsubscribe(topic);

  // Serial.print("Waiting for messages on topic: ");
  // Serial.println(topic);
  // Serial.println();
#endif

#ifdef ENABLE_SOCKET
  serverIP = WiFi.gatewayIP();

  // Connect to the server
  reconnect();
#endif

start_threads:
  // Create the mutex before starting any tasks
  clientMutex = xSemaphoreCreateMutex();
  if (clientMutex == NULL) {
    Serial.println("Failed to create mutex");
    while (1); // Halt further execution
  }

  // Create tasks
  if (xTaskCreatePinnedToCore(
      gpsTask,             // Task function
      "GPS Task",          // Name of the task
      10000,               // Stack size (in words)
      NULL,                // Task input parameter
      1,                   // Priority of the task
      &gpsTaskHandle,      // Task handle
      0                    // Core 0
  ) != pdPASS) {
    SERIAL_PRINTLN("Failed to create GPS Task");
  }

  if (xTaskCreatePinnedToCore(
      imuTask,             // Task function
      "IMU Task",          // Name of the task
      10000,               // Stack size (in words)
      NULL,                // Task input parameter
      1,                   // Priority of the task
      &imuTaskHandle,      // Task handle
      0                    // Core 0
  ) != pdPASS) {
    SERIAL_PRINTLN("Failed to create IMU Task");
  }

  if (xTaskCreatePinnedToCore(
      enviroTask,             // Task function
      "Environmental Task",          // Name of the task
      10000,               // Stack size (in words)
      NULL,                // Task input parameter
      1,                   // Priority of the task
      &enviroTaskHandle,      // Task handle
      1                    // Core 1
  ) != pdPASS) {
    SERIAL_PRINTLN("Failed to create Environmental Task");
  }
}

void loop() {
#ifdef ENABLE_SOCKET
  if (!wifiClient.connected()) {
    pixels.setPixelColor(0, pixels.Color(0, 255, 0));
    pixels.show();

    // Attempt to reconnect if the client is disconnected
    if (!reconnect()) {
      // Handle failure to reconnect, if necessary
      Serial.println("Failed to reconnect. Entering an error state.");
    }
  }
#endif

    delay(1000); // Prevent the loop from running too fast
}

