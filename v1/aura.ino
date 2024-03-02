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
 * - Teensy 4.0
 * - Adafruit LSM6SDSOX+LIS3MDL (http://adafru.it/4517)
 * - Adafruit Mini GPS (http://adafru.it/4415)
 * - Adafruit HTS221 Humidity Sensor (http://adafru.it/4535)
 * - 128x64 OLED Display (http://adafru.it/326)
 *
 */

#include <SPI.h>
#include <Wire.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor_Calibration.h>
#include <Adafruit_AHRS.h>
#include <Adafruit_GPS.h>
#include <Adafruit_HTS221.h>
#include <PWMServo.h>
#include <Bounce2.h>
#include "LSM6DS_LIS3MDL.h"
#include "Adafruit_seesaw.h"

#include <ArduinoJson.h>

/* PINS */
#define POWER_PIN   13

#define BUTTONA_PIN 8
#define SERVOA_PIN  10
#define SERVOB_PIN  12
#define LEDA_PIN    A0
#define LEDB_PIN    A1

int open_angle   =  80;
int closed_angle =  20;

#define LED_POWER   255

#define SS_SWITCH   24
#define SEESAW_ADDR 0x36

//#define DEBUG_BUNNY /* For use with Adafruit 3D Model Viewer
// https://adafruit.github.io/Adafruit_WebSerial_3DModelViewer/ */

// GPS
// Connect to the GPS on the hardware I2C port
Adafruit_GPS GPS(&Wire);

// Set GPSECHO to 'false' to turn off echoing the GPS data to the Serial console
// Set to 'true' if you want to debug and listen to the raw GPS sentences
#define GPSECHO false



// Display (SSD1306)
#define SCREEN_WIDTH    128 // OLED display width, in pixels
#define SCREEN_HEIGHT   64 // OLED display height, in pixels

#define OLED_RESET      -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS  0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);



// AHRS
Adafruit_Sensor *accelerometer, *gyroscope, *magnetometer, *temperature;

// pick your filter! slower == better quality output
Adafruit_NXPSensorFusion filter; // slowest
//Adafruit_Madgwick filter;      // faster than NXP
//Adafruit_Mahony filter;        // fastest/smalleset

#if defined(ADAFRUIT_SENSOR_CALIBRATION_USE_EEPROM)
Adafruit_Sensor_Calibration_EEPROM cal;
#else
Adafruit_Sensor_Calibration_SDFat cal;
#endif

#define FILTER_UPDATE_RATE_HZ 100
#define PRINT_EVERY_N_UPDATES 5
//#define AHRS_DEBUG_OUTPUT

uint32_t timestamp;
uint8_t counter = 0;

// Enviro Sensor
Adafruit_HTS221 hts;
Adafruit_Sensor *hts_humidity, *hts_temp;

// Servos
PWMServo servoA, servoB;

// Buttons
Bounce debouncer = Bounce();

// Adafruit Seesaw
Adafruit_seesaw ss;
int seesaw_found = 0;
int32_t encoder_position;

int mask_state = 1;
int button_state = 0, last_state = 0;

int power_led_state = 1;

void setup()
{
   Serial.begin(115200);
   //while (!Serial) yield();

   // Display
   // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
   if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
      Serial.println(F("SSD1306 allocation failed"));
      for(;;); // Don't proceed, loop forever
   }

   // Clear the buffer
   display.clearDisplay();
   display.setTextSize(1);      // Normal 1:1 pixel scale
   display.setTextColor(SSD1306_WHITE); // Draw white text
   display.setCursor(0, 0);     // Start at top-left corner
   display.cp437(true);         // Use full 256 char 'Code Page 437' font
   display.println("Ready to display data...");
   display.display();

   // Accel/Gyro/Mag
   if (!cal.begin()) {
      Serial.println("Failed to initialize calibration helper");
   } else if (! cal.loadCalibration()) {
      Serial.println("No calibration loaded/found");
   } else {
      Serial.println("Calibrations loaded succesfully.");
   }

   if (!init_sensors(&accelerometer, &gyroscope, &magnetometer, &temperature)) {
      Serial.println("Failed to find sensors");
      while (1) delay(10);
   } else {
      Serial.println("Sensors initialized.");
   }
  
   accelerometer->printSensorDetails();
   gyroscope->printSensorDetails();
   magnetometer->printSensorDetails();

   setup_sensors();
   filter.begin(FILTER_UPDATE_RATE_HZ);
   timestamp = millis();

   Wire.setClock(400000); // 400KHz

   // GPS
   // 9600 NMEA is the default baud rate for Adafruit MTK GPS's- some use 4800
   GPS.begin(0x10);  // The I2C address to use is 0x10
   // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
   GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
   // uncomment this line to turn on only the "minimum recommended" data
   //GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
   // For parsing data, we don't suggest using anything but either RMC only or RMC+GGA since
   // the parser doesn't care about other sentences at this time
   // Set the update rate
   GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ); // 1 Hz update rate
   // For the parsing code to work nicely and have time to sort thru the data, and
   // print it out we don't suggest using anything higher than 1 Hz

   // Request updates on antenna status, comment out to keep quiet
   GPS.sendCommand(PGCMD_ANTENNA);

   delay(1000);

   // Ask for firmware version
   GPS.println(PMTK_Q_RELEASE);

   Serial.println("GPS initialized.");


   // Enviro
   if (!hts.begin_I2C()) {
      Serial.println("Failed to find HTS221 chip");
      while (1) {
         delay(10);
      }
   }

   Serial.println("HTS221 Found!");
   hts_temp = hts.getTemperatureSensor();
   hts_temp->printSensorDetails();

   hts_humidity = hts.getHumiditySensor();
   hts_humidity->printSensorDetails();

   // Servo
   servoA.attach(SERVOA_PIN);
   servoB.attach(SERVOB_PIN);

   pinMode(BUTTONA_PIN, INPUT_PULLUP);
   debouncer.attach(BUTTONA_PIN);
   debouncer.interval(10);

   // Initial State for LEDs and Servos
   if (mask_state) {
      servoA.write(closed_angle);
      servoB.write(map(closed_angle, 0, 180, 180, 0));

      delay(1000);

      analogWrite(LEDA_PIN, LED_POWER);
      analogWrite(LEDB_PIN, LED_POWER);
   } else {
      analogWrite(LEDA_PIN, 0);
      analogWrite(LEDB_PIN, 0);

      delay(500);

      servoA.write(open_angle);
      servoB.write(map(open_angle, 0, 180, 180, 0));

      delay(1000);
   }

   // SEESAW INIT
   if (! ss.begin(SEESAW_ADDR)) {
      Serial.println("Couldn't find seesaw on default address");
      seesaw_found = 0;
   } else {
      seesaw_found = 1;
      Serial.println("Seesaw started");
   }

   if (seesaw_found) {
      uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
      if (version  != 4991){
         Serial.print("Wrong firmware loaded? ");
         Serial.println(version);
         seesaw_found = 0;
      } else {
         Serial.println("Found Product 4991");
      }
   }

   if (seesaw_found) {
      // use a pin for the built in encoder switch
      ss.pinMode(SS_SWITCH, INPUT_PULLUP);

      // get starting position
      encoder_position = ss.getEncoderPosition();

      Serial.println("Turning on interrupts");
      delay(10);
      ss.setGPIOInterrupts((uint32_t)1 << SS_SWITCH, 1);
      ss.enableEncoderInterrupt();
   }
   // END SEESAW
  
   pinMode(13, OUTPUT);
   digitalWrite(POWER_PIN, power_led_state);
   Serial.println("Ready to go!");
}

void loop() // run over and over again
{
   /* AHRS Vars */
   sensors_event_t accel, gyro, mag, temp;
   float roll, pitch, heading;
   float gx, gy, gz;

   /* Enviro Vars */
   sensors_event_t humidity, temp2;

   /* GPS Vars */
   char c;
   char time[9] = "";
   char date[11] = "";
   char lat[2] = "";
   char lon[2] = "";

   /* Misc. Vars */
   static StaticJsonDocument<256> doc;

   /* This came from example code to throttle reading. */
   if ((millis() - timestamp) < (1000 / FILTER_UPDATE_RATE_HZ)) {
      return;
   }
   timestamp = millis();

   /* Read environmental sensors */
   hts_humidity->getEvent(&humidity);
   hts_temp->getEvent(&temp2);

   /* Read the motion sensors */
   accelerometer->getEvent(&accel);
   gyroscope->getEvent(&gyro);
   magnetometer->getEvent(&mag);
   temperature->getEvent(&temp);
#if defined(AHRS_DEBUG_OUTPUT)
   Serial.print("I2C took "); Serial.print(millis()-timestamp); Serial.println(" ms");
#endif

   /* Calculate motion information */
   cal.calibrate(mag);
   cal.calibrate(accel);
   cal.calibrate(gyro);
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

   /* Read GPS infomation */
   c = GPS.read();
   // Debug GPS reads...
   if (GPSECHO)
      if (c) Serial.print(c);
   // if a sentence is received, we can check the checksum, parse it...
   if (GPS.newNMEAreceived()) {
      // a tricky thing here is if we print the NMEA sentence, or data
      // we end up not listening and catching other sentences!
      // so be very wary if using OUTPUT_ALLDATA and trying to print out data
      //Serial.println(GPS.lastNMEA()); // this also sets the newNMEAreceived() flag to false
      if (!GPS.parse(GPS.lastNMEA())) // this also sets the newNMEAreceived() flag to false
         return; // we can fail to parse a sentence in which case we should just wait for another
   }

   // only print the calculated output once in a while
   if (counter++ <= PRINT_EVERY_N_UPDATES) {
      return;
   }
   // reset the counter
   counter = 0;

   // reset the display
   display.clearDisplay();
   display.setCursor(0, 0);

   snprintf(time, 9, "%02d:%02d:%02d", GPS.hour, GPS.minute, GPS.seconds);
   snprintf(date, 11, "20%02d/%02d/%02d", GPS.year, GPS.month, GPS.day);
   snprintf(lat, 2, "%c", GPS.lat);
   snprintf(lon, 2, "%c", GPS.lon);

   display.print("Time: ");
   display.println(time);

   display.print("Date: ");
   display.println(date);

   display.print("Fix: "); display.print((int)GPS.fix);
   display.print(" quality: "); display.println((int)GPS.fixquality);
#if 0
   if (GPS.fix) {
      //display.print("Location: ");
      display.print(GPS.latitude/100, 2); display.print(GPS.lat);
      display.print(", ");
      display.print(GPS.longitude/100, 2); display.println(GPS.lon);
      //display.print("Speed (knots): "); display.println(GPS.speed);
      //display.print("Angle: "); display.println(GPS.angle);
      display.print("Altitude: "); display.println(GPS.altitude);
      display.print("Satellites: "); display.println((int)GPS.satellites);
   }
#endif

   doc["device"] =     "GPS";
   doc["time"] =       time;
   doc["date"] =       date;
   doc["fix"] =        (int)GPS.fix;
   if (GPS.fix) {
      doc["quality"] =    GPS.fixquality;
      doc["latitude"] =   GPS.latitude;
      doc["latitudeDegrees"] = GPS.latitudeDegrees;
      doc["lat"] =        lat;
      doc["longitude"] =  GPS.longitude;
      doc["longitudeDegrees"] = GPS.longitudeDegrees;
      doc["lon"] =        lon;
      doc["speed"] =      GPS.speed;
      doc["angle"] =      GPS.angle;
      doc["altitude"] =   GPS.altitude;
      doc["satellites"] = GPS.satellites;
   }
   serializeJson(doc, Serial);
   Serial.println();
   doc.clear();

   // get the heading, pitch and roll
   roll = filter.getRoll();
   pitch = filter.getPitch();
   heading = filter.getYaw();

   display.print(roll);
   display.print(" ");
   display.print(pitch);
   display.print(" ");
   display.println(heading);

   doc["device"] = "Motion";
   doc["format"] = "Orientation";
   // The following will remap the compass in case it's backwards.
   //doc["heading"] = map(heading, 0, 360, 360, 0);
   doc["heading"] = heading;
   doc["pitch"] = -1.0f * pitch;
   doc["roll"] = roll;
   serializeJson(doc, Serial);
   Serial.println();
   doc.clear();

#if 0 /* Also suppressing this since we're using angles. */
   float qw, qx, qy, qz;
   filter.getQuaternion(&qw, &qx, &qy, &qz);

   doc["device"] = "Motion";
   doc["format"] = "Quaternion";
   doc["w"] = qw;
   doc["x"] = qx;
   doc["y"] = qy;
   doc["z"] = qz;
   serializeJson(doc, Serial);
   Serial.println();
   doc.clear();
#endif

   /* From motion sensor */
#if 0 /* suppressing for now */
   doc["device"] = "Enviro";
   doc["temp"] = temp.temperature;
   serializeJson(doc, Serial);
   Serial.println();
   doc.clear();
#endif

   /* From dedicated sensor */
   doc["device"] = "Enviro";
   doc["temp"] = temp2.temperature;
   doc["humidity"] = humidity.relative_humidity;
   serializeJson(doc, Serial);
   Serial.println();
   doc.clear();

   display.print("Temp: ");
   display.println(temp2.temperature, 2);
   display.print("Humidity: ");
   display.println(humidity.relative_humidity, 2);

#if 0 /* There's really no room for this on one display. */
   display.print("O: ");
   display.print(heading, 1);
   display.print(", ");
   display.print(pitch, 1);
   display.print(", ");
   display.println(roll, 1);

   display.print("Q: ");
   display.print(qw, 2);
   display.print(", ");
   display.print(qx, 2);
   display.print(", ");
   display.print(qy, 2);
   display.print(", ");
   display.println(qz, 2);
#endif

#ifdef DEBUG_BUNNY
   Serial.print("Orientation: ");
   Serial.print(heading);
   Serial.print(", ");
   Serial.print(pitch);
   Serial.print(", ");
   Serial.println(roll);

#if 0
   Serial.print("Quaternion: ");
   Serial.print(qw, 4);
   Serial.print(", ");
   Serial.print(qx, 4);
   Serial.print(", ");
   Serial.print(qy, 4);
   Serial.print(", ");
   Serial.println(qz, 4);
#endif
#endif

   if (mask_state == 1) {
      display.print("* ");
   }
   display.print("Closed: ");
   display.println(closed_angle);

   if (mask_state == 0) {
      display.print("* ");
   }
   display.print("Open: ");
   display.println(open_angle);

   display.display();

   if (seesaw_found) {
      if (! ss.digitalRead(SS_SWITCH)) {
         Serial.println("Button pressed!");
      }

      int32_t new_position = ss.getEncoderPosition();
      // did we move arounde?
      if (encoder_position != new_position) {
         Serial.println(new_position);         // display new position

         if (new_position < encoder_position) {
            if (mask_state) {
               closed_angle--;
               if (closed_angle < 0) {
                  closed_angle = 0;
               }
            } else {
               open_angle--;
               if (open_angle < 0) {
                  open_angle = 0;
               }
            }
         }

         if (new_position > encoder_position) {
            if (mask_state) {
               closed_angle++;
               if (closed_angle > 180) {
                  closed_angle = 180;
               }
            } else {
               open_angle++;
               if (open_angle > 180) {
                  open_angle = 180;
               }
            }
         }

         if (mask_state) {
            servoA.write(closed_angle);
            servoB.write(map(closed_angle, 0, 180, 180, 0));
         } else {
            servoA.write(open_angle);
            servoB.write(map(open_angle, 0, 180, 180, 0));
         }

         encoder_position = new_position;      // and save for next round
      }
   }

   debouncer.update();

   button_state = debouncer.read();

   if (button_state != last_state) {
      last_state = button_state;
      if (button_state == LOW) {
         mask_state = !mask_state;

         if (mask_state) {
            servoA.write(closed_angle);
            servoB.write(map(closed_angle, 0, 180, 180, 0));

            delay(1000);

            analogWrite(LEDA_PIN, LED_POWER);
            analogWrite(LEDB_PIN, LED_POWER);
         } else {
            analogWrite(LEDA_PIN, 0);
            analogWrite(LEDB_PIN, 0);

            delay(500);

            servoA.write(open_angle);
            servoB.write(map(open_angle, 0, 180, 180, 0));

            delay(1000);
         }
      }
   }

   power_led_state = !power_led_state;
   digitalWrite(POWER_PIN, power_led_state);
}
