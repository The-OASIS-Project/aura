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
 * - SparkFun BNO086 IMU
 * - SparkFun u-blox NEO-M9N
 * - SparkFun BME688
 * - 128x64 OLED Display (http://adafru.it/326)
 *
 */
// I2C
#include <Wire.h>

// Display
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor_Calibration.h>

// GPS
#include <SparkFun_u-blox_GNSS_Arduino_Library.h> //http://librarymanager/All#SparkFun_u-blox_GNSS
SFE_UBLOX_GNSS myGNSS;

#include <MicroNMEA.h> //http://librarymanager/All#MicroNMEA
char nmeaBuffer[100];
MicroNMEA nmea(nmeaBuffer, sizeof(nmeaBuffer));

// Environmental Sensor
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>

// IMU
#include <Adafruit_ICM20948.h>
#include <Adafruit_ICM20X.h>
#include <Adafruit_AHRS.h>
//#define AHRS_DEBUG_OUTPUT

// Helmet Operation
//#include <PWMServo.h>
#include <ESP32Servo.h>
#include <Bounce2.h>
#include <Adafruit_seesaw.h>

// Data
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

#if 0
#define SDA_PIN 5
#define SCL_PIN 6
#else
#define SDA_PIN 3
#define SCL_PIN 4
#endif

//#define DEBUG_BUNNY /* For use with Adafruit 3D Model Viewer
// https://adafruit.github.io/Adafruit_WebSerial_3DModelViewer/ */

// Display (SSD1306)
#define SCREEN_WIDTH    128 // OLED display width, in pixels
#define SCREEN_HEIGHT   64 // OLED display height, in pixels

#define OLED_RESET      -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS  0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#if 0
#if defined(ADAFRUIT_SENSOR_CALIBRATION_USE_EEPROM)
Adafruit_Sensor_Calibration_EEPROM cal;
#else
Adafruit_Sensor_Calibration_SDFat cal;
#endif
#endif

#define FILTER_UPDATE_RATE_HZ 100
#define PRINT_EVERY_N_UPDATES 10
//#define AHRS_DEBUG_OUTPUT

uint32_t timestamp = 0;
uint8_t counter = 0;

// IMU/AHRS
Adafruit_ICM20948 icm;
Adafruit_Sensor *accelerometer, *gyroscope, *magnetometer, *temperature;

// pick your filter! slower == better quality output
Adafruit_NXPSensorFusion filter; // slowest
//Adafruit_Madgwick filter;      // faster than NXP
//Adafruit_Mahony filter;        // fastest/smalleset

// Enviro Sensor
Adafruit_BME680 bme;
#define SEALEVELPRESSURE_HPA (1018)

// Servos
Servo servoA, servoB;

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

  Wire.begin(SDA_PIN, SCL_PIN, 350000);

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

  // Servo
  Serial.println("Attaching servos");
  servoA.attach(SERVOA_PIN);
  servoB.attach(SERVOB_PIN);

  pinMode(BUTTONA_PIN, INPUT_PULLUP);
  debouncer.attach(BUTTONA_PIN);
  debouncer.interval(10);

  // Initial State for LEDs and Servos
  Serial.println("Initial states for LEDs and servos");
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
  Serial.println("Attempting to setup SeeSaw");
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
  
#if 0 // TODO: Determine new power LED.
  pinMode(13, OUTPUT);
  digitalWrite(POWER_PIN, power_led_state);
#endif
  Serial.println("Ready to go!");
}

unsigned long bmeEndTime = 0L;
uint32_t gpsNextTime = 0;

void loop() // run over and over again
{
  /* IMU Vars */
  sensors_event_t accel, gyro, mag, temp;
  float roll, pitch, heading;
  float gx, gy, gz;

  /* Enviro Vars */
  float temp2;
  uint32_t pressure;
  float humidity;
  uint32_t gas_resistance;
  float altitude;

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
      pressure = bme.pressure / 100.0; // hPa
      humidity = bme.humidity; // %
      gas_resistance = bme.gas_resistance / 1000.0; // KOhms
      altitude = bme.readAltitude(SEALEVELPRESSURE_HPA); // m

      bmeEndTime = 0L;
    }
  }

  /* Read IMU */
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

  // GPS
  if (gpsNextTime == 0 || gpsNextTime <= timestamp) {
    myGNSS.checkUblox();

    // reset the display
    display.clearDisplay();
    display.setCursor(0, 0);

    snprintf(time, 9, "%02d:%02d:%02d", nmea.getHour(), nmea.getMinute(), nmea.getSecond());
    snprintf(date, 11, "20%02d/%02d/%02d", nmea.getYear(), nmea.getMonth(), nmea.getDay());

    display.print("Time: ");
    display.println(time);

    display.print("Date: ");
    display.println(date);

    display.print("Fix: "); display.print((int)nmea.isValid());
    //display.print(" quality: "); display.println((int)GPS.fixquality);
#if 0
    if (nmea.isValid()) {
      //display.print("Location: ");
      display.print(nmea.getLatitude()/1000000.0, 6);
      display.print(", ");
      display.print(nmea.getLongitude()/1000000.0, 6);
      //display.print("Speed (knots): "); display.println(GPS.speed);
      //display.print("Angle: "); display.println(GPS.angle);
      long altitude = 0L;
      nmea.getAltitude(&altitude);
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

    gpsNextTime = timestamp + 1000;
  }

  // only print the calculated output once in a while
  if (counter++ <= PRINT_EVERY_N_UPDATES) {
    return;
  }
  // reset the counter
  counter = 0;

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
  doc["pitch"] = pitch;
  doc["roll"] = roll;
  serializeJson(doc, Serial);
  Serial.println();
  doc.clear();

  /* From dedicated sensor */
  doc["device"] = "Enviro";
  doc["temp"] = temp2;
  doc["humidity"] = humidity;
  serializeJson(doc, Serial);
  Serial.println();
  doc.clear();

  display.print("Temp: ");
  display.println(temp2, 2);
  display.print("Humidity: ");
  display.println(humidity, 2);

#if 0 /* There's really no room for this on one display. */
  display.print("O: ");
  display.print(heading, 1);
  display.print(", ");
  display.print(pitch, 1);
  display.print(", ");
  display.println(roll, 1);
#endif

#ifdef DEBUG_BUNNY
  Serial.print("Orientation: ");
  Serial.print(heading);
  Serial.print(", ");
  Serial.print(pitch);
  Serial.print(", ");
  Serial.println(roll);
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

#if 0
   power_led_state = !power_led_state;
   digitalWrite(POWER_PIN, power_led_state);
#endif
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
