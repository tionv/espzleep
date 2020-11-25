#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <stdio.h>
#include <stdlib.h>
#include "Wire.h"

#define NTP_OFFSET   60 * 60      // In seconds
#define NTP_INTERVAL 60 * 1000    // In miliseconds
#define NTP_ADDRESS  "europe.pool.ntp.org"


// Network Communication Vars / Prototypes


const char* ssid = "****"; // edit
const char* password = "****"; // edit

const char* host = "0.0.0.0"; // edit
const int httpPort = 8080; // (optional)

String craftRequestContent();


// Sensor Vars / Prototypes


struct rawdata {
  int16_t AcX;
  int16_t AcY;
  int16_t AcZ;
  int16_t Tmp;
  int16_t GyX;
  int16_t GyY;
  int16_t GyZ;
};

struct scaleddata {
  float AcX;
  float AcY;
  float AcZ;
  float Tmp;
  float GyX;
  float GyY;
  float GyZ;
};

const uint8_t MPU_addr = 0x68; // I2C address of the MPU-6050

const float MPU_GYRO_250_SCALE = 131.0;
const float MPU_GYRO_500_SCALE = 65.5;
const float MPU_GYRO_1000_SCALE = 32.8;
const float MPU_GYRO_2000_SCALE = 16.4;
const float MPU_ACCL_2_SCALE = 16384.0;
const float MPU_ACCL_4_SCALE = 8192.0;
const float MPU_ACCL_8_SCALE = 4096.0;
const float MPU_ACCL_16_SCALE = 2048.0;

bool checkI2c(byte addr);
void mpu6050Begin(byte addr);
void setMPU6050scales(byte addr, uint8_t Gyro, uint8_t Accl);
void getMPU6050scales(byte addr, uint8_t &Gyro, uint8_t &Accl);
rawdata mpu6050Read(byte addr, bool Debug);
scaleddata convertRawToScaled(byte addr, rawdata data_in, bool Debug);


// Start time service


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);


// MPU Sensor Functions


// mpu6050Begin: This function initializes the MPU-6050 IMU Sensor.
// It verifys the address is correct and wakes up the MPU.
void mpu6050Begin(byte addr) {
  if (checkI2c(addr)) {
    Wire.beginTransmission(MPU_addr);
    Wire.write(0x6B); // PWR_MGMT_1 register
    Wire.write(0); // set to zero (wakes up the MPU-6050)
    Wire.endTransmission(true);

    delay(30); // Ensure gyro has enough time to power up
  }
}

// checkI2c: We are using the return value of the Write.endTransmisstion
// to see if a device did acknowledge to the address.
bool checkI2c(byte addr) {
  Serial.println(" ");
  Wire.beginTransmission(addr);

  if (Wire.endTransmission() == 0)
  {
    Serial.print(" Device Found at 0x");
    Serial.println(addr, HEX);
    return true;
  }
  else
  {
    Serial.print(" No Device Found at 0x");
    Serial.println(addr, HEX);
    return false;
  }
}

// mpu6050Read: This function reads the raw 16-bit data values from the MPU-6050
rawdata mpu6050Read(byte addr, bool Debug) {
  rawdata values;

  Wire.beginTransmission(addr);
  Wire.write(0x3B); // starting with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom(addr, 14); // request a total of 14 registers
  values.AcX = Wire.read() << 8 | Wire.read(); // 0x3B (ACCEL_XOUT_H) & 0x3C (ACCEL_XOUT_L)
  values.AcY = Wire.read() << 8 | Wire.read(); // 0x3D (ACCEL_YOUT_H) & 0x3E (ACCEL_YOUT_L)
  values.AcZ = Wire.read() << 8 | Wire.read(); // 0x3F (ACCEL_ZOUT_H) & 0x40 (ACCEL_ZOUT_L)
  values.Tmp = Wire.read() << 8 | Wire.read(); // 0x41 (TEMP_OUT_H) & 0x42 (TEMP_OUT_L)
  values.GyX = Wire.read() << 8 | Wire.read(); // 0x43 (GYRO_XOUT_H) & 0x44 (GYRO_XOUT_L)
  values.GyY = Wire.read() << 8 | Wire.read(); // 0x45 (GYRO_YOUT_H) & 0x46 (GYRO_YOUT_L)
  values.GyZ = Wire.read() << 8 | Wire.read(); // 0x47 (GYRO_ZOUT_H) & 0x48 (GYRO_ZOUT_L)

  if (Debug) {
    Serial.print(" GyX = "); Serial.print(values.GyX);
    Serial.print(" | GyY = "); Serial.print(values.GyY);
    Serial.print(" | GyZ = "); Serial.print(values.GyZ);
    Serial.print(" | Tmp = "); Serial.print(values.Tmp);
    Serial.print(" | AcX = "); Serial.print(values.AcX);
    Serial.print(" | AcY = "); Serial.print(values.AcY);
    Serial.print(" | AcZ = "); Serial.println(values.AcZ);
  }

  return values;
}

void setMPU6050scales(byte addr, uint8_t Gyro, uint8_t Accl) {
  Wire.beginTransmission(addr);
  Wire.write(0x1B); // write to register starting at 0x1B
  Wire.write(Gyro); // Self Tests Off and set Gyro FS to 250
  Wire.write(Accl); // Self Tests Off and set Accl FS to 8g
  Wire.endTransmission(true);
}

void getMPU6050scales(byte addr, uint8_t &Gyro, uint8_t &Accl) {
  Wire.beginTransmission(addr);
  Wire.write(0x1B); // starting with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom(addr, 2); // request a total of 14 registers
  Gyro = (Wire.read() & (bit(3) | bit(4))) >> 3;
  Accl = (Wire.read() & (bit(3) | bit(4))) >> 3;
}

scaleddata convertRawToScaled(byte addr, rawdata data_in, bool Debug) {
  scaleddata values;
  float scale_value = 0.0;
  byte Gyro, Accl;

  getMPU6050scales(addr, Gyro, Accl);

  if (Debug) {
    Serial.print("Gyro Full-Scale = ");
  }

  switch (Gyro) {
    case 0:
      scale_value = MPU_GYRO_250_SCALE;
      if (Debug) {
        Serial.println("±250 °/s");
      }
      break;
    case 1:
      scale_value = MPU_GYRO_500_SCALE;
      if (Debug) {
        Serial.println("±500 °/s");
      }
      break;
    case 2:
      scale_value = MPU_GYRO_1000_SCALE;
      if (Debug) {
        Serial.println("±1000 °/s");
      }
      break;
    case 3:
      scale_value = MPU_GYRO_2000_SCALE;
      if (Debug) {
        Serial.println("±2000 °/s");
      }
      break;
    default:
      break;
  }

  values.GyX = (float) data_in.GyX / scale_value;
  values.GyY = (float) data_in.GyY / scale_value;
  values.GyZ = (float) data_in.GyZ / scale_value;

  scale_value = 0.0;
  if (Debug) {
    Serial.print("Accl Full-Scale = ");
  }
  switch (Accl) {
    case 0:
      scale_value = MPU_ACCL_2_SCALE;
      if (Debug) {
        Serial.println("±2 g");
      }
      break;
    case 1:
      scale_value = MPU_ACCL_4_SCALE;
      if (Debug) {
        Serial.println("±4 g");
      }
      break;
    case 2:
      scale_value = MPU_ACCL_8_SCALE;
      if (Debug) {
        Serial.println("±8 g");
      }
      break;
    case 3:
      scale_value = MPU_ACCL_16_SCALE;
      if (Debug) {
        Serial.println("±16 g");
      }
      break;
    default:
      break;
  }
  values.AcX = (float) data_in.AcX / scale_value;
  values.AcY = (float) data_in.AcY / scale_value;
  values.AcZ = (float) data_in.AcZ / scale_value;


  values.Tmp = (float) data_in.Tmp / 340.0 + 36.53;

  if (Debug) {
    Serial.print(" GyX = "); Serial.print(values.GyX);
    Serial.print(" °/s| GyY = "); Serial.print(values.GyY);
    Serial.print(" °/s| GyZ = "); Serial.print(values.GyZ);
    Serial.print(" °/s| Tmp = "); Serial.print(values.Tmp);
    Serial.print(" °C| AcX = "); Serial.print(values.AcX);
    Serial.print(" g| AcY = "); Serial.print(values.AcY);
    Serial.print(" g| AcZ = "); Serial.print(values.AcZ); Serial.println(" g");
  }

  return values;
}


// Networking Functions

// craftRequestContent: This function wraps data from the MPU sensor in IOT2Tangle JSON format
// which can be sent to HTTP-Streams-Gateway inside an HTTP paket
String craftRequestContent() {
  scaleddata values;
  rawdata next_sample;
  scaleddata gyroValues;
  
  setMPU6050scales(MPU_addr, 0b00000000, 0b00010000);

  next_sample = mpu6050Read(MPU_addr, false);
  gyroValues = convertRawToScaled(MPU_addr, next_sample, false);

  // Generate Timestamp
  timeClient.update();
  unsigned long timestamp = timeClient.getEpochTime();

  // IOT2TANGLE JSON format, include gyro parameters
  String content = "{ \"iot2tangle\": [ { \"sensor\": \"Gyroscope\", \"data\": [ { \"x\": \"" + String(gyroValues.GyX) + "\" }, { \"y\": \"" + String(gyroValues.GyY) + "\" }, { \"z\": \"" + String(gyroValues.GyZ) + "\" } ] }, { \"sensor\": \"Acoustic\", \"data\": [ { \"mp\": \"1\" } ] } ], \"device\": \"DEVICE_ID_1\", \"timestamp\": " + String(timestamp) + " }";

  return content;
}


// Setup


// setup: The setup includes initializations of networking and hardware interfaces
void setup(void) {

  // Initialize serial monitor and I2C communication with MPU
  Serial.begin(115200);
  Wire.begin();
  mpu6050Begin(MPU_addr);

  // Make WIFI client and request connection to network
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}


// Loop


// loop: The loop repeatedly sends data from MPU gyrometer to HTTP-Streams-Gateway from IOT2TANGLE
void loop(void) {
    
  // Connect to IOT2TANGLE Streams-HTTP-Gateway
  WiFiClient client;
  if (!client.connect(host, httpPort)) {
    Serial.println("Connection failed");
    return;
  }

  // Craft and send request
  String content = craftRequestContent();
  String request = "POST /sensor_data HTTP/1.1\nHost: " + String(host) + ":" + httpPort + "\nAccept: */*\nContent-Type: application/json\nContent-Length: " + String(content.length()) + "\n\n" + content;
  Serial.println("Requesting POST: ");
  Serial.println(request);
  client.println(request);
  
  // Example Request from Github page
//  String data = "{ \"iot2tangle\": [ { \"sensor\": \"Gyroscope\", \"data\": [ { \"x\": \"4514\" }, { \"y\": \"244\" }, { \"z\": \"-1830\" } ] }, { \"sensor\": \"Acoustic\", \"data\": [ { \"mp\": \"1\" } ] } ], \"device\": \"DEVICE_ID_1\", \"timestamp\": 1558511111 }";
//  Serial.println("Requesting POST: ");
//  String request = "POST /sensor_data HTTP/1.1\nHost: " + String(host) + ":8080\nAccept: */*\nContent-Type: application/json\nContent-Length: " + String(data.length()) + "\n\n" + String(data);
//  client.println(request);
//  Serial.println(request);
  
  delay(3000); // Streams-HTTP-Gateway is not fast enough for 'real-time' updates
  if (client.connected()) {
  }
  Serial.println();
  Serial.println("Closing connection");
}
