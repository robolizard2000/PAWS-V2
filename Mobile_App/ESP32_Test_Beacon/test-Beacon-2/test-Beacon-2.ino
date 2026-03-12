#include <NimBLEDevice.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

const int debugPin = 2;

NimBLEServer* pServer;
NimBLECharacteristic* pCharacteristic;

bool deviceConnected = false;
unsigned long lastActivity = 0;
const unsigned long resetTimeout = 60000; // 60s watchdog

class ServerCallbacks : public NimBLEServerCallbacks {

    void onConnect(NimBLEServer* pServer) {
        deviceConnected = true;
        digitalWrite(debugPin, 1);
        lastActivity = millis();

        Serial.println("Client connected");
    }

    void onDisconnect(NimBLEServer* pServer) {
        deviceConnected = false;
        digitalWrite(debugPin, 0);

        Serial.println("Client disconnected");

        delay(200);
        NimBLEDevice::startAdvertising();

        Serial.println("Advertising restarted");
    }
};

void setup() {

    Serial.begin(115200);
    pinMode(debugPin, OUTPUT);

    NimBLEDevice::init("ESP32_BLE_Test");

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
        NIMBLE_PROPERTY::WRITE
    );

    pCharacteristic->setValue("Hello BLE");

    pService->start();

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    //pAdvertising->setScanResponse(true);

    NimBLEDevice::startAdvertising();

    Serial.println("BLE NimBLE server advertising...");

    lastActivity = millis();
}

void loop() {
    if (deviceConnected == true){
        digitalWrite(debugPin, 1);
        Serial.println("Connected");
    }
    else{
        digitalWrite(debugPin, 0);
        Serial.println("Disconnected");
    }
    // Watchdog for BLE advertising recovery
    if (!deviceConnected && millis() - lastActivity > resetTimeout) {

        Serial.println("BLE idle watchdog triggered");

        NimBLEDevice::stopAdvertising();
        delay(200);
        NimBLEDevice::startAdvertising();

        lastActivity = millis();
    }

    delay(1000);
}