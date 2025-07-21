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

#include <string.h>
#include "display_module.h"
#include "enviro_module.h"  // For ENS160/SCD41 defines
#include "imu_module.h"     // For IMU sensor defines
#include "gps_module.h"     // For GPS functionality
#include "logger.h"

extern char ssid[];

// Create semaphore for thread-safe display access
SemaphoreHandle_t displayMutex = NULL;

// First-draw flags for each page (to reduce flicker)
bool imu_page_first_draw = true;
bool gps_page_first_draw = true;
bool enviro_page_first_draw = true;
bool wifi_page_first_draw = true;
bool espnow_page_first_draw = true;

// Global variables
display_data_t display_data;
int current_page = PAGE_IMU;
unsigned long last_page_change = 0;

#ifdef ESP32_S3_REVERSE_TFT
// Button control variables
volatile bool prevPageRequested = false;
volatile bool nextPageRequested = false;
volatile unsigned long lastButtonTime = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 500;
#endif

// Initialize display
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Color definitions
#define COLOR_BG ST77XX_BLACK
#define COLOR_TITLE ST77XX_WHITE
#define COLOR_IMU ST77XX_MAGENTA
#define COLOR_GPS ST77XX_BLUE
#define COLOR_ENVIRO ST77XX_GREEN
#define COLOR_WIFI ST77XX_CYAN
#define COLOR_ALERT ST77XX_RED
#define COLOR_GOOD ST77XX_GREEN
#define COLOR_TEXT ST77XX_WHITE
#define COLOR_VALUE ST77XX_YELLOW

#ifdef ESP32_S3_REVERSE_TFT
void setupPageButtons(void) {
  // Setup page control buttons
  // D1 and D2 are pulled HIGH when pressed
  pinMode(PREV_PAGE_BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(NEXT_PAGE_BUTTON_PIN, INPUT_PULLDOWN);

  // Attach interrupts - trigger on RISING edge for D1/D2 (LOW to HIGH when pressed)
  attachInterrupt(digitalPinToInterrupt(PREV_PAGE_BUTTON_PIN), prevPageButtonHandler, RISING);
  attachInterrupt(digitalPinToInterrupt(NEXT_PAGE_BUTTON_PIN), nextPageButtonHandler, RISING);

  LOG_PRINTLN(F("Page control buttons initialized"));
}

void IRAM_ATTR prevPageButtonHandler(void) {
  unsigned long buttonTime = millis();
  if (buttonTime - lastButtonTime > BUTTON_DEBOUNCE_MS) {
    prevPageRequested = true;
    lastButtonTime = buttonTime;
  }
}

void IRAM_ATTR nextPageButtonHandler(void) {
  unsigned long buttonTime = millis();
  if (buttonTime - lastButtonTime > BUTTON_DEBOUNCE_MS) {
    nextPageRequested = true;
    lastButtonTime = buttonTime;
  }
}
#endif

// Setup TFT display
void setupDisplay(void) {
  // Enable power for TFT
  pinMode(TFT_PWR, OUTPUT);
  digitalWrite(TFT_PWR, HIGH);

  // Enable backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Initialize the display
  tft.init(TFT_HEIGHT, TFT_WIDTH);  // Note: We're initializing with swapped height and width due to rotation
#ifdef ESP32_S3_REVERSE_TFT
  tft.setRotation(1);               // Rotate to landscape
#else
  tft.setRotation(3);               // Rotate to reverse landscape
#endif
  tft.fillScreen(COLOR_BG);

  // Create mutex for display access
  displayMutex = xSemaphoreCreateMutex();
  if (displayMutex == NULL) {
    LOG_PRINTLN("Failed to create displayMutex");
  }

  // Initialize display data structure
  memset(&display_data, 0, sizeof(display_data));
  strcpy(display_data.air_quality_description, "Unknown");
  strcpy(display_data.time, "00:00:00");
  strcpy(display_data.date, "0000/00/00");
#ifdef WIFI_MODE
  strcpy(display_data.ssid, ssid);
  strcpy(display_data.ip_address, "0.0.0.0");
#endif

  // Reset page change timer
  last_page_change = millis();

  // Draw initial welcome screen
  tft.fillScreen(COLOR_BG);
  tft.setCursor(20, 30);
  tft.setTextSize(3);
  tft.setTextColor(COLOR_TITLE);
  tft.println("OASIS AURA");
  tft.setCursor(20, 70);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT);
  tft.println("Sensor Display");
  tft.setCursor(20, 100);
  tft.setTextSize(1);
  tft.println("Initializing...");

  delay(2000);  // Show welcome screen for 2 seconds

#ifdef ESP32_S3_REVERSE_TFT
  setupPageButtons();
#endif

  LOG_PRINTLN("Display initialized");
}

// Function to reset all page first-draw flags when changing pages
void resetPageDrawFlags(void) {
  imu_page_first_draw = true;
  gps_page_first_draw = true;
  enviro_page_first_draw = true;
  wifi_page_first_draw = true;
  espnow_page_first_draw = true;
}

// Display IMU page with minimal flicker
void displayIMUPage(void) {
  // Static variables to track previous values
  static float prev_heading = -999;
  static float prev_pitch = -999;
  static float prev_roll = -999;

  // On first draw or page change, draw the static elements
  if (imu_page_first_draw) {
    // Clear the screen on first draw
    tft.fillScreen(COLOR_BG);

    // Page title
    tft.setTextSize(2);
    tft.setTextColor(COLOR_IMU);
    tft.setCursor(10, 10);
    tft.print("Motion Sensor");

    // Draw divider line
    tft.drawLine(0, 30, TFT_WIDTH, 30, COLOR_IMU);

    // UPDATED: Define fixed locations for our elements
    int compass_x = 180;  // Moved further right
    int compass_y = 55;
    int indicator_size = 35;  // Size for compass

    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);

    // Static labels - moved text position further left to avoid overlap
    tft.setCursor(10, 50);
    tft.print("Heading:");

    tft.setCursor(10, 70);
    tft.print("Pitch:");

    tft.setCursor(10, 90);
    tft.print("Roll:");

    // Mark cardinal directions
    tft.setCursor(compass_x - 3, compass_y - indicator_size - 7);
    tft.print("N");
    tft.setCursor(compass_x + indicator_size + 2, compass_y - 3);
    tft.print("E");
    tft.setCursor(compass_x - 3, compass_y + indicator_size + 2);
    tft.print("S");
    tft.setCursor(compass_x - indicator_size - 7, compass_y - 3);
    tft.print("W");

    // Page indicator
    tft.setCursor(10, TFT_HEIGHT - 15);
    tft.setTextColor(COLOR_TEXT);
    tft.print("Page ");
    tft.print(current_page + 1);
    tft.print("/");
    tft.print(PAGE_COUNT);

    imu_page_first_draw = false;

    // Set initial values to force first update
    prev_heading = -999;
    prev_pitch = -999;
    prev_roll = -999;
  }

  if (!display_data.imu_available) {
    // Display unavailable message
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ALERT);
    tft.setCursor(50, 80);
    tft.print("IMU SENSOR UNAVAILABLE");
    tft.setCursor(50, 100);
    tft.print("Check connections");
    return;  // Skip the rest of the rendering
  }

  // Use the same size for both indicators
  int indicator_size = 30;  // Slightly reduced size

  // Only update values if they've changed
  if (prev_heading != display_data.heading || prev_pitch != display_data.pitch || prev_roll != display_data.roll) {
    // Draw a simple orientation indicator in top-right quadrant
    int compass_x = 180;  // Moved further right
    int compass_y = 55;

    // Update heading display (erase old value first) - reduced width for data display area
    tft.fillRect(70, 50, 50, 10, COLOR_BG);
    tft.setCursor(70, 50);
    tft.setTextColor(COLOR_VALUE);
    tft.print(display_data.heading, 1);

    // Update pitch display
    tft.fillRect(70, 70, 50, 10, COLOR_BG);
    tft.setCursor(70, 70);
    tft.setTextColor(COLOR_VALUE);
    tft.print(display_data.pitch, 1);

    // Update roll display
    tft.fillRect(70, 90, 50, 10, COLOR_BG);
    tft.setCursor(70, 90);
    tft.setTextColor(COLOR_VALUE);
    tft.print(display_data.roll, 1);

    // Redraw compass (erase old content first)
    tft.fillCircle(compass_x, compass_y, indicator_size, COLOR_BG);
    tft.drawCircle(compass_x, compass_y, indicator_size, COLOR_TEXT);

    // Draw heading line (convert to radians)
    float heading_rad = display_data.heading * PI / 180.0;
    int x2 = compass_x + sin(heading_rad) * indicator_size;
    int y2 = compass_y - cos(heading_rad) * indicator_size;
    tft.drawLine(compass_x, compass_y, x2, y2, COLOR_IMU);

    // Draw Roll/Pitch indicator in bottom-right quadrant
    // UPDATED: Moved the level indicator
    int level_x = 180;  // Keep aligned with compass on x-axis
    int level_y = 115;  // Moved down more

    // Erase old content
    tft.fillRect(level_x - indicator_size, level_y - indicator_size / 2,
                 indicator_size * 2, indicator_size, COLOR_BG);

    // Draw level border (same size as compass)
    tft.drawRect(level_x - indicator_size, level_y - indicator_size / 2,
                 indicator_size * 2, indicator_size, COLOR_TEXT);

    // Calculate position based on roll and pitch (scale down to keep in bounds)
    int ball_x = level_x + (display_data.roll / 45.0) * (indicator_size - 5);
    int ball_y = level_y + (display_data.pitch / 45.0) * (indicator_size / 2 - 5);

    // Constrain to box
    if (ball_x < level_x - indicator_size + 5) ball_x = level_x - indicator_size + 5;
    if (ball_x > level_x + indicator_size - 5) ball_x = level_x + indicator_size - 5;
    if (ball_y < level_y - indicator_size / 2 + 5) ball_y = level_y - indicator_size / 2 + 5;
    if (ball_y > level_y + indicator_size / 2 - 5) ball_y = level_y + indicator_size / 2 - 5;

    // Draw center crosshair
    tft.drawLine(level_x - 8, level_y, level_x + 8, level_y, COLOR_TEXT);
    tft.drawLine(level_x, level_y - 8, level_x, level_y + 8, COLOR_TEXT);

    // Draw level bubble
    tft.fillCircle(ball_x, ball_y, 5, COLOR_IMU);

    // Update previous values
    prev_heading = display_data.heading;
    prev_pitch = display_data.pitch;
    prev_roll = display_data.roll;
  }
}

// Display GPS page with minimal flicker
void displayGPSPage(void) {
  // Static variables to track previous values
  static char prev_time[10] = "";
  static char prev_date[12] = "";
  static int prev_fix = -1;
  static float prev_lat = -999;
  static float prev_lon = -999;
  static int prev_alt = -999;
  static int prev_satellites = -1;
  static int prev_speed = -1;

  // On first draw, display the static elements
  if (gps_page_first_draw) {
    // Clear the screen on first draw
    tft.fillScreen(COLOR_BG);

    // Page title
    tft.setTextSize(2);
    tft.setTextColor(COLOR_GPS);
    tft.setCursor(10, 10);
    tft.print("GPS Status");

    // Draw divider line
    tft.drawLine(0, 30, TFT_WIDTH, 30, COLOR_GPS);

    // Display static labels
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);

    // Time and date labels
    tft.setCursor(10, 40);
    tft.print("Time:");

    tft.setCursor(120, 40);
    tft.print("Date:");

    // Fix status label
    tft.setCursor(10, 55);
    tft.print("Fix:");

    // Satellites label
    tft.setCursor(120, 55);
    tft.print("Sats:");

    // Position labels
    tft.setCursor(10, 70);
    tft.print("Lat:");

    tft.setCursor(10, 85);
    tft.print("Lon:");

    tft.setCursor(10, 100);
    tft.print("Alt:");

    // Speed label - moved from 120 to 120 to ensure it's fully visible
    tft.setCursor(120, 70);
    tft.print("Speed:");

    // Speed gauge labels
    tft.setCursor(120, 95);
    tft.print("0");

    tft.setCursor(205, 95);
    tft.print("100");

    // Page indicator
    tft.setCursor(10, TFT_HEIGHT - 15);
    tft.setTextColor(COLOR_TEXT);
    tft.print("Page ");
    tft.print(current_page + 1);
    tft.print("/");
    tft.print(PAGE_COUNT);

    gps_page_first_draw = false;

    // Force initial update
    strcpy(prev_time, "");
    strcpy(prev_date, "");
    prev_fix = -1;
    prev_lat = -999;
    prev_lon = -999;
    prev_alt = -999;
    prev_satellites = -1;
    prev_speed = -1;
  }

  if (!display_data.gps_available) {
    // Display unavailable message
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ALERT);
    tft.setCursor(50, 80);
    tft.print("GPS SENSOR UNAVAILABLE");
    tft.setCursor(50, 100);
    tft.print("Check connections");
    return;  // Skip the rest of the rendering
  }

  // Only update time if it's changed
  if (strcmp(prev_time, display_data.time) != 0) {
    tft.fillRect(50, 40, 60, 10, COLOR_BG);
    tft.setCursor(50, 40);
    tft.setTextColor(COLOR_VALUE);
    tft.print(display_data.time);
    strcpy(prev_time, display_data.time);
  }

  // Only update date if it's changed
  if (strcmp(prev_date, display_data.date) != 0) {
    tft.fillRect(160, 40, 80, 10, COLOR_BG);
    tft.setCursor(160, 40);
    tft.setTextColor(COLOR_VALUE);
    tft.print(display_data.date);
    strcpy(prev_date, display_data.date);
  }

  // Only update fix status if it's changed
  if (prev_fix != display_data.fix) {
    tft.fillRect(50, 55, 70, 10, COLOR_BG);
    tft.setCursor(50, 55);

    // Show fix status with appropriate color
    if (display_data.fix) {
      tft.setTextColor(COLOR_GOOD);
      tft.print("Valid");

      // Draw a small indicator
      tft.fillCircle(85, 57, 3, COLOR_GOOD);
    } else {
      tft.setTextColor(COLOR_ALERT);
      tft.print("No Fix");

      // Draw a small indicator
      tft.fillCircle(100, 57, 3, COLOR_ALERT);
    }

    prev_fix = display_data.fix;
  }

  // Only update satellites if changed
  if (prev_satellites != display_data.satellites) {
    tft.fillRect(160, 55, 30, 10, COLOR_BG);
    tft.setCursor(160, 55);
    tft.setTextColor(COLOR_VALUE);
    tft.print(display_data.satellites);

    prev_satellites = display_data.satellites;
  }

  // Only show location data if we have a fix
  if (display_data.fix) {
    // Update latitude if changed
    if (prev_lat != display_data.latitude) {
      tft.fillRect(50, 70, 80, 10, COLOR_BG);
      tft.setCursor(50, 70);
      tft.setTextColor(COLOR_VALUE);
      tft.print(display_data.latitude, 6);

      prev_lat = display_data.latitude;
    }

    // Update longitude if changed
    if (prev_lon != display_data.longitude) {
      tft.fillRect(50, 85, 80, 10, COLOR_BG);
      tft.setCursor(50, 85);
      tft.setTextColor(COLOR_VALUE);
      tft.print(display_data.longitude, 6);

      prev_lon = display_data.longitude;
    }

    // Update altitude if changed
    if (prev_alt != display_data.altitude) {
      tft.fillRect(50, 100, 60, 10, COLOR_BG);
      tft.setCursor(50, 100);
      tft.setTextColor(COLOR_VALUE);
      tft.print(display_data.altitude);
      tft.print(" ft");

      prev_alt = display_data.altitude;
    }

    // Only update speed if it's changed
    if (prev_speed != display_data.speed) {
      // Make sure the Speed label is visible
      tft.setCursor(120, 70);
      tft.setTextColor(COLOR_TEXT);
      tft.print("Speed:");

      // Clear just the speed value area
      tft.fillRect(160, 70, 70, 10, COLOR_BG);
      tft.setCursor(160, 70);
      tft.setTextColor(COLOR_VALUE);
      tft.print(display_data.speed);
      tft.print(" mph");

      // Speed gauge (simple horizontal bar)
      int max_speed = 100;  // Maximum speed for display scale
      int gauge_width = 100;
      int gauge_height = 8;
      int gauge_x = 120;
      int gauge_y = 85;

      // Always clear and redraw the entire gauge area to prevent artifacts
      tft.fillRect(gauge_x, gauge_y - 2, gauge_width + 2, gauge_height + 4, COLOR_BG);

      // Draw gauge outline
      tft.drawRect(gauge_x, gauge_y, gauge_width, gauge_height, COLOR_TEXT);

      // Fill based on speed (with limit)
      int speed_fill = (display_data.speed > max_speed) ? gauge_width : (display_data.speed * gauge_width / max_speed);

      // Choose color based on speed
      uint16_t speed_color;
      if (display_data.speed < 30) {
        speed_color = ST77XX_GREEN;
      } else if (display_data.speed < 60) {
        speed_color = ST77XX_YELLOW;
      } else {
        speed_color = ST77XX_RED;
      }

      // Always draw filled portion, even if speed is 0 (will draw a 0-width rectangle)
      if (speed_fill > 0) {
        tft.fillRect(gauge_x, gauge_y, speed_fill, gauge_height, speed_color);
      }

      // Update the speed scale indicators
      tft.setCursor(gauge_x, gauge_y + gauge_height + 2);
      tft.setTextColor(COLOR_TEXT);
      tft.print("0");

      tft.setCursor(gauge_x + gauge_width - 15, gauge_y + gauge_height + 2);
      tft.print("100");

      prev_speed = display_data.speed;
    }
  } else if (prev_fix != display_data.fix) {
    // If we just lost fix, clear the data area
    tft.fillRect(10, 70, TFT_WIDTH - 20, 60, COLOR_BG);

    // If no fix, show message
    tft.setCursor(10, 85);
    tft.setTextColor(COLOR_ALERT);
    tft.print("Waiting for satellite signal...");
  }
}

// Display environmental sensor page with minimal flicker
void displayEnviroPage(void) {
  // Static variables to track previous values
  static float prev_temp = -999;
  static float prev_humidity = -999;
  static float prev_air_quality = -999;
  static char prev_aq_desc[15] = "";

  // Additional values for ENS160 sensor
  static int prev_eco2 = -1;
  static int prev_tvoc = -1;

  // Additional values for SCD41 CO2 sensor
  static uint16_t prev_co2 = 0;
  static bool prev_co2_available = false;
  static char prev_co2_desc[15] = "";

  // Draw static elements on first load
  if (enviro_page_first_draw) {
    // Clear screen
    tft.fillScreen(COLOR_BG);

    // Page title
    tft.setTextSize(2);
    tft.setTextColor(COLOR_ENVIRO);
    tft.setCursor(10, 10);
    tft.print("Environment");

    // Draw divider line
    tft.drawLine(0, 30, TFT_WIDTH, 30, COLOR_ENVIRO);

    // Static labels
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);

    tft.setCursor(10, 40);
    tft.print("Temp:");

    tft.setCursor(10, 55);
    tft.print("Humidity:");

    // Show eCO2 and TVOC for ENS160
    tft.setCursor(10, 70);
    tft.print("eCO2:");

    tft.setCursor(120, 70);
    tft.print("TVOC:");

    // CO2 label if SCD41 is available
    if (display_data.co2_available) {
      tft.setCursor(10, 85);
      tft.print("CO2:");
    }

    // Air Quality label (moved down if CO2 is shown)
    tft.setCursor(10, display_data.co2_available ? 100 : 85);
    tft.print("Air Quality:");

    // Page indicator moved to far left
    tft.setCursor(10, TFT_HEIGHT - 15);
    tft.setTextColor(COLOR_TEXT);
    tft.print("Page ");
    tft.print(current_page + 1);
    tft.print("/");
    tft.print(PAGE_COUNT);

    enviro_page_first_draw = false;

    // Force initial update
    prev_temp = -999;
    prev_humidity = -999;
    prev_air_quality = -999;
    strcpy(prev_aq_desc, "");
    prev_eco2 = -1;
    prev_tvoc = -1;

    // Force initial CO2 update
    prev_co2 = 0;
    prev_co2_available = !display_data.co2_available;  // Force refresh
    strcpy(prev_co2_desc, "");
  }

  // Check if all environmental sensors are unavailable
  if (!display_data.temp_available && !display_data.humidity_available && !display_data.air_quality_available && !display_data.ens160_available && !display_data.co2_available) {
    // Display unavailable message
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ALERT);
    tft.setCursor(30, 80);
    tft.print("ALL ENVIRONMENT SENSORS UNAVAILABLE");
    tft.setCursor(50, 100);
    tft.print("Check connections");
    return;  // Skip the rest of the rendering
  }

  // Only update temperature if changed
  if (prev_temp != display_data.temperature) {
    tft.fillRect(120, 40, 120, 10, COLOR_BG);
    tft.setCursor(120, 40);

    if (display_data.temp_available) {
      tft.setTextColor(COLOR_VALUE);
      tft.print(display_data.temperature, 1);
      tft.print("C / ");
      tft.print(display_data.temperature * 9.0 / 5.0 + 32.0, 1);
      tft.print("F");
    } else {
      tft.setTextColor(COLOR_ALERT);
      tft.print("Not available");
    }

    prev_temp = display_data.temperature;
  }

  // Only update humidity if changed
  if (prev_humidity != display_data.humidity) {
    tft.fillRect(120, 55, 70, 10, COLOR_BG);
    tft.setCursor(120, 55);

    if (display_data.humidity_available) {
      tft.setTextColor(COLOR_VALUE);
      tft.print(display_data.humidity, 1);
      tft.print("%");
    } else {
      tft.setTextColor(COLOR_ALERT);
      tft.print("Not available");
    }

    prev_humidity = display_data.humidity;
  }

  // Update eCO2 and TVOC for ENS160
  if (prev_eco2 != display_data.eco2) {
    tft.fillRect(50, 70, 60, 10, COLOR_BG);
    tft.setCursor(50, 70);

    if (display_data.ens160_available) {
      tft.setTextColor(COLOR_VALUE);
      tft.print(display_data.eco2);
      tft.print(" ppm");
    } else {
      tft.setTextColor(COLOR_ALERT);
      tft.print("Not available");
    }

    prev_eco2 = display_data.eco2;
  }

  if (prev_tvoc != display_data.tvoc) {
    tft.fillRect(160, 70, 70, 10, COLOR_BG);
    tft.setCursor(160, 70);

    if (display_data.ens160_available) {
      tft.setTextColor(COLOR_VALUE);
      tft.print(display_data.tvoc);
      tft.print(" ppb");
    } else {
      tft.setTextColor(COLOR_ALERT);
      tft.print("Not available");
    }

    prev_tvoc = display_data.tvoc;
  }

  // Update CO2 readings if available and changed
  if (display_data.co2_available) {
    if (prev_co2 != display_data.co2 || prev_co2_available != display_data.co2_available || strcmp(prev_co2_desc, display_data.co2_quality_description) != 0) {

      // Clear the CO2 value area
      tft.fillRect(120, 85, 120, 10, COLOR_BG);
      tft.setCursor(120, 85);

      // Choose color based on CO2 quality
      uint16_t co2_color;
      if (strcmp(display_data.co2_quality_description, "Excellent") == 0) {
        co2_color = COLOR_GOOD;
      } else if (strcmp(display_data.co2_quality_description, "Good") == 0) {
        co2_color = ST77XX_CYAN;
      } else if (strcmp(display_data.co2_quality_description, "Fair") == 0) {
        co2_color = ST77XX_YELLOW;
      } else if (strcmp(display_data.co2_quality_description, "Poor") == 0) {
        co2_color = ST77XX_ORANGE;
      } else {
        co2_color = COLOR_ALERT;
      }

      tft.setTextColor(co2_color);
      tft.print(display_data.co2);
      tft.print(" ppm (");
      tft.print(display_data.co2_quality_description);
      tft.print(")");

      prev_co2 = display_data.co2;
      prev_co2_available = display_data.co2_available;
      strcpy(prev_co2_desc, display_data.co2_quality_description);
    }
  } else if (prev_co2_available) {
    // Only show "Not available" when transitioning from available to unavailable
    tft.fillRect(120, 85, 120, 10, COLOR_BG);
    tft.setCursor(120, 85);
    tft.setTextColor(COLOR_ALERT);
    tft.print("Not available");

    prev_co2_available = false;
  }

  // Only update air quality if changed
  if (prev_air_quality != display_data.air_quality || strcmp(prev_aq_desc, display_data.air_quality_description) != 0) {

    // Adjust y position based on whether CO2 is displayed
    int aq_y = display_data.co2_available ? 100 : 85;

    tft.fillRect(120, aq_y, 120, 10, COLOR_BG);
    tft.setCursor(120, aq_y);

    if (display_data.air_quality_available) {
      // Set color based on air quality
      uint16_t aq_color;
      if (display_data.air_quality > 80) {
        aq_color = COLOR_GOOD;
      } else if (display_data.air_quality > 60) {
        aq_color = ST77XX_CYAN;
      } else if (display_data.air_quality > 40) {
        aq_color = ST77XX_YELLOW;
      } else if (display_data.air_quality > 20) {
        aq_color = ST77XX_ORANGE;
      } else {
        aq_color = COLOR_ALERT;
      }

      tft.setTextColor(aq_color);
      tft.print(display_data.air_quality, 1);
      tft.print(" (");
      tft.print(display_data.air_quality_description);
      tft.print(")");
    } else {
      tft.setTextColor(COLOR_ALERT);
      tft.print("Not available");
    }

    // Update previous values tracking
    prev_air_quality = display_data.air_quality;
    strcpy(prev_aq_desc, display_data.air_quality_description);
  }

  // Page indicator moved to far left
  tft.setCursor(10, TFT_HEIGHT - 15);
  tft.setTextColor(COLOR_TEXT);
  tft.print("Page ");
  tft.print(current_page + 1);
  tft.print("/");
  tft.print(PAGE_COUNT);
}

#ifdef WIFI_MODE
// Display WiFi status page with minimal flicker
void displayWiFiPage(void) {
  // Static variables to track previous values
  static char prev_ssid[33] = "";
  static char prev_ip[16] = "";
  static int prev_rssi = 999;
  static bool prev_connected = false;

  // Draw static elements on first load
  if (wifi_page_first_draw) {
    // Clear screen
    tft.fillScreen(COLOR_BG);

    // Page title
    tft.setTextSize(2);
    tft.setTextColor(COLOR_WIFI);
    tft.setCursor(10, 10);
    tft.print("WiFi Status");

    // Draw divider line
    tft.drawLine(0, 30, TFT_WIDTH, 30, COLOR_WIFI);

    // Static labels
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);

    tft.setCursor(10, 40);
    tft.print("SSID:");

    tft.setCursor(10, 55);
    tft.print("Status:");

    tft.setCursor(10, 70);
    tft.print("IP:");

    tft.setCursor(10, 85);
    tft.print("RSSI:");

    // Page indicator
    tft.setCursor(10, TFT_HEIGHT - 15);
    tft.setTextColor(COLOR_TEXT);
    tft.print("Page ");
    tft.print(current_page + 1);
    tft.print("/");
    tft.print(PAGE_COUNT);

    wifi_page_first_draw = false;

    // Force initial update
    strcpy(prev_ssid, "");
    strcpy(prev_ip, "");
    prev_rssi = 999;
    prev_connected = !display_data.is_connected;
  }

  // Only update SSID if changed
  if (strcmp(prev_ssid, display_data.ssid) != 0) {
    tft.fillRect(120, 40, 120, 10, COLOR_BG);
    tft.setCursor(120, 40);
    tft.setTextColor(COLOR_VALUE);
    tft.print(display_data.ssid);
    strcpy(prev_ssid, display_data.ssid);
  }

  // Only update connection status if changed
  if (prev_connected != display_data.is_connected) {
    tft.fillRect(120, 55, 100, 10, COLOR_BG);
    tft.setCursor(120, 55);

    // Show connection status with appropriate color
    if (display_data.is_connected) {
      tft.setTextColor(COLOR_GOOD);
      tft.print("Connected");
    } else {
      tft.setTextColor(COLOR_ALERT);
      tft.print("Disconnected");
    }

    prev_connected = display_data.is_connected;
  }

  // Only update IP address if changed
  if (strcmp(prev_ip, display_data.ip_address) != 0) {
    tft.fillRect(120, 70, 120, 10, COLOR_BG);
    tft.setCursor(120, 70);
    tft.setTextColor(COLOR_VALUE);
    tft.print(display_data.ip_address);
    strcpy(prev_ip, display_data.ip_address);
  }

  // Only update RSSI if changed
  if (prev_rssi != display_data.rssi) {
    tft.fillRect(120, 85, 60, 10, COLOR_BG);
    tft.setCursor(120, 85);

    // Color code signal strength
    int signal_strength = display_data.rssi;
    uint16_t signal_color;

    if (signal_strength > -50) {
      signal_color = COLOR_GOOD;
    } else if (signal_strength > -70) {
      signal_color = ST77XX_YELLOW;
    } else {
      signal_color = COLOR_ALERT;
    }

    tft.setTextColor(signal_color);
    tft.print(signal_strength);
    tft.print(" dBm");

    int base_x = 180;     // Moved more to the right
    int base_y = 105;     // Moved up to align with RSSI text
    int bar_width = 10;   // Slightly smaller bars
    int bar_spacing = 3;  // Closer together
    int max_height = 15;  // Slightly shorter

    // Clear previous bars
    tft.fillRect(base_x, base_y - max_height, (bar_width + bar_spacing) * 4, max_height + 5, COLOR_BG);

    // Draw 4 bars with varying heights
    for (int i = 0; i < 4; i++) {
      int height = (i + 1) * (max_height / 4);
      int x = base_x + i * (bar_width + bar_spacing);
      int y = base_y + (max_height - height) - max_height;  // Position above the text line

      // Determine if this bar should be filled based on signal strength
      if (display_data.is_connected && ((signal_strength > -90 && i == 0) || (signal_strength > -70 && i == 1) || (signal_strength > -60 && i == 2) || (signal_strength > -50 && i == 3))) {
        tft.fillRect(x, y, bar_width, height, signal_color);
      } else {
        // Draw outline only for inactive bars
        tft.drawRect(x, y, bar_width, height, COLOR_TEXT);
      }
    }

    prev_rssi = display_data.rssi;
  }
}
#endif

#ifdef ESPNOW_MODE
// Display ESP-Now page
void displayESPNowPage(void) {
  // Static variables to track previous values
  static size_t prev_peer_count = 0;
  static char prev_peers[MAX_DISPLAY_PEERS][32] = {0};
  static uint32_t prev_packets_received[MAX_DISPLAY_PEERS] = {0};
  static uint32_t prev_packets_missed[MAX_DISPLAY_PEERS] = {0};

  // On first draw, display the static elements
  if (espnow_page_first_draw) {
    // Clear the screen on first draw
    tft.fillScreen(COLOR_BG);

    // Page title
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_BLUE); // Use blue for ESP-Now
    tft.setCursor(10, 10);
    tft.print("ESP-Now Status");

    // Draw divider line
    tft.drawLine(0, 30, TFT_WIDTH, 30, ST77XX_BLUE);

    // Page indicator
    tft.setCursor(10, TFT_HEIGHT - 15);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.print("Page ");
    tft.print(current_page + 1);
    tft.print("/");
    tft.print(PAGE_COUNT);

    espnow_page_first_draw = false;

    // Force refresh of all data
    prev_peer_count = 999;
    memset(prev_peers, 0, sizeof(prev_peers));
    memset(prev_packets_received, 0, sizeof(prev_packets_received));
    memset(prev_packets_missed, 0, sizeof(prev_packets_missed));
  }

  // Update peer count if changed
  if (prev_peer_count != display_data.espnow_peer_count) {
    tft.fillRect(10, 40, 220, 10, COLOR_BG);
    tft.setCursor(10, 40);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.print("Connected Peers: ");
    tft.setTextColor(COLOR_VALUE);
    tft.print(display_data.espnow_peer_count);

    prev_peer_count = display_data.espnow_peer_count;
  }

  // Check if any data has changed
  bool update_needed = false;
  for (size_t i = 0; i < MAX_DISPLAY_PEERS; i++) {
    if (strcmp(prev_peers[i], display_data.espnow_peers[i]) != 0 ||
        prev_packets_received[i] != display_data.espnow_packets_received[i] ||
        prev_packets_missed[i] != display_data.espnow_packets_missed[i]) {
      update_needed = true;
      break;
    }
  }

  if (update_needed) {
    // Clear the peer list area
    tft.fillRect(10, 55, 220, 60, COLOR_BG);

    // Column headers
    tft.setCursor(10, 55);
    tft.setTextColor(COLOR_TEXT);
    tft.print("Device");

    tft.setCursor(130, 55);
    tft.print("Rcv");

    tft.setCursor(170, 55);
    tft.print("Missed");

    tft.drawLine(10, 65, 230, 65, COLOR_TEXT);

    // Display peer list with statistics
    int visiblePeers = min((int)display_data.espnow_peer_count, MAX_DISPLAY_PEERS);
    for (size_t i = 0; i < visiblePeers; i++) {
      // Topic name
      tft.setCursor(10, 70 + i * 12);
      tft.setTextColor(COLOR_VALUE);
      tft.print(display_data.espnow_peers[i]);

      // Packets received
      tft.setCursor(130, 70 + i * 12);
      tft.print(display_data.espnow_packets_received[i]);

      // Packets missed
      tft.setCursor(170, 70 + i * 12);
      if (display_data.espnow_packets_missed[i] > 0) {
        tft.setTextColor(COLOR_ALERT); // Red for missed packets
      } else {
        tft.setTextColor(COLOR_GOOD);  // Green for no missed packets
      }
      tft.print(display_data.espnow_packets_missed[i]);

      // Copy to previous state
      strncpy(prev_peers[i], display_data.espnow_peers[i], sizeof(prev_peers[i]));
      prev_packets_received[i] = display_data.espnow_packets_received[i];
      prev_packets_missed[i] = display_data.espnow_packets_missed[i];
    }

    // If we have more peers than we can display
    if (display_data.espnow_peer_count > MAX_DISPLAY_PEERS) {
      tft.setCursor(10, 118);
      tft.setTextColor(COLOR_TEXT);
      tft.print("... and ");
      tft.print(display_data.espnow_peer_count - MAX_DISPLAY_PEERS);
      tft.print(" more");
    }
  }
}
#endif

// Main display task - handles page rotation and refresh
void displayTask(void *pvParameters) {
  LOG_PRINTLN(F("Display task started."));

  int current_display_page = -1;  // Force initial draw
  unsigned long last_refresh = 0;

  while (true) {
    unsigned long current_time = millis();

    // Take the display mutex
    if (xSemaphoreTake(displayMutex, portMAX_DELAY) == pdTRUE) {

#ifdef ESP32_S3_REVERSE_TFT
      // Handle button-controlled page changes
      if (prevPageRequested) {
        current_page = (current_page == 0) ? (PAGE_COUNT - 1) : (current_page - 1);
        resetPageDrawFlags();
        prevPageRequested = false;
        LOG_PRINT("Button: Previous page to: ");
        LOG_PRINTLN(String(current_page));
      }

      if (nextPageRequested) {
        current_page = (current_page + 1) % PAGE_COUNT;
        resetPageDrawFlags();
        nextPageRequested = false;
        LOG_PRINT("Button: Next page to: ");
        LOG_PRINTLN(String(current_page));
      }
#else
      // Check if it's time to change the page
      if (current_time - last_page_change >= PAGE_CHANGE_INTERVAL) {
        // Move to next page
        current_page = (current_page + 1) % PAGE_COUNT;
        last_page_change = current_time;

        // Reset the first-draw flags to force redraw of new page
        resetPageDrawFlags();

        LOG_PRINT("Changing to page: ");
        LOG_PRINTLN(String(current_page));
      }
#endif

      // Determine if we should refresh the display now
      bool should_refresh = (current_display_page != current_page) || (current_time - last_refresh >= DISPLAY_REFRESH_RATE_MS);

      if (should_refresh) {
        current_display_page = current_page;

        // Draw the current page - no need to clear as each page handles its own drawing
        switch (current_page) {
          case PAGE_IMU:
            displayIMUPage();
            break;
          case PAGE_GPS:
            displayGPSPage();
            break;
          case PAGE_ENVIRO:
            displayEnviroPage();
            break;
#ifdef WIFI_MODE
          case PAGE_WIFI:
            displayWiFiPage();
            break;
#endif
#ifdef ESPNOW_MODE
          case PAGE_ESPNOW:
            displayESPNowPage();
            break;
#endif
          default:
            // Should never get here
            current_page = PAGE_IMU;
            break;
        }

        last_refresh = current_time;
      }

      // Release the mutex
      xSemaphoreGive(displayMutex);
    }

    // Sleep for a short period
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
