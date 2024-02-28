#include <Adafruit_LIS3MDL.h>
Adafruit_LIS3MDL lis3mdl;

// Can change this to be LSM6DSOX or whatever ya like
#include <Adafruit_LSM6DSOX.h>
Adafruit_LSM6DSOX lsm6ds;

bool init_sensors(Adafruit_Sensor **accelerometer, Adafruit_Sensor **gyroscope, Adafruit_Sensor **magnetometer, Adafruit_Sensor **temperature) {
  if (!lsm6ds.begin_I2C() || !lis3mdl.begin_I2C()) {
    return false;
  }
  *accelerometer = lsm6ds.getAccelerometerSensor();
  *gyroscope = lsm6ds.getGyroSensor();
  *temperature = lsm6ds.getTemperatureSensor();
  *magnetometer = &lis3mdl;

  return true;
}

void setup_sensors(void) {
  // set lowest range
  lsm6ds.setAccelRange(LSM6DS_ACCEL_RANGE_8_G);
  lsm6ds.setGyroRange(LSM6DS_GYRO_RANGE_1000_DPS);
  lis3mdl.setRange(LIS3MDL_RANGE_4_GAUSS);

  // set slightly above refresh rate
  lsm6ds.setAccelDataRate(LSM6DS_RATE_104_HZ);
  lsm6ds.setGyroDataRate(LSM6DS_RATE_104_HZ);
  lis3mdl.setDataRate(LIS3MDL_DATARATE_1000_HZ);
  lis3mdl.setPerformanceMode(LIS3MDL_HIGHMODE);
  lis3mdl.setOperationMode(LIS3MDL_CONTINUOUSMODE);
}
