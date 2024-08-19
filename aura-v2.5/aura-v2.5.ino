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
#include <MicroNMEA.h> //http://librarymanager/All#MicroNMEA

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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// I2C pins for ESP32
#define SDA_PIN 3
#define SCL_PIN 4

// GPS
SFE_UBLOX_GNSS myGNSS;
char nmeaBuffer[100];
MicroNMEA nmea(nmeaBuffer, sizeof(nmeaBuffer));

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
  char time[9] = "";
  char date[11] = "";
  long altitude = 0L;

  /* Misc. Vars */
  JsonDocument doc;

  Serial.println(F("GPS task started."));

  while (true) {
    myGNSS.checkUblox();

    snprintf(time, 9, "%02d:%02d:%02d", nmea.getHour(), nmea.getMinute(), nmea.getSecond());
    snprintf(date, 11, "20%02d/%02d/%02d", nmea.getYear(), nmea.getMonth(), nmea.getDay());

    nmea.getAltitude(altitude);

#if 0
    if (nmea.isValid()) {
      //display.print("Location: ");
      display.print(nmea.getLatitude()/1000000.0, 6);
      display.print(", ");
      display.print(nmea.getLongitude()/1000000.0, 6);
      //display.print("Speed (knots): "); display.println(GPS.speed);
      //display.print("Angle: "); display.println(GPS.angle);
      display.print("Altitude: "); display.println(altitude);
      display.print("Satellites: "); display.println((int)nmea.getNumSatellites());
    }
#endif

    doc["device"] =     "GPS";
    doc["time"] =       time;
    doc["date"] =       date;
    doc["fix"] =        (int)nmea.isValid();
    if (nmea.isValid()) {
      //doc["quality"] =    GPS.fixquality;
      //doc["latitude"] =   GPS.latitude;
      doc["latitudeDegrees"] = nmea.getLatitude()/1000000.0;
      //doc["lat"] =        lat;
      //doc["longitude"] =  GPS.longitude;
      doc["longitudeDegrees"] = nmea.getLongitude()/1000000.0;
      //doc["lon"] =        lon;
      doc["speed"] =      nmea.getSpeed();
      doc["angle"] =      nmea.getCourse();
      doc["altitude"] =   altitude;
      doc["satellites"] = nmea.getNumSatellites();
    }
    serializeJson(doc, Serial);
    Serial.println();
    doc.clear();

    nmea.clear();

    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
  }
}

// IMU task, pinned to Core 0, runs every 100 ms
void imuTask(void *pvParameters) {
  sensors_event_t accel, gyro, mag, temp;
  float roll, pitch, heading;
  float gx, gy, gz;
  JsonDocument doc;

  Serial.println(F("IMU task started."));

  while (true) {
    /* Read the motion sensors */
    accelerometer->getEvent(&accel);
    gyroscope->getEvent(&gyro);
    magnetometer->getEvent(&mag);
    temperature->getEvent(&temp);
#if defined(AHRS_DEBUG_OUTPUT)
    Serial.print("I2C took "); Serial.print(millis()-timestamp); Serial.println(" ms");
#endif

#if 0
    Serial.print("\t\tTemperature ");
    Serial.print(temp.temperature);
    Serial.println(" deg C");

    /* Display the results (acceleration is measured in m/s^2) */
    Serial.print("\t\tAccel X: ");
    Serial.print(accel.acceleration.x);
    Serial.print(" \tY: ");
    Serial.print(accel.acceleration.y);
    Serial.print(" \tZ: ");
    Serial.print(accel.acceleration.z);
    Serial.println(" m/s^2 ");

    /* Display the results (rotation is measured in rad/s) */
    Serial.print("\t\tGyro X: ");
    Serial.print(gyro.gyro.x);
    Serial.print(" \tY: ");
    Serial.print(gyro.gyro.y);
    Serial.print(" \tZ: ");
    Serial.print(gyro.gyro.z);
    Serial.println(" radians/s ");
    Serial.println();

    Serial.print("\t\tMag X: ");
    Serial.print(mag.magnetic.x);
    Serial.print(" \tY: ");
    Serial.print(mag.magnetic.y);
    Serial.print(" \tZ: ");
    Serial.print(mag.magnetic.z);
    Serial.println(" uT");
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
    Serial.print("Update took "); Serial.print(millis()-timestamp); Serial.println(" ms");
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
    doc["pitch"] = pitch;
    doc["roll"] = roll;
    serializeJson(doc, Serial);
    Serial.println();
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

  Serial.println(F("Environmental task started."));

  while (true) {
    timestamp = millis();

    /* Read environmental sensors */
    if (bmeEndTime == 0) {
      bmeEndTime = bme.beginReading();
      if (bmeEndTime == 0) {
        Serial.println(F("Failed to begin reading :("));
      } else {
        bmeEndTime += 5000; // I add padding here so we don't block on the reading.
      }
    }

    if (bmeEndTime != 0 && timestamp > bmeEndTime) {
      Serial.println("Reading BME");
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
      serializeJson(doc, Serial);
      Serial.println();
      doc.clear();
    }

    vTaskDelay(pdMS_TO_TICKS(5000)); // Delay for 5 seconds
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }

  Serial.println("AURA Initialized. Beginning power on sequence...");
  
  // Initialize I2C with specified pins and speed
  if (!Wire.begin(SDA_PIN, SCL_PIN, 350000)) {
    Serial.println("I2C initialization failed!");
  } else {
    Serial.println("I2C initialization success!");
  }
  
  // IMU
  if (!icm.begin_I2C()) {
    Serial.println("Failed to find ICM20948 chip");
    while (1) {
      delay(10);
    }
  }

  Serial.println("ICM20948 Found!");
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
    Serial.println(F("u-blox GNSS not detected at default I2C address."));
  } else {
    Serial.println("GPS found!");
  }

  myGNSS.setI2COutput(COM_TYPE_UBX | COM_TYPE_NMEA); //Set the I2C port to output both NMEA and UBX messages
  myGNSS.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT); //Save (only) the communications port settings to flash and BBR

  myGNSS.setProcessNMEAMask(SFE_UBLOX_FILTER_NMEA_ALL); // Make sure the library is passing all NMEA messages to processNMEA

  myGNSS.setProcessNMEAMask(SFE_UBLOX_FILTER_NMEA_GGA); // Or, we can be kind to MicroNMEA and _only_ pass the GGA messages to it

  Serial.println("GPS initialized.");

  // Enviro
  if (!bme.begin(0x76, true)) {
    Serial.println("Could not find a valid BME688 sensor, check wiring!");
  }
  Serial.println("BME688 found!");

  // Set up oversampling and filter initialization
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms

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
    Serial.println("Failed to create GPS Task");
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
    Serial.println("Failed to create IMU Task");
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
    Serial.println("Failed to create Environmental Task");
  }
}

void loop() {
  // No need for code here; tasks handle everything
}

//This function gets called from the SparkFun u-blox Arduino Library
//As each NMEA character comes in you can specify what to do with it
//Useful for passing to other libraries like tinyGPS, MicroNMEA, or even
//a buffer, radio, etc.
void SFE_UBLOX_GNSS::processNMEA(char incoming)
{
  //Take the incoming char from the u-blox I2C port and pass it on to the MicroNMEA lib
  //for sentence cracking
  nmea.process(incoming);
}
