#include <NimBLEDevice.h>
//#include <string.h>
// ====== UUIDs ======

// 12346008-0000-1000-8000-00805F9B34FB

// Services

#define UUID_SERVICE_SETTING  "12345000-0000-1000-8000-00805F9B34FB"
#define UUID_SERVICE_CONTROLS "12346000-0000-1000-8000-00805F9B34FB"
#define UUID_SERVICE_DATA     "12347000-0000-1000-8000-00805F9B34FB"


// ====== Globals ====== 
// Settings
NimBLECharacteristic *clockChar;
NimBLECharacteristic *backLightTimerChar;
NimBLECharacteristic *lcdLockChar;
NimBLECharacteristic *debugChar;
NimBLECharacteristic *serviceNameChar;
NimBLECharacteristic *localNameChar;
// Cotrols
NimBLECharacteristic *tempSetpointChar;   
NimBLECharacteristic *tempHysteresisChar; 
NimBLECharacteristic *humidSetpointChar;  
NimBLECharacteristic *humidHysteresisChar;
NimBLECharacteristic *windMaxChar;        
NimBLECharacteristic *lightMinChar;       
NimBLECharacteristic *lightOverrideChar;  
NimBLECharacteristic *lightSetChar;       
NimBLECharacteristic *windowOverrideChar; 
NimBLECharacteristic *windowSetChar;      
// Data Reading
NimBLECharacteristic *tempChar;
NimBLECharacteristic *humidChar;
NimBLECharacteristic *windChar;
NimBLECharacteristic *rainChar;
NimBLECharacteristic *lightChar;

// Dummy values
int  clockVal = 100;
int  backLightTimerVal = 101;
bool lcdLockVal = true;
bool debugVal = true;
const char*serviceNameVal = "Setting Service";
const char*localNameVal = "ESP32 Device";

int  tempSetpointVal = 102;   
int  tempHysteresisVal = 103; 
int  humidSetpointVal = 104;  
int  humidHysteresisVal = 105;
int  windMaxVal = 106;        
int  lightMinVal = 107;       
bool lightOverrideVal = false;  
bool lightSetVal = false;       
bool windowOverrideVal = true; 
bool windowSetVal = true;      

int tempVal = 25;
int humidVal = 60;
int windVal = 10;
int rainVal = 0;
int lightVal = 500;

bool OS_Connected = false; // OS mean one shot here to stop the sytem repeated say it disconnected 
bool OS_Disconnected = true;

// ====== Helper to create RW characteristic ======
NimBLECharacteristic* createRWCharacteristic(NimBLEService *service, const char *uuid) {
  NimBLECharacteristic *c = service->createCharacteristic(
    uuid,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
  );
  c->setValue("0");
  return c;
}

NimBLECharacteristic* createRCharacteristic(NimBLEService *service, const char *uuid) {
  NimBLECharacteristic *c = service->createCharacteristic(
    uuid,
    NIMBLE_PROPERTY::READ
  );
  c->setValue("0");
  return c;
}

NimBLECharacteristic* createNotifyCharacteristic(NimBLEService *service, const char *uuid) {
  NimBLECharacteristic *c = service->createCharacteristic(
    uuid,
    NIMBLE_PROPERTY::NOTIFY
  );
  // c->addDescriptor(new NimBLE2902());
  return c;
}


// ====== Setup ======
void setup() {
  Serial.begin(9600);
  NimBLEDevice::init("char-Data");
  NimBLEServer *server = NimBLEDevice::createServer();

  // ====== SETTING SERVICE ======
  BLEService *settingService = server->createService(UUID_SERVICE_SETTING);
  clockChar =         createRWCharacteristic(settingService, "12345001-0000-1000-8000-00805F9B34FB"); //Clock                
  backLightTimerChar= createRWCharacteristic(settingService, "12345002-0000-1000-8000-00805F9B34FB"); //Back-light-timer     
  lcdLockChar =       createRWCharacteristic(settingService, "12345003-0000-1000-8000-00805F9B34FB"); //LCD-Lock             
  debugChar =         createRWCharacteristic(settingService, "12345004-0000-1000-8000-00805F9B34FB"); //Debug-Enable         
  serviceNameChar =   createRCharacteristic (settingService, "12345010-0000-1000-8000-00805F9B34FB"); //Service-name (debug)
  localNameChar =     createRCharacteristic (settingService, "12345011-0000-1000-8000-00805F9B34FB"); //Local-name (debug)
  settingService->start();

  // ====== CONTROLS SERVICE ======
  NimBLEService *controlService = server->createService(UUID_SERVICE_CONTROLS);

  tempSetpointChar   = createRWCharacteristic(controlService, "12346001-0000-1000-8000-00805F9B34FB"); // Temp-Setpoint   
  tempHysteresisChar = createRWCharacteristic(controlService, "12346002-0000-1000-8000-00805F9B34FB"); // Temp-hysteresis 
  humidSetpointChar  = createRWCharacteristic(controlService, "12346003-0000-1000-8000-00805F9B34FB"); // Humid-Setpoint  
  humidHysteresisChar= createRWCharacteristic(controlService, "12346004-0000-1000-8000-00805F9B34FB"); // Humid-Hysteresis
  windMaxChar        = createRWCharacteristic(controlService, "12346005-0000-1000-8000-00805F9B34FB"); // Wind-Max        
  lightMinChar       = createRWCharacteristic(controlService, "12346006-0000-1000-8000-00805F9B34FB"); // Light-Min       
  lightOverrideChar  = createRWCharacteristic(controlService, "12346007-0000-1000-8000-00805F9B34FB"); // Light-override  
  lightSetChar       = createRWCharacteristic(controlService, "12346008-0000-1000-8000-00805F9B34FB"); // Light-Set       
  windowOverrideChar = createRWCharacteristic(controlService, "12346009-0000-1000-8000-00805F9B34FB"); // Window-override 
  windowSetChar      = createRWCharacteristic(controlService, "1234600A-0000-1000-8000-00805F9B34FB"); // Window-Set      
  controlService->start();

  // ====== DATA SERVICE ======
  NimBLEService *dataService = server->createService(UUID_SERVICE_DATA);

  tempChar  = createNotifyCharacteristic(dataService, "12347001-0000-1000-8000-00805F9B34FB"); // Temp  
  humidChar = createNotifyCharacteristic(dataService, "12347002-0000-1000-8000-00805F9B34FB"); // Humid 
  windChar  = createNotifyCharacteristic(dataService, "12347003-0000-1000-8000-00805F9B34FB"); // Wind  
  rainChar  = createNotifyCharacteristic(dataService, "12347004-0000-1000-8000-00805F9B34FB"); // Rain  
  lightChar = createNotifyCharacteristic(dataService, "12347005-0000-1000-8000-00805F9B34FB"); // Light 
  dataService->start();

  // ====== Advertising ======
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(UUID_SERVICE_SETTING);
  advertising->addServiceUUID(UUID_SERVICE_CONTROLS);
  advertising->addServiceUUID(UUID_SERVICE_DATA);
  NimBLEDevice::startAdvertising();

  Serial.println("BLE services started...");
  
  pinMode(2, OUTPUT);
}

// ====== Loop ======
void loop() {

  // Update dummy values
  int number_connections = NimBLEDevice::getServer()->getConnectedCount();
  Serial.print(number_connections);
  Serial.print(" Bool: ");
  Serial.println(OS_Connected);
  if (number_connections > 0 && OS_Connected == false) {
    digitalWrite(2, HIGH);
    OS_Connected = true;
    Serial.println("Device connected");
  } 
  if (number_connections == 0 && OS_Connected == true){
    digitalWrite(2, LOW);
    OS_Connected = false;
    Serial.println("Device disconnected");
    NimBLEDevice::startAdvertising();
  }
  clockChar           ->setValue(clockVal);
  backLightTimerChar  ->setValue(backLightTimerVal);
  lcdLockChar         ->setValue(lcdLockVal);
  debugChar           ->setValue(debugVal);
  serviceNameChar     ->setValue(serviceNameVal);
  localNameChar       ->setValue(localNameVal);

  tempSetpointChar    ->setValue(tempSetpointVal);
  tempHysteresisChar  ->setValue(tempHysteresisVal);
  humidSetpointChar   ->setValue(humidSetpointVal);
  humidHysteresisChar ->setValue(humidHysteresisVal);
  windMaxChar         ->setValue(windMaxVal);
  lightMinChar        ->setValue(lightMinVal);
  lightOverrideChar   ->setValue(lightOverrideVal);
  lightSetChar        ->setValue(lightSetVal);
  windowOverrideChar  ->setValue(windowOverrideVal);
  windowSetChar       ->setValue(windowSetVal);

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