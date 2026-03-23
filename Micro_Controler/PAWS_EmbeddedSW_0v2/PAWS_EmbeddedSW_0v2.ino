// ============================================================
//  PAWS - Automated Environmental Control System
//  ESP32 Embedded Software  |  Version 0.2
// ============================================================
//
//  Hardware: ESP32 Dev Board
//
//  Inputs:   AHT25 Temp/Humidity (I2C)
//            Wind sensor (PWM pulse-width)
//            Rain sensor (Digital output)
//            Ambient light (Analogue / ADC)
//            Rotary encoder + push-button
//
//  Outputs:  Servo  - window open/close
//            Relay1 - heater
//            Relay2 - cooler
//            LED1   - BLE connection status
//            LED2   - automatic lighting indicator
//            LCD    - 20x4 I2C display
//
//  Comms:    Bluetooth Low Energy (BLE) to mobile app
// ============================================================

// ---- Libraries --------------------------------------------
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>          
#include <LiquidCrystal_I2C.h>       // 20x4 I2C LCD
#include <ESP32Servo.h>              // ESP32-compatible servo library
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ============================================================
//  PIN DEFINITIONS
// ============================================================
// I2C bus - AHT25 + LCD
#define PIN_SDA           21
#define PIN_SCL           22

// Sensors
#define PIN_WIND_PWM       4   // Wind sensor PWM input
#define PIN_RAIN_DIG       5   // Rain sensor digital output (HIGH = rain)
#define PIN_LIGHT_ADC     34   // Ambient light LDR (ADC1 input only)

// Outputs
#define PIN_SERVO          13  // Window servo
#define PIN_RELAY1         25  // Heater relay (active LOW)
#define PIN_RELAY2         26  // Cooler relay (active LOW)
#define PIN_LED_BLE        27  // BLE status LED
#define PIN_LED_LIGHT      14  // Lighting indicator LED

// Rotary encoder
#define PIN_ENC_CLK        32
#define PIN_ENC_DT         33
#define PIN_ENC_SW         35  // Push-button 

// ============================================================
//  BLE CONFIGURATION
// ============================================================
// Three services matching the agreed UUID map:
//   12345000-... Settings
//   12346000-... Controls
//   12347000-... Data Reading (sensor output → app)
//
// All Int32 values are sent as 4-byte little-endian binary.
// Bool values are sent as 1-byte (0x00 / 0x01).
// Char values are sent as raw UTF-8 bytes (max 20).
//
// Scaling (encode on write, decode on read):
//   Clock          : raw seconds          (Int32)
//   Backlight TMR  : minutes × 10         (Int32)  e.g. 20 = 2.0 min
//   Temp setpoint  : °C × 10             (Int32)  e.g. 220 = 22.0 °C
//   Temp hysteresis: °C × 10             (Int32)
//   Humid setpoint : % direct            (Int32)
//   Humid hysteresis:%direct             (Int32)
//   Wind-Max       : mph × 10            (Int32)  (future - currently enum index)
//   Light-Min      : % × 1              (Int32)  0-100 mapped from ADC
//   Temp data      : °C × 100           (Int32)  e.g. 2250 = 22.50 °C
//   Humid data     : % × 100            (Int32)  e.g. 5500 = 55.00 %
//   Wind data      : mph × 10           (Int32)
//   Rain data      : Int32 (0=dry, scaled for future analogue)
//   Light data     : % × 10            (Int32)  ADC mapped 0-4095 → 0-1000

// ---- Service: Settings (12345000) ----
#define BLE_SVC_SETTINGS_UUID     "12345000-0000-1000-8000-00805F9B34FB"
#define BLE_CHAR_CLOCK_UUID       "12345001-0000-1000-8000-00805F9B34FB"  // R|W|N Int32  seconds
#define BLE_CHAR_BL_TMR_UUID      "12345002-0000-1000-8000-00805F9B34FB"  // R|W|N Int32  minutes×10
#define BLE_CHAR_LCD_LOCK_UUID    "12345003-0000-1000-8000-00805F9B34FB"  // R|W|N Bool
#define BLE_CHAR_DEBUG_UUID       "12345004-0000-1000-8000-00805F9B34FB"  // R|W|N Bool
#define BLE_CHAR_SVC_NAME_UUID    "12345010-0000-1000-8000-00805F9B34FB"  // W     Char(20) service name (debug)
#define BLE_CHAR_LOCAL_NAME_UUID  "12345011-0000-1000-8000-00805F9B34FB"  // W     Char(20) local name   (debug)

// ---- Service: Controls (12346000) ----
#define BLE_SVC_CONTROLS_UUID     "12346000-0000-1000-8000-00805F9B34FB"
#define BLE_CHAR_TEMP_SP_UUID     "12346001-0000-1000-8000-00805F9B34FB"  // R|W|N Int32  °C×10
#define BLE_CHAR_TEMP_HYS_UUID    "12346002-0000-1000-8000-00805F9B34FB"  // R|W|N Int32  °C×10
#define BLE_CHAR_HUM_SP_UUID      "12346003-0000-1000-8000-00805F9B34FB"  // R|W|N Int32  %
#define BLE_CHAR_HUM_HYS_UUID     "12346004-0000-1000-8000-00805F9B34FB"  // R|W|N Int32  %
#define BLE_CHAR_WIND_MAX_UUID    "12346005-0000-1000-8000-00805F9B34FB"  // R|W|N Int32  mph×10
#define BLE_CHAR_LIGHT_MIN_UUID   "12346006-0000-1000-8000-00805F9B34FB"  // R|W|N Int32  %
#define BLE_CHAR_LIGHT_OVR_UUID   "12346007-0000-1000-8000-00805F9B34FB"  // R|W|N Bool
#define BLE_CHAR_LIGHT_SET_UUID   "12346008-0000-1000-8000-00805F9B34FB"  // R|W|N Bool
#define BLE_CHAR_WIN_OVR_UUID     "12346009-0000-1000-8000-00805F9B34FB"  // R|W|N Bool
#define BLE_CHAR_WIN_SET_UUID     "1234600A-0000-1000-8000-00805F9B34FB"  // R|W|N Bool

// ---- Service: Data Reading (12347000) ----
#define BLE_SVC_DATA_UUID         "12347000-0000-1000-8000-00805F9B34FB"
#define BLE_CHAR_TEMP_UUID        "12347001-0000-1000-8000-00805F9B34FB"  // Notify Int32  °C×100
#define BLE_CHAR_HUMID_UUID       "12347002-0000-1000-8000-00805F9B34FB"  // Notify Int32  %×100
#define BLE_CHAR_WIND_UUID        "12347003-0000-1000-8000-00805F9B34FB"  // Notify Int32  mph×10
#define BLE_CHAR_RAIN_UUID        "12347004-0000-1000-8000-00805F9B34FB"  // Notify Int32  (0=dry, scaled future)
#define BLE_CHAR_LIGHT_UUID       "12347005-0000-1000-8000-00805F9B34FB"  // Notify Int32  %×10

// ============================================================
//  CONSTANTS
// ============================================================
// Wind speed lookup table (pulse width in µs → wind category)
// Adjust boundaries once you have real calibration data.
// Wider pulse = lower speed
enum WindLevel { WIND_CALM = 0, WIND_LOW, WIND_MEDIUM, WIND_HIGH, WIND_STORM };
const char* windLevelStr[] = { "Calm", "Low", "Medium", "High", "Storm" };

// Servo positions
#define SERVO_WINDOW_OPEN    90    // degrees - adjust as required
#define SERVO_WINDOW_CLOSED   0    // degrees
#define SERVO_SPEED_DELAY    20    // ms per degree step (controls motor speed)

// LCD dimensions
#define LCD_COLS  20
#define LCD_ROWS   4
#define LCD_I2C_ADDR 0x27          // typical address - check specific module

// ADC
#define ADC_MAX   4095             // ESP32 12-bit ADC

// ============================================================
//  SETTINGS  (persistent - TODO: save to Preferences/NVS)
// ============================================================
int32_t measurementClk      = 10;    // seconds between sensor reads / BLE updates
int32_t backlightSleepTMR   = 20;    // minutes×10 (20 = 2.0 min)
bool    LCDLock             = false;
bool    debugEN             = false;

String  serviceName = "PAWS_BLE";
String  localName   = "PAWS";

// ============================================================
//  CONTROL VARIABLES  (all stored in the scaled Int32 format)
// ============================================================
int32_t tempSetPoint        = 220;   // °C×10  (220 = 22.0 °C)
int32_t tempHysteresis      = 10;    // °C×10  (10  =  1.0 °C)
int32_t humidSetPoint       = 55;    // % direct
int32_t humidHysteresis     = 5;     // % direct
int32_t windMax             = 300;   // mph×10 (300 = 30.0 mph) - calibrate to your sensor
int32_t lightMin            = 20;    // % (0-100) below which lights turn on

bool  lightOverride       = false;
bool  lightSet            = false;
bool  windowOverride      = false;
bool  windowSet           = false;

bool  relay1Set           = false;   // heater state
bool  relay2Set           = false;   // cooler state

// ============================================================
//  SENSOR / DATA VARIABLES
// ============================================================
float tempDegC            = 0.0;
float tempDegCavg         = 0.0;
int   humidPercent        = 0;
int   windSpeedRaw        = 0;     // raw pulse-width µs
WindLevel windLevel       = WIND_CALM;
bool  isRaining           = false;
int   lightLevel          = 0;     // ADC counts (0–4095)

// ============================================================
//  TIMING
// ============================================================
unsigned long lastMeasurementMs = 0;
unsigned long lastDisplayMs     = 0;
unsigned long lastEncoderMs     = 0;
unsigned long backlightOnMs     = 0;  // millis() when backlight was last triggered
bool          backlightOn       = true;

// ============================================================
//  HARDWARE OBJECTS
// ============================================================
Adafruit_AHTX0    aht;
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
Servo             windowServo;

// ============================================================
//  BLE OBJECTS
// ============================================================
BLEServer* pServer = nullptr;

// -- Data Reading service characteristics (Notify → app) --
BLECharacteristic* pCharTemp      = nullptr;
BLECharacteristic* pCharHumid     = nullptr;
BLECharacteristic* pCharWind      = nullptr;
BLECharacteristic* pCharRain      = nullptr;
BLECharacteristic* pCharLight     = nullptr;

// -- Settings service characteristics (R|W|N) --
BLECharacteristic* pCharClock     = nullptr;
BLECharacteristic* pCharBlTmr     = nullptr;
BLECharacteristic* pCharLcdLock   = nullptr;
BLECharacteristic* pCharDebug     = nullptr;

// -- Controls service characteristics (R|W|N) --
BLECharacteristic* pCharTempSP    = nullptr;
BLECharacteristic* pCharTempHys   = nullptr;
BLECharacteristic* pCharHumSP     = nullptr;
BLECharacteristic* pCharHumHys    = nullptr;
BLECharacteristic* pCharWindMax   = nullptr;
BLECharacteristic* pCharLightMin  = nullptr;
BLECharacteristic* pCharLightOvr  = nullptr;
BLECharacteristic* pCharLightSet  = nullptr;
BLECharacteristic* pCharWinOvr    = nullptr;
BLECharacteristic* pCharWinSet    = nullptr;

bool bleConnected     = false;
bool blePrevConnected = false;

// ============================================================
//  ENCODER STATE
// ============================================================
volatile int  encoderPos     = 0;
volatile bool encoderPressed = false;
int           lastEncoderPos = 0;
int           lastEncCLK     = HIGH;

// ============================================================
//  LCD MENU SYSTEM
// ============================================================
// Simple two-level menu: top-level pages selectable by encoder.
// Each page shows sensor data or a specific setting.
enum MenuPage {
  PAGE_HOME = 0,
  PAGE_TEMP,
  PAGE_HUMID,
  PAGE_WIND,
  PAGE_RAIN,
  PAGE_LIGHT,
  PAGE_WINDOW,
  PAGE_SETTINGS,
  PAGE_COUNT    // keep last - used for wrap-around
};
MenuPage currentPage = PAGE_HOME;
bool     inEditMode  = false;  // true when encoder adjusts a value

// ============================================================
//  WINDOW STATE
// ============================================================
bool windowIsOpen     = false;
int  currentServoDeg  = SERVO_WINDOW_CLOSED;

// ============================================================
//  BLE HELPER: encode / decode Int32 as 4-byte little-endian
// ============================================================
void bleSetInt32(BLECharacteristic* pChar, int32_t value) {
  uint8_t buf[4];
  buf[0] = (value)       & 0xFF;
  buf[1] = (value >> 8)  & 0xFF;
  buf[2] = (value >> 16) & 0xFF;
  buf[3] = (value >> 24) & 0xFF;
  pChar->setValue(buf, 4);
}

int32_t bleGetInt32(BLECharacteristic* pChar) {
  String raw = pChar->getValue();
  if (raw.length() < 4) return 0;
  int32_t v = (uint8_t)raw[0]
            | ((uint8_t)raw[1] << 8)
            | ((uint8_t)raw[2] << 16)
            | ((uint8_t)raw[3] << 24);
  return v;
}

void bleSetBool(BLECharacteristic* pChar, bool value) {
  uint8_t buf[1] = { value ? (uint8_t)0x01 : (uint8_t)0x00 };
  pChar->setValue(buf, 1);
}

bool bleGetBool(BLECharacteristic* pChar) {
  String raw = pChar->getValue();
  if (raw.length() < 1) return false;
  return ((uint8_t)raw[0] != 0x00);
}

// ============================================================
//  BLE SERVER CALLBACKS
// ============================================================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pSvr) {
    bleConnected = true;
    digitalWrite(PIN_LED_BLE, HIGH);
    Serial.println("[BLE] Client connected");
  }
  void onDisconnect(BLEServer* pSvr) {
    bleConnected = false;
    digitalWrite(PIN_LED_BLE, LOW);
    Serial.println("[BLE] Client disconnected - restarting advertising");
    BLEDevice::startAdvertising();
  }
};

// ============================================================
//  BLE WRITE CALLBACK
//  Called immediately when the app writes any R|W characteristic.
//  Int32 values arrive as 4-byte little-endian binary.
//  Bool values arrive as 1 byte (0x00 or 0x01).
// ============================================================
class SettingWriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    String uuid = pChar->getUUID().toString();
    uuid.toUpperCase();
    String val = pChar->getValue();
    if (val.length() == 0) return;

    // ---- Settings service ----
    if (uuid == String(BLE_CHAR_CLOCK_UUID).toUpperCase()) {
      measurementClk = bleGetInt32(pChar);
      Serial.printf("[BLE] Clock = %d s\n", measurementClk);

    } else if (uuid == String(BLE_CHAR_BL_TMR_UUID).toUpperCase()) {
      backlightSleepTMR = bleGetInt32(pChar);
      Serial.printf("[BLE] BacklightTMR = %d (min*10)\n", backlightSleepTMR);

    } else if (uuid == String(BLE_CHAR_LCD_LOCK_UUID).toUpperCase()) {
      LCDLock = bleGetBool(pChar);
      Serial.printf("[BLE] LCDLock = %d\n", LCDLock);

    } else if (uuid == String(BLE_CHAR_DEBUG_UUID).toUpperCase()) {
      debugEN = bleGetBool(pChar);
      Serial.printf("[BLE] DebugEN = %d\n", debugEN);

    } else if (uuid == String(BLE_CHAR_SVC_NAME_UUID).toUpperCase()) {
      // Char(20) - service name debug write, store only (restart needed to take effect)
      serviceName = val;
      Serial.printf("[BLE] ServiceName = %s\n", serviceName.c_str());

    } else if (uuid == String(BLE_CHAR_LOCAL_NAME_UUID).toUpperCase()) {
      localName = val;
      Serial.printf("[BLE] LocalName = %s\n", localName.c_str());

    // ---- Controls service ----
    } else if (uuid == String(BLE_CHAR_TEMP_SP_UUID).toUpperCase()) {
      tempSetPoint = bleGetInt32(pChar);
      Serial.printf("[BLE] TempSetpoint = %d (°C*10)\n", tempSetPoint);

    } else if (uuid == String(BLE_CHAR_TEMP_HYS_UUID).toUpperCase()) {
      tempHysteresis = bleGetInt32(pChar);
      Serial.printf("[BLE] TempHysteresis = %d (°C*10)\n", tempHysteresis);

    } else if (uuid == String(BLE_CHAR_HUM_SP_UUID).toUpperCase()) {
      humidSetPoint = bleGetInt32(pChar);
      Serial.printf("[BLE] HumidSetpoint = %d %%\n", humidSetPoint);

    } else if (uuid == String(BLE_CHAR_HUM_HYS_UUID).toUpperCase()) {
      humidHysteresis = bleGetInt32(pChar);
      Serial.printf("[BLE] HumidHysteresis = %d %%\n", humidHysteresis);

    } else if (uuid == String(BLE_CHAR_WIND_MAX_UUID).toUpperCase()) {
      windMax = bleGetInt32(pChar);
      Serial.printf("[BLE] WindMax = %d (mph*10)\n", windMax);

    } else if (uuid == String(BLE_CHAR_LIGHT_MIN_UUID).toUpperCase()) {
      lightMin = bleGetInt32(pChar);
      Serial.printf("[BLE] LightMin = %d %%\n", lightMin);

    } else if (uuid == String(BLE_CHAR_LIGHT_OVR_UUID).toUpperCase()) {
      lightOverride = bleGetBool(pChar);
      Serial.printf("[BLE] LightOverride = %d\n", lightOverride);

    } else if (uuid == String(BLE_CHAR_LIGHT_SET_UUID).toUpperCase()) {
      lightSet = bleGetBool(pChar);
      Serial.printf("[BLE] LightSet = %d\n", lightSet);

    } else if (uuid == String(BLE_CHAR_WIN_OVR_UUID).toUpperCase()) {
      windowOverride = bleGetBool(pChar);
      Serial.printf("[BLE] WindowOverride = %d\n", windowOverride);

    } else if (uuid == String(BLE_CHAR_WIN_SET_UUID).toUpperCase()) {
      windowSet = bleGetBool(pChar);
      Serial.printf("[BLE] WindowSet = %d\n", windowSet);
    }
  }
};

// ============================================================
//  HELPER: add a characteristic to a service
// ============================================================
BLECharacteristic* addCharacteristic(BLEService* svc,
                                     const char* uuid,
                                     uint32_t    properties,
                                     bool        addNotify = false,
                                     BLECharacteristicCallbacks* cb = nullptr)
{
  BLECharacteristic* pChar = svc->createCharacteristic(uuid, properties);
  if (addNotify) {
    pChar->addDescriptor(new BLE2902());
  }
  if (cb) {
    pChar->setCallbacks(cb);
  }
  return pChar;
}

// ============================================================
//  FUNCTION PROTOTYPES
// ============================================================
void     initBLE();
void     initLCD();
void     initSensors();
void     initOutputs();
void     initEncoder();

void     readSensors();
void     readBLE();
void     writeBLE();
void     autoControl();
void     displayUpdate();

void     moveServoTo(int targetDeg);
void     setRelay(int pin, bool state);
void     setWindow(bool open);

WindLevel measureWindLevel();
int       measureWindPulseWidth();

void     encoderISR();
void     encoderButtonISR();
void     handleEncoder();

void     lcdPrintPage(MenuPage page);
void     lcdClear();
String   formatFloat(float val, int dp);

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("Starting up...");

  Wire.begin(PIN_SDA, PIN_SCL);

  initOutputs();
  initSensors();
  initLCD();
  initEncoder();
  initBLE();

  backlightOnMs = millis();

  Serial.println("Init complete");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  // --- Encoder handling (non-ISR debounce + action) ---
  handleEncoder();

  // --- LCD backlight timeout ---
  // backlightSleepTMR is stored as minutes×10; convert to ms
  unsigned long blTimeoutMs = (unsigned long)(backlightSleepTMR) * 6000UL; // ×10 min × 60000ms / 10
  if (backlightOn && (now - backlightOnMs >= blTimeoutMs)) {
    lcd.noBacklight();
    backlightOn = false;
  }

  // --- Periodic measurement cycle ---
  if ((now - lastMeasurementMs) >= (unsigned long)measurementClk * 1000UL) {
    lastMeasurementMs = now;

    readSensors();
    readBLE();        // receive any pending settings writes
    writeBLE();       // push sensor data + current settings to app
    autoControl();    // evaluate conditions and action outputs
  }

  // --- LCD update (runs every loop so menus feel responsive) ---
  displayUpdate();

  // --- BLE reconnect handling ---
  if (!bleConnected && blePrevConnected) {
    blePrevConnected = false;
  }
  if (bleConnected && !blePrevConnected) {
    blePrevConnected = true;
  }
}

// ============================================================
//  INIT FUNCTIONS
// ============================================================
void initOutputs() {
  pinMode(PIN_RELAY1,    OUTPUT);  digitalWrite(PIN_RELAY1, HIGH);  // relay off (active LOW?)
  pinMode(PIN_RELAY2,    OUTPUT);  digitalWrite(PIN_RELAY2, HIGH);
  pinMode(PIN_LED_BLE,   OUTPUT);  digitalWrite(PIN_LED_BLE, LOW);
  pinMode(PIN_LED_LIGHT, OUTPUT);  digitalWrite(PIN_LED_LIGHT, LOW);

  windowServo.attach(PIN_SERVO);
  windowServo.write(SERVO_WINDOW_CLOSED);
  Serial.println("Outputs ready");
}

void initSensors() {
  // AHT25
  if (!aht.begin()) {
    Serial.println("ERROR: AHT25 not found - check wiring");
  } else {
    Serial.println("AHT25 ready");
  }

  // Rain sensor - digital input
  pinMode(PIN_RAIN_DIG, INPUT);

  // Wind - PWM input (pulse width measurement via pulseIn)
  pinMode(PIN_WIND_PWM, INPUT);

  // Light - ADC input (no pinMode needed for ADC1 on ESP32)
  Serial.println("Sensors ready");
}

void initLCD() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("   PAWS  v0.2   ");
  lcd.setCursor(0, 1); lcd.print("  Initialising..  ");
  delay(1500);
  lcd.clear();
  Serial.println("LCD ready");
}

void initEncoder() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encoderISR,    CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_SW),  encoderButtonISR, FALLING);

  lastEncCLK = digitalRead(PIN_ENC_CLK);
  Serial.println("[INIT] Encoder ready");
}

void initBLE() {
  BLEDevice::init(localName.c_str());
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  SettingWriteCallback* writeCB = new SettingWriteCallback();

  // Property shorthand
  uint32_t propRN  = BLECharacteristic::PROPERTY_READ  |
                     BLECharacteristic::PROPERTY_NOTIFY;
  uint32_t propRWN = BLECharacteristic::PROPERTY_READ  |
                     BLECharacteristic::PROPERTY_WRITE  |
                     BLECharacteristic::PROPERTY_NOTIFY;
  uint32_t propW   = BLECharacteristic::PROPERTY_WRITE;
  uint32_t propN   = BLECharacteristic::PROPERTY_READ  |
                     BLECharacteristic::PROPERTY_NOTIFY;

  // ---- Service: Settings (12345000) ----
  // 6 characteristics  → allocate at least 12 handles (×2 each + descriptors)
  BLEService* pSvcSettings = pServer->createService(BLEUUID(BLE_SVC_SETTINGS_UUID), 20);

  pCharClock   = addCharacteristic(pSvcSettings, BLE_CHAR_CLOCK_UUID,      propRWN, true, writeCB);
  pCharBlTmr   = addCharacteristic(pSvcSettings, BLE_CHAR_BL_TMR_UUID,     propRWN, true, writeCB);
  pCharLcdLock = addCharacteristic(pSvcSettings, BLE_CHAR_LCD_LOCK_UUID,   propRWN, true, writeCB);
  pCharDebug   = addCharacteristic(pSvcSettings, BLE_CHAR_DEBUG_UUID,      propRWN, true, writeCB);
  addCharacteristic(pSvcSettings, BLE_CHAR_SVC_NAME_UUID,   propW, false, writeCB);
  addCharacteristic(pSvcSettings, BLE_CHAR_LOCAL_NAME_UUID, propW, false, writeCB);

  // Initialise readable values
  bleSetInt32(pCharClock,   measurementClk);
  bleSetInt32(pCharBlTmr,   backlightSleepTMR);
  bleSetBool (pCharLcdLock, LCDLock);
  bleSetBool (pCharDebug,   debugEN);

  pSvcSettings->start();

  // ---- Service: Controls (12346000) ----
  // 10 characteristics
  BLEService* pSvcControls = pServer->createService(BLEUUID(BLE_SVC_CONTROLS_UUID), 30);

  pCharTempSP   = addCharacteristic(pSvcControls, BLE_CHAR_TEMP_SP_UUID,  propRWN, true, writeCB);
  pCharTempHys  = addCharacteristic(pSvcControls, BLE_CHAR_TEMP_HYS_UUID, propRWN, true, writeCB);
  pCharHumSP    = addCharacteristic(pSvcControls, BLE_CHAR_HUM_SP_UUID,   propRWN, true, writeCB);
  pCharHumHys   = addCharacteristic(pSvcControls, BLE_CHAR_HUM_HYS_UUID,  propRWN, true, writeCB);
  pCharWindMax  = addCharacteristic(pSvcControls, BLE_CHAR_WIND_MAX_UUID, propRWN, true, writeCB);
  pCharLightMin = addCharacteristic(pSvcControls, BLE_CHAR_LIGHT_MIN_UUID,propRWN, true, writeCB);
  pCharLightOvr = addCharacteristic(pSvcControls, BLE_CHAR_LIGHT_OVR_UUID,propRWN, true, writeCB);
  pCharLightSet = addCharacteristic(pSvcControls, BLE_CHAR_LIGHT_SET_UUID,propRWN, true, writeCB);
  pCharWinOvr   = addCharacteristic(pSvcControls, BLE_CHAR_WIN_OVR_UUID,  propRWN, true, writeCB);
  pCharWinSet   = addCharacteristic(pSvcControls, BLE_CHAR_WIN_SET_UUID,  propRWN, true, writeCB);

  // Initialise readable values
  bleSetInt32(pCharTempSP,   tempSetPoint);
  bleSetInt32(pCharTempHys,  tempHysteresis);
  bleSetInt32(pCharHumSP,    humidSetPoint);
  bleSetInt32(pCharHumHys,   humidHysteresis);
  bleSetInt32(pCharWindMax,  windMax);
  bleSetInt32(pCharLightMin, lightMin);
  bleSetBool (pCharLightOvr, lightOverride);
  bleSetBool (pCharLightSet, lightSet);
  bleSetBool (pCharWinOvr,   windowOverride);
  bleSetBool (pCharWinSet,   windowSet);

  pSvcControls->start();

  // ---- Service: Data Reading (12347000) ----
  // 5 characteristics - Notify only (app subscribes, ESP32 pushes)
  BLEService* pSvcData = pServer->createService(BLEUUID(BLE_SVC_DATA_UUID), 20);

  pCharTemp  = addCharacteristic(pSvcData, BLE_CHAR_TEMP_UUID,  propN, true);
  pCharHumid = addCharacteristic(pSvcData, BLE_CHAR_HUMID_UUID, propN, true);
  pCharWind  = addCharacteristic(pSvcData, BLE_CHAR_WIND_UUID,  propN, true);
  pCharRain  = addCharacteristic(pSvcData, BLE_CHAR_RAIN_UUID,  propN, true);
  pCharLight = addCharacteristic(pSvcData, BLE_CHAR_LIGHT_UUID, propN, true);

  pSvcData->start();

  // ---- Advertise all three services ----
  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(BLE_SVC_SETTINGS_UUID);
  pAdv->addServiceUUID(BLE_SVC_CONTROLS_UUID);
  pAdv->addServiceUUID(BLE_SVC_DATA_UUID);
  pAdv->setScanResponse(true);
  pAdv->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising as: " + localName);
}

// ============================================================
//  READ SENSORS
// ============================================================
void readSensors() {
  // --- AHT25: Temperature + Humidity ---
  sensors_event_t humidity_ev, temp_ev;
  if (aht.getEvent(&humidity_ev, &temp_ev)) {
    tempDegC     = temp_ev.temperature;
    humidPercent = (int)humidity_ev.relative_humidity;

    // Simple rolling average (last 4 readings)
    static float tempBuf[4] = {0};
    static int   tempIdx    = 0;
    tempBuf[tempIdx] = tempDegC;
    tempIdx = (tempIdx + 1) % 4;
    float sum = 0;
    for (int i = 0; i < 4; i++) sum += tempBuf[i];
    tempDegCavg = sum / 4.0f;
  } else {
    Serial.println("AHT25 read failed");
  }

  // --- Rain (digital) ---
  isRaining = (digitalRead(PIN_RAIN_DIG) == HIGH);

  // --- Wind ---
  windLevel = measureWindLevel();

  // --- Light (ADC) ---
  // Average 8 samples to reduce ADC noise
  long adcSum = 0;
  for (int i = 0; i < 8; i++) adcSum += analogRead(PIN_LIGHT_ADC);
  lightLevel = (int)(adcSum / 8);

  if (debugEN) {
    Serial.printf("T=%.1f°C H=%d%% Wind=%s Rain=%s Light=%d\n",
      tempDegC, humidPercent, windLevelStr[windLevel],
      isRaining ? "YES" : "NO", lightLevel);
  }
}

// ============================================================
//  WIND MEASUREMENT
// ============================================================
// The sensor outputs a PWM signal where wider pulse = lower speed.
// We measure pulse width and map it to a category.
// Calibrate thresholds against physical sensor.
int measureWindPulseWidth() {
  // pulseIn returns µs; timeout after 50ms (assumes at least some signal)
  int pw = (int)pulseIn(PIN_WIND_PWM, HIGH, 50000);
  return pw;  // 0 if timeout (no signal = still air)
}

WindLevel measureWindLevel() {
  const int SAMPLES = 5;
  long total = 0;
  for (int i = 0; i < SAMPLES; i++) {
    total += measureWindPulseWidth();
    delay(5);
  }
  int avgPW = (int)(total / SAMPLES);
  windSpeedRaw = avgPW;

  // ---- Lookup table (µs pulse width → wind category) ----
  // Wider pulse = slower speed per spec.
  // TODO: Replace these thresholds with values from real calibration.
  if (avgPW == 0 || avgPW > 40000) return WIND_CALM;
  if (avgPW > 25000)               return WIND_LOW;
  if (avgPW > 15000)               return WIND_MEDIUM;
  if (avgPW > 8000)                return WIND_HIGH;
  return WIND_STORM;
}

// ============================================================
//  READ BLE (settings written by app are handled in the callback;
//  this function can poll or trigger any deferred BLE-driven actions)
// ============================================================
void readBLE() {
  // Settings are applied immediately via SettingWriteCallback.
  // Add any deferred or polled BLE handling here if needed.
}

// ============================================================
//  WRITE BLE
//  Push current sensor data to the Data Reading service (Notify).
//  Also refresh the Settings and Controls characteristics so the
//  app can read current values at any time.
//  All Int32 values use the agreed scaling factors.
// ============================================================
void writeBLE() {
  if (!bleConnected) return;

  // ---- Data Reading service (sensor values → app) ----
  // Temp: °C × 100  e.g. 22.50°C → 2250
  bleSetInt32(pCharTemp, (int32_t)(tempDegC * 100.0f));
  pCharTemp->notify();

  // Humid: % × 100  e.g. 55.0% → 5500
  bleSetInt32(pCharHumid, (int32_t)(humidPercent * 100));
  pCharHumid->notify();

  // Wind: mph × 10  - windSpeedRaw is pulse-width µs; convert to mph using
  // calibration factor (1.4 mph = 1 RPS placeholder - adjust to your sensor).
  // TODO: replace with real RPM→mph calculation once calibrated.
  int32_t windMph10 = (int32_t)(windSpeedRaw > 0 ? 50 : 0); // placeholder 5.0 mph when spinning
  bleSetInt32(pCharWind, windMph10);
  pCharWind->notify();

  // Rain: 0 = dry, future analogue scale placeholder (currently digital only)
  bleSetInt32(pCharRain, isRaining ? 100 : 0);
  pCharRain->notify();

  // Light: % × 10  - map ADC 0-4095 to 0-1000 (0-100.0%)
  int32_t lightPct10 = (int32_t)(((long)lightLevel * 1000L) / ADC_MAX);
  bleSetInt32(pCharLight, lightPct10);
  pCharLight->notify();

  // ---- Settings service (refresh readable values) ----
  bleSetInt32(pCharClock,   measurementClk);
  bleSetInt32(pCharBlTmr,   backlightSleepTMR);
  bleSetBool (pCharLcdLock, LCDLock);
  bleSetBool (pCharDebug,   debugEN);

  // ---- Controls service (refresh readable values) ----
  bleSetInt32(pCharTempSP,   tempSetPoint);
  bleSetInt32(pCharTempHys,  tempHysteresis);
  bleSetInt32(pCharHumSP,    humidSetPoint);
  bleSetInt32(pCharHumHys,   humidHysteresis);
  bleSetInt32(pCharWindMax,  windMax);
  bleSetInt32(pCharLightMin, lightMin);
  bleSetBool (pCharLightOvr, lightOverride);
  bleSetBool (pCharLightSet, lightSet);
  bleSetBool (pCharWinOvr,   windowOverride);
  bleSetBool (pCharWinSet,   windowSet);
}

// ============================================================
//  AUTOMATIC CONTROL LOGIC
//  All threshold comparisons use the same scaled Int32 units
//  that are stored in the control variables.
//  tempSetPoint / tempHysteresis are °C×10, so compare against
//  tempDegC×10 to keep arithmetic consistent.
// ============================================================
void autoControl() {

  // Work in °C×10 for temperature comparisons
  int32_t tempNow10  = (int32_t)(tempDegCavg * 10.0f);
  int32_t humidNow   = (int32_t)humidPercent;  // % direct

  // ---- Window control ----
  bool targetWindowOpen = windowIsOpen;

  if (windowOverride) {
    targetWindowOpen = windowSet;
  } else {
    // Safety: force closed on high wind or rain regardless of other conditions
    // windMax is mph×10; windSpeedRaw needs conversion - for now use enum level
    // TODO: update wind comparison once mph calculation is calibrated
    bool windTooHigh = ((int)windLevel >= WIND_HIGH);
    if (windTooHigh || isRaining) {
      targetWindowOpen = false;
    } else {
      // Temperature logic (°C×10)
      if (tempNow10 > (tempSetPoint + tempHysteresis)) {
        targetWindowOpen = true;    // too hot → ventilate
      } else if (tempNow10 < (tempSetPoint - tempHysteresis)) {
        targetWindowOpen = false;   // too cold → close
      }

      // Humidity logic (% direct, not raining)
      if (!isRaining) {
        if (humidNow > (humidSetPoint + humidHysteresis)) {
          targetWindowOpen = true;  // too humid → ventilate
        } else if (humidNow < (humidSetPoint - humidHysteresis)) {
          targetWindowOpen = true;  // too dry → open per spec
        }
      }
    }
  }

  if (targetWindowOpen != windowIsOpen) {
    setWindow(targetWindowOpen);
  }

  // ---- Relay: Heater (Relay1) ----
  bool heaterNeeded = (tempNow10 < (tempSetPoint - tempHysteresis));
  if (heaterNeeded != relay1Set) {
    relay1Set = heaterNeeded;
    setRelay(PIN_RELAY1, relay1Set);
    Serial.printf("Heater %s\n", relay1Set ? "ON" : "OFF");
  }

  // ---- Relay: Cooler (Relay2) ----
  bool coolerNeeded = (tempNow10 > (tempSetPoint + tempHysteresis));
  if (coolerNeeded != relay2Set) {
    relay2Set = coolerNeeded;
    setRelay(PIN_RELAY2, relay2Set);
    Serial.printf("Cooler %s\n", relay2Set ? "ON" : "OFF");
  }

  // ---- Lighting LED ----
  // lightMin is % (0-100); map to ADC counts for comparison
  // lightMin% → ADC threshold = (lightMin * ADC_MAX) / 100
  int adcThreshold = (int)(((long)lightMin * ADC_MAX) / 100L);
  bool lightOn;
  if (lightOverride) {
    lightOn = lightSet;
  } else {
    lightOn = (lightLevel < adcThreshold);
  }
  digitalWrite(PIN_LED_LIGHT, lightOn ? HIGH : LOW);
}

// ============================================================
//  OUTPUT HELPERS
// ============================================================
void setWindow(bool open) {
  Serial.printf("WINDOW %s\n", open ? "OPENING" : "CLOSING");
  windowIsOpen = open;
  int target = open ? SERVO_WINDOW_OPEN : SERVO_WINDOW_CLOSED;
  moveServoTo(target);
}

// Slow-step the servo to avoid mechanical shock
void moveServoTo(int targetDeg) {
  int step = (targetDeg > currentServoDeg) ? 1 : -1;
  while (currentServoDeg != targetDeg) {
    currentServoDeg += step;
    windowServo.write(currentServoDeg);
    delay(SERVO_SPEED_DELAY);
  }
}

// Relay is active LOW - invert the logic here
void setRelay(int pin, bool state) {
  digitalWrite(pin, state ? LOW : HIGH);
}

// ============================================================
//  DISPLAY UPDATE
// ============================================================
void displayUpdate() {
  static unsigned long lastRefresh = 0;
  if (millis() - lastRefresh < 200) return;  // cap refresh at ~5 Hz
  lastRefresh = millis();

  if (!backlightOn) return;  // don't refresh if backlight is off

  lcdPrintPage(currentPage);
}

void lcdPrintPage(MenuPage page) {
  static MenuPage lastPage = (MenuPage)-1;
  // Only clear when page changes to avoid flicker
  if (page != lastPage) {
    lcd.clear();
    lastPage = page;
  }

  switch (page) {
    case PAGE_HOME:
      lcd.setCursor(0, 0); lcd.print("  PAWS  Monitor   ");
      lcd.setCursor(0, 1); lcd.print("T:" + formatFloat(tempDegC, 1) + "C  H:" + String(humidPercent) + "%  ");
      lcd.setCursor(0, 2); lcd.print("Wind:" + String(windLevelStr[windLevel]) + "        ");
      lcd.setCursor(0, 3); lcd.print(isRaining ? "Rain:YES" : "Rain:NO ");
      lcd.setCursor(9, 3);  lcd.print(bleConnected ? " BLE:OK " : " BLE:-- ");
      break;

    case PAGE_TEMP:
      lcd.setCursor(0, 0); lcd.print("-- Temperature --   ");
      lcd.setCursor(0, 1); lcd.print("Current: " + formatFloat(tempDegC, 1) + " C    ");
      lcd.setCursor(0, 2); lcd.print("Setpt:   " + formatFloat(tempSetPoint / 10.0f, 1) + " C    ");
      lcd.setCursor(0, 3); lcd.print("Hys: +/-" + formatFloat(tempHysteresis / 10.0f, 1) + " C   ");
      break;

    case PAGE_HUMID:
      lcd.setCursor(0, 0); lcd.print("-- Humidity -----   ");
      lcd.setCursor(0, 1); lcd.print("Current: " + String(humidPercent) + " %      ");
      lcd.setCursor(0, 2); lcd.print("Setpt:   " + String(humidSetPoint) + " %       ");
      lcd.setCursor(0, 3); lcd.print("Hys: +/- " + String(humidHysteresis) + " %      ");
      break;

    case PAGE_WIND:
      lcd.setCursor(0, 0); lcd.print("---- Wind -------   ");
      lcd.setCursor(0, 1); lcd.print("Level: " + String(windLevelStr[windLevel]) + "       ");
      lcd.setCursor(0, 2); lcd.print("Max:   " + String(windLevelStr[windMax]) + "       ");
      lcd.setCursor(0, 3); lcd.print("Raw PW:" + String(windSpeedRaw) + "us     ");
      break;

    case PAGE_RAIN:
      lcd.setCursor(0, 0); lcd.print("---- Rain -------   ");
      lcd.setCursor(0, 1); lcd.print(isRaining ? "Status: RAINING   " : "Status: DRY       ");
      lcd.setCursor(0, 2); lcd.print("                    ");
      lcd.setCursor(0, 3); lcd.print("                    ");
      break;

    case PAGE_LIGHT:
      lcd.setCursor(0, 0); lcd.print("-- Ambient Light -  ");
      lcd.setCursor(0, 1); lcd.print("Level:   " + String(lightLevel) + "        ");
      lcd.setCursor(0, 2); lcd.print("Min thr: " + String(lightMin) + "        ");
      lcd.setCursor(0, 3); lcd.print("Lights: ");
      lcd.print(lightOverride ? (lightSet ? "ON(OVR) " : "OFF(OVR)") : (lightLevel < lightMin ? "ON(AUTO)" : "OFF    "));
      break;

    case PAGE_WINDOW:
      lcd.setCursor(0, 0); lcd.print("---- Window -----   ");
      lcd.setCursor(0, 1); lcd.print("State: ");
      lcd.print(windowIsOpen ? "OPEN         " : "CLOSED       ");
      lcd.setCursor(0, 2); lcd.print("Override: ");
      lcd.print(windowOverride ? (windowSet ? "OPEN  " : "CLOSE ") : "OFF   ");
      lcd.setCursor(0, 3); lcd.print("                    ");
      break;

    case PAGE_SETTINGS:
      lcd.setCursor(0, 0); lcd.print("--- Settings ----   ");
      lcd.setCursor(0, 1); lcd.print("Clk: " + String(measurementClk) + "s BL:" + formatFloat(backlightSleepTMR / 10.0f, 1) + "m");
      lcd.setCursor(0, 2); lcd.print("Lock:");
      lcd.print(LCDLock ? "ON  " : "OFF ");
      lcd.print("Dbg:");
      lcd.print(debugEN ? "ON  " : "OFF ");
      lcd.setCursor(0, 3); lcd.print("                    ");
      break;

    default:
      break;
  }
}

// ============================================================
//  ENCODER ISR
// ============================================================
void IRAM_ATTR encoderISR() {
  int clk = digitalRead(PIN_ENC_CLK);
  int dt  = digitalRead(PIN_ENC_DT);
  if (clk != lastEncCLK && clk == LOW) {
    encoderPos += (dt != clk) ? 1 : -1;
  }
  lastEncCLK = clk;
}

void IRAM_ATTR encoderButtonISR() {
  encoderPressed = true;
}

// ============================================================
//  HANDLE ENCODER (called from main loop)
// ============================================================
void handleEncoder() {
  if (LCDLock) return;

  // Wake backlight on any encoder activity
  if (encoderPos != lastEncoderPos || encoderPressed) {
    if (!backlightOn) {
      lcd.backlight();
      backlightOn = true;
    }
    backlightOnMs = millis();
  }

  // Rotate through menu pages
  if (encoderPos != lastEncoderPos) {
    int delta = encoderPos - lastEncoderPos;
    lastEncoderPos = encoderPos;

    int p = (int)currentPage + delta;
    // Wrap around
    if (p < 0)            p = PAGE_COUNT - 1;
    if (p >= PAGE_COUNT)  p = 0;
    currentPage = (MenuPage)p;

    if (debugEN) Serial.printf("[ENC] Page → %d\n", p);
  }

  // Button press - placeholder for edit mode entry
  if (encoderPressed) {
    encoderPressed = false;
    inEditMode = !inEditMode;
    if (debugEN) Serial.printf("[ENC] Button - editMode=%d\n", inEditMode);
    // TODO: expand to allow in-place editing of the focused setting
  }
}

// ============================================================
//  UTILITY
// ============================================================
String formatFloat(float val, int dp) {
  char buf[12];
  if (dp == 1) snprintf(buf, sizeof(buf), "%.1f", val);
  else if (dp == 2) snprintf(buf, sizeof(buf), "%.2f", val);
  else snprintf(buf, sizeof(buf), "%.0f", val);
  return String(buf);
}
