#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
// ====== UUIDs ======

// Services
#define SETTING_SERVICE_UUID     "12345000-0000-1000-8000-00805F9B34FB"
#define CONTROLS_SERVICE_UUID    "12346000-0000-1000-8000-00805F9B34FB"
#define DATA_SERVICE_UUID        "12347000-0000-1000-8000-00805F9B34FB"

// ====== Globals ======
BLECharacteristic *tempChar;
BLECharacteristic *humidChar;
BLECharacteristic *windChar;
BLECharacteristic *rainChar;
BLECharacteristic *lightChar;

// Dummy values
int tempVal = 25;
int humidVal = 60;
int windVal = 10;
int rainVal = 0;
int lightVal = 500;

// ====== Helper to create RW characteristic ======
BLECharacteristic* createRWCharacteristic(BLEService *service, const char *uuid) {
  BLECharacteristic *c = service->createCharacteristic(
    uuid,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  c->setValue("0");
  return c;
}

// ====== Helper to create Notify characteristic ======
BLECharacteristic* createNotifyCharacteristic(BLEService *service, const char *uuid) {
  BLECharacteristic *c = service->createCharacteristic(
    uuid,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  c->addDescriptor(new BLE2902());
  return c;
}

// ====== Setup ======
void setup() {
  Serial.begin(115200);

  BLEDevice::init("ESP32-Env-Device");
  BLEServer *server = BLEDevice::createServer();

  // ====== SETTING SERVICE ======
  BLEService *settingService = server->createService(SETTING_SERVICE_UUID);

  createRWCharacteristic(settingService, "12345001-0000-1000-8000-00805F9B34FB"); // Clock
  createRWCharacteristic(settingService, "12345002-0000-1000-8000-00805F9B34FB"); // Backlight timer
  createRWCharacteristic(settingService, "12345003-0000-1000-8000-00805F9B34FB"); // LCD Lock
  createRWCharacteristic(settingService, "12345004-0000-1000-8000-00805F9B34FB"); // Debug Enable

  BLECharacteristic *serviceName = settingService->createCharacteristic(
    "12345010-0000-1000-8000-00805F9B34FB",
    BLECharacteristic::PROPERTY_READ
  );
  serviceName->setValue("Setting Service");

  BLECharacteristic *localName = settingService->createCharacteristic(
    "12345011-0000-1000-8000-00805F9B34FB",
    BLECharacteristic::PROPERTY_READ
  );
  localName->setValue("ESP32 Device");

  settingService->start();

  // ====== CONTROLS SERVICE ======
  BLEService *controlService = server->createService(CONTROLS_SERVICE_UUID);

  createRWCharacteristic(controlService, "12346001-0000-1000-8000-00805F9B34FB");
  createRWCharacteristic(controlService, "12346002-0000-1000-8000-00805F9B34FB");
  createRWCharacteristic(controlService, "12346003-0000-1000-8000-00805F9B34FB");
  createRWCharacteristic(controlService, "12346004-0000-1000-8000-00805F9B34FB");
  createRWCharacteristic(controlService, "12346005-0000-1000-8000-00805F9B34FB");
  createRWCharacteristic(controlService, "12346006-0000-1000-8000-00805F9B34FB");
  createRWCharacteristic(controlService, "12346007-0000-1000-8000-00805F9B34FB");
  createRWCharacteristic(controlService, "12346008-0000-1000-8000-00805F9B34FB");
  createRWCharacteristic(controlService, "12346009-0000-1000-8000-00805F9B34FB");
  createRWCharacteristic(controlService, "1234600A-0000-1000-8000-00805F9B34FB");

  controlService->start();

  // ====== DATA SERVICE ======
  BLEService *dataService = server->createService(DATA_SERVICE_UUID);

  tempChar  = createNotifyCharacteristic(dataService, "12347001-0000-1000-8000-00805F9B34FB");
  humidChar = createNotifyCharacteristic(dataService, "12347002-0000-1000-8000-00805F9B34FB");
  windChar  = createNotifyCharacteristic(dataService, "12347003-0000-1000-8000-00805F9B34FB");
  rainChar  = createNotifyCharacteristic(dataService, "12347004-0000-1000-8000-00805F9B34FB");
  lightChar = createNotifyCharacteristic(dataService, "12347005-0000-1000-8000-00805F9B34FB");

  dataService->start();

  // ====== Advertising ======
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SETTING_SERVICE_UUID);
  advertising->addServiceUUID(CONTROLS_SERVICE_UUID);
  advertising->addServiceUUID(DATA_SERVICE_UUID);
  advertising->start();

  Serial.println("BLE services started...");
}

// ====== Loop ======
void loop() {
  // Update dummy values
  tempVal++;
  humidVal++;
  windVal++;

  if (tempVal > 35) tempVal = 25;
  if (humidVal > 80) humidVal = 60;
  if (windVal > 20) windVal = 5;

  // Send notifications
  tempChar->setValue(tempVal);
  tempChar->notify();

  humidChar->setValue(humidVal);
  humidChar->notify();

  windChar->setValue(windVal);
  windChar->notify();

  rainChar->setValue(rainVal);
  rainChar->notify();

  lightChar->setValue(lightVal);
  lightChar->notify();

  Serial.println("Notifying values...");

  delay(2000);
}