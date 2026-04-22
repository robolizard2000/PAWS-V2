// ============================================================
//  PAWS - Automated Environmental Control System
//  ESP32 Embedded Software  |  Version 0.5
// ============================================================
//
//  Hardware: ESP32 Dev Board
//
//  Inputs:
//    - AHT25 Temperature / Humidity Sensor (I2C)
//    - Wind sensor (PWM pulse-width output)
//    - Tipping bucket rain gauge (digital switch pulses)
//    - Ambient light sensor (analogue ADC)
//    - Rotary encoder + push-button
//
//  Outputs:
//    - Servo  : automatic window open / close
//    - Relay1 : heater control
//    - Relay2 : cooler control
//    - LED1   : BLE connection status
//    - LED2   : lighting indicator
//    - LCD    : 20x4 I2C display
//
//  Communications:
//    - Bluetooth Low Energy (BLE) with 3 services:
//        * Settings
//        * Controls
//        * Data Reading
//
//  Main features:
//    - Periodic sensor reading
//    - Automatic environmental control
//    - LCD / encoder menu system
//    - BLE read/write control from mobile app
//    - Debug output that can be enabled/disabled
// ============================================================

// ============================================================
//  LIBRARIES
// ============================================================
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ============================================================
//  PIN DEFINITIONS
// ============================================================

// I2C bus for AHT25 and LCD
#define PIN_SDA            21
#define PIN_SCL            22

// Sensor inputs
#define PIN_WIND_PWM        4    // Wind sensor PWM input
#define PIN_RAIN_DIG        5    // Tipping bucket switch input
#define PIN_LIGHT_ADC      34    // Light sensor ADC input

// Outputs
#define PIN_SERVO          13    // Window servo motor
#define PIN_RELAY1         25    // Heater relay (active LOW)
#define PIN_RELAY2         26    // Cooler relay (active LOW)
#define PIN_LED_BLE        27    // BLE status LED
#define PIN_LED_LIGHT      14    // Lighting indicator LED

// Rotary encoder inputs
#define PIN_ENC_CLK        32
#define PIN_ENC_DT         33
#define PIN_ENC_SW         35    // Encoder push-button

// ============================================================
//  BLE UUID DEFINITIONS
// ============================================================
//
//  Three BLE services are used:
//    1) Settings Service
//    2) Controls Service
//    3) Data Reading Service
//
//  These match the UUID list you supplied.
// ============================================================

// ---------------- Settings Service ----------------
#define BLE_SERVICE_SETTINGS_UUID        "12345000-0000-1000-8000-00805F9B34FB"
#define BLE_CHAR_CLOCK_UUID              "12345001-0000-1000-8000-00805F9B34FB"  // Int32 seconds
#define BLE_CHAR_BACKLIGHT_UUID          "12345002-0000-1000-8000-00805F9B34FB"  // Int32 minutes *10
#define BLE_CHAR_LCD_LOCK_UUID           "12345003-0000-1000-8000-00805F9B34FB"  // Bool
#define BLE_CHAR_DEBUG_UUID              "12345004-0000-1000-8000-00805F9B34FB"  // Bool
#define BLE_CHAR_SERVICE_NAME_UUID       "12345010-0000-1000-8000-00805F9B34FB"  // Char(20) read
#define BLE_CHAR_LOCAL_NAME_UUID         "12345011-0000-1000-8000-00805F9B34FB"  // Char(20) read

// ---------------- Controls Service ----------------
#define BLE_SERVICE_CONTROLS_UUID        "12346000-0000-1000-8000-00805F9B34FB"
#define BLE_CHAR_TEMP_SP_UUID            "12346001-0000-1000-8000-00805F9B34FB"  // Int32 C *10
#define BLE_CHAR_TEMP_HYS_UUID           "12346002-0000-1000-8000-00805F9B34FB"  // Int32 C *10
#define BLE_CHAR_HUM_SP_UUID             "12346003-0000-1000-8000-00805F9B34FB"  // Int32 %
#define BLE_CHAR_HUM_HYS_UUID            "12346004-0000-1000-8000-00805F9B34FB"  // Int32 %
#define BLE_CHAR_WIND_MAX_UUID           "12346005-0000-1000-8000-00805F9B34FB"  // Int32 mph *10
#define BLE_CHAR_LIGHT_MIN_UUID          "12346006-0000-1000-8000-00805F9B34FB"  // Int32 %
#define BLE_CHAR_LIGHT_OVR_UUID          "12346007-0000-1000-8000-00805F9B34FB"  // Bool
#define BLE_CHAR_LIGHT_SET_UUID          "12346008-0000-1000-8000-00805F9B34FB"  // Bool
#define BLE_CHAR_WIN_OVR_UUID            "12346009-0000-1000-8000-00805F9B34FB"  // Bool
#define BLE_CHAR_WIN_SET_UUID            "1234600A-0000-1000-8000-00805F9B34FB"  // Bool

// ---------------- Data Reading Service ----------------
#define BLE_SERVICE_DATA_UUID            "12347000-0000-1000-8000-00805F9B34FB"
#define BLE_CHAR_TEMP_UUID               "12347001-0000-1000-8000-00805F9B34FB"  // Int32 C *100
#define BLE_CHAR_HUMID_UUID              "12347002-0000-1000-8000-00805F9B34FB"  // Int32 % *100
#define BLE_CHAR_WIND_UUID               "12347003-0000-1000-8000-00805F9B34FB"  // Int32 mph *10
#define BLE_CHAR_RAIN_UUID               "12347004-0000-1000-8000-00805F9B34FB"  // Int32 tips/min *10
#define BLE_CHAR_LIGHT_UUID              "12347005-0000-1000-8000-00805F9B34FB"  // Int32 % *10

// ============================================================
//  CONSTANTS AND ENUMS
// ============================================================

// Wind categories used internally by the control algorithm
enum WindLevel { WIND_CALM = 0, WIND_LOW , WIND_MEDIUM , WIND_HIGH  , WIND_STORM };

// Human-readable names for each category
const char* windLevelStr[] = { "Calm", "Low", "Medium", "High", "Storm" };

// Approximate mph*10 values used for BLE reporting / simple comparison
const int windLevelMphX10[] = { 0, 25, 60, 110, 175 };

// Servo positions
#define SERVO_WINDOW_OPEN     90
#define SERVO_WINDOW_CLOSED    0
#define SERVO_SPEED_DELAY     20

// LCD dimensions
#define LCD_COLS              20
#define LCD_ROWS               4
#define LCD_I2C_ADDR        0x27

// ADC maximum for ESP32 12-bit ADC
#define ADC_MAX             4095

// Tipping bucket rain gauge parameters
#define RAIN_DEBOUNCE_US    50000UL    // debounce time in microseconds
#define RAIN_HOLD_MS       120000UL    // remain "raining" for 2 mins after last tip

// Optional conversion factor for mm/hour if needed later
#define RAIN_MM_PER_TIP      0.2794f   // example value only; change if required

// ============================================================
//  SETTINGS STRUCT
// ============================================================
//
//  This struct stores one configurable item used by the menu
//  system. It supports numeric settings and boolean settings.
// ============================================================
struct Set {
  const char* Name;   // text shown on LCD
  float       Value;  // current value
  float       Defult; // default value
  float       Step;   // step size when encoder turns
  float       Min;    // minimum allowed value
  float       Max;    // maximum allowed value

  Set(const char* name, float value, float defult, float step, float min, float max)
    : Name(name), Value(value), Defult(defult), Step(step), Min(min), Max(max) {}
};

// ============================================================
//  SETTINGS ARRAY
// ============================================================
//
//  These values drive automatic control and BLE settings.
// ============================================================
Set Settings[] = {
  Set("Temp Setpoint", 22.0, 22.0, 0.5,  0.0,  50.0),  // 0  °C
  Set("Temp Hyster.",   1.0,  1.0, 0.1,  0.0,  10.0),  // 1  °C
  Set("Humid Setpoint",55.0, 55.0, 1.0, 10.0,  90.0),  // 2  %
  Set("Humid Hyster.",  5.0,  5.0, 0.5,  0.0,  20.0),  // 3  %
  Set("Wind Max",      22.0, 22.0, 1.0,  0.0,  80.0),  // 4  mph
  Set("Light Min %",   20.0, 20.0, 1.0,  0.0, 100.0),  // 5  %
  Set("Meas. Clock",   10.0, 10.0, 1.0,  1.0, 300.0),  // 6  seconds
  Set("Backlight min",  2.0,  2.0, 0.5,  0.5,  30.0),  // 7  minutes
  Set("Debug",          0.0,  0.0, 1.0,  0.0,   1.0),  // 8  bool
  Set("LCD Lock",       0.0,  0.0, 1.0,  0.0,   1.0),  // 9  bool
  Set("Exit",           0.0,  0.0, 0.0,  0.0,   0.0),  // 10 menu exit
};
const uint8_t SETTINGS_COUNT = sizeof(Settings) / sizeof(Settings[0]);

// ============================================================
//  CONTROLS ARRAY
// ============================================================
//
//  Manual override controls shown in the Controls menu.
// ============================================================
Set Controls[] = {
  Set("Win Override",   0.0, 0.0, 1.0, 0.0, 1.0),  // 0
  Set("Window Set",     0.0, 0.0, 1.0, 0.0, 1.0),  // 1
  Set("Light Override", 0.0, 0.0, 1.0, 0.0, 1.0),  // 2
  Set("Light Set",      0.0, 0.0, 1.0, 0.0, 1.0),  // 3
  Set("Exit",           0.0, 0.0, 0.0, 0.0, 0.0),  // 4
};
const uint8_t CONTROLS_COUNT = sizeof(Controls) / sizeof(Controls[0]);

// ============================================================
//  CONVENIENCE MACROS
// ============================================================
//
//  These make the control code easier to read.
// ============================================================
#define S_TEMP_SP      Settings[0].Value
#define S_TEMP_HYS     Settings[1].Value
#define S_HUMID_SP     Settings[2].Value
#define S_HUMID_HYS    Settings[3].Value
#define S_WIND_MAX     Settings[4].Value
#define S_LIGHT_MIN    Settings[5].Value
#define S_MEAS_CLK     Settings[6].Value
#define S_BL_TMR       Settings[7].Value
#define S_DEBUG        Settings[8].Value
#define S_LCD_LOCK     Settings[9].Value

#define C_WIN_OVR      Controls[0].Value
#define C_WIN_SET      Controls[1].Value
#define C_LIGHT_OVR    Controls[2].Value
#define C_LIGHT_SET    Controls[3].Value

// ============================================================
//  DEBUG MACROS
// ============================================================
//
//  These macros allow debug output to be enabled / disabled
//  using the Debug setting.
// ============================================================
#define DEBUG_PRINT(...)   do { if (S_DEBUG >= 1.0f) Serial.printf(__VA_ARGS__); } while(0)
#define DEBUG_PRINTLN(x)   do { if (S_DEBUG >= 1.0f) Serial.println(x); } while(0)

// ============================================================
//  SENSOR / DATA VARIABLES
// ============================================================

// ---------------- Temperature / Humidity ----------------
float     tempDegC        = 0.0;
float     tempDegCavg     = 0.0;
int       humidPercent    = 0;

// ---------------- Wind ----------------
int       windSpeedRaw    = 0;
WindLevel windLevel       = WIND_CALM;

// ---------------- Light ----------------
int       lightLevel      = 0;

// ---------------- Rain (tipping bucket) ----------------
volatile uint32_t rainTipCountISR = 0;      // incremented in ISR
volatile unsigned long lastRainTipUs = 0;   // debounce timer for ISR

uint32_t rainTipCountTotal = 0;             // total tips since startup
uint32_t rainTipsInterval  = 0;             // tips in latest interval
float    rainTipsPerMin    = 0.0f;          // tips/minute
float    rainMmPerHour     = 0.0f;          // optional converted rainfall rate
bool     isRaining         = false;         // logical rain status
unsigned long lastRainActivityMs = 0;       // time of last rain activity

// ============================================================
//  SENSOR CONNECTION FLAGS
//  These allow testing without all sensors connected
// ============================================================

// AHT25 is auto-detected in initSensors()
bool ahtConnected   = false;

// For the others, set these manually.
// Change to false if that sensor is not connected.
bool windConnected  = false;
bool rainConnected  = false;
bool lightConnected = false;

// ============================================================
//  DEFAULT / FALLBACK VALUES
//  Used if a sensor is missing or disabled
// ============================================================


#define DEFAULT_TEMP_C         20.0f
#define DEFAULT_HUMID_PCT      50
#define DEFAULT_WIND_LEVEL     WIND_LOW
#define DEFAULT_LIGHT_ADC      2000
#define DEFAULT_RAIN_TIPS_MIN  0.0f

// ============================================================
//  TIMING VARIABLES
// ============================================================
unsigned long lastMeasurementMs = 0;
unsigned long backlightOnMs     = 0;
bool          backlightOn       = true;

// ============================================================
//  RUNTIME OUTPUT STATE
// ============================================================
bool relay1Set       = false;   // heater state
bool relay2Set       = false;   // cooler state
bool windowIsOpen    = false;   // window state
int  currentServoDeg = SERVO_WINDOW_CLOSED;

// ============================================================
//  HARDWARE OBJECTS
// ============================================================
Adafruit_AHTX0    aht;
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
Servo             windowServo;

// ============================================================
//  BLE OBJECT POINTERS
// ============================================================
BLEServer* pServer = nullptr;

// Data characteristics
BLECharacteristic* pCharTemp  = nullptr;
BLECharacteristic* pCharHumid = nullptr;
BLECharacteristic* pCharWind  = nullptr;
BLECharacteristic* pCharRain  = nullptr;
BLECharacteristic* pCharLight = nullptr;

// Settings characteristics
BLECharacteristic* pCharClock       = nullptr;
BLECharacteristic* pCharBacklight   = nullptr;
BLECharacteristic* pCharLcdLock     = nullptr;
BLECharacteristic* pCharDebug       = nullptr;
BLECharacteristic* pCharServiceName = nullptr;
BLECharacteristic* pCharLocalName   = nullptr;

// Controls characteristics
BLECharacteristic* pCharTempSp   = nullptr;
BLECharacteristic* pCharTempHys  = nullptr;
BLECharacteristic* pCharHumSp    = nullptr;
BLECharacteristic* pCharHumHys   = nullptr;
BLECharacteristic* pCharWindMax  = nullptr;
BLECharacteristic* pCharLightMin = nullptr;
BLECharacteristic* pCharLightOvr = nullptr;
BLECharacteristic* pCharLightSet = nullptr;
BLECharacteristic* pCharWinOvr   = nullptr;
BLECharacteristic* pCharWinSet   = nullptr;

// BLE connection state
bool bleConnected = false;

// BLE text identifiers
String localName   = "PAWS";
String serviceName = "PAWS";

// ============================================================
//  ENCODER STATE
// ============================================================
int lastStateCLK = HIGH;

// ============================================================
//  FUNCTION PROTOTYPES
// ============================================================
void initBLE();
void initLCD();
void initSensors();
void initOutputs();
void initEncoder();

void readSensors();
void readBLE();
void writeBLE();
void autoControl();
void syncSettingsToBLE();

void moveServoTo(int targetDeg);
void setRelay(int pin, bool state);
void setWindow(bool open);

WindLevel measureWindLevel();
int       measureWindPulseWidth();
int       windLevelToMphX10(WindLevel wl);

void IRAM_ATTR rainTipISR();

void Menu_Select();
void Monitor_Screen();
void Settings_Menu();
void Controls_Menu();
void Setting_Change(Set* arr, uint8_t idx);

String formatFloat(float val, int dp);
void touchBacklight();
void checkBacklightTimeout();

// ============================================================
//  BLE HELPER FUNCTIONS
// ============================================================
//
//  These functions convert between native values and BLE values.
//  Int32 and Bool values are transmitted in binary form.
// ============================================================

// Write a 32-bit integer into a BLE characteristic
void setCharacteristicInt32(BLECharacteristic* pChar, int32_t value) {
  if (!pChar) return;
  pChar->setValue((uint8_t*)&value, sizeof(value));
}

// Write a boolean into a BLE characteristic
void setCharacteristicBool(BLECharacteristic* pChar, bool value) {
  if (!pChar) return;
  uint8_t v = value ? 1 : 0;
  pChar->setValue(&v, 1);
}

// Read a 32-bit integer from a BLE characteristic
int32_t getCharacteristicInt32(BLECharacteristic* pChar) {
  String val = pChar->getValue();

  // Must contain at least 4 bytes
  if (val.length() < (int)sizeof(int32_t)) return 0;

  int32_t out = 0;
  memcpy(&out, val.c_str(), sizeof(int32_t));
  return out;
}

// Read a boolean from a BLE characteristic
bool getCharacteristicBool(BLECharacteristic* pChar) {
  String val = pChar->getValue();

  if (val.length() < 1) return false;
  return ((uint8_t)val[0]) != 0;
}

// ============================================================
//  BLE SERVER CALLBACKS
// ============================================================
//
//  These run when a client connects or disconnects.
// ============================================================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pSvr) override {
    bleConnected = true;
    digitalWrite(PIN_LED_BLE, HIGH);
    DEBUG_PRINTLN("[BLE] Client connected");
    Serial.println("[BLE] Client connected");
    // Send current settings to app when connection starts
    syncSettingsToBLE();
  }

  void onDisconnect(BLEServer* pSvr) override {
    bleConnected = false;
    digitalWrite(PIN_LED_BLE, LOW);
    DEBUG_PRINTLN("[BLE] Client disconnected - restarting advertising");
    Serial.println("[BLE] Client disconnected - restarting advertising");
    BLEDevice::startAdvertising();
    
  }
};

// ============================================================
//  BLE WRITE CALLBACK
// ============================================================
//
//  This callback handles incoming writes from the mobile app.
//  It updates internal Settings[] and Controls[] values.
// ============================================================
class SettingWriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String uuid = pChar->getUUID().toString().c_str();

    // ---------------- Settings Service ----------------
    if (uuid == BLE_CHAR_CLOCK_UUID) {
      int32_t v = getCharacteristicInt32(pChar);
      Settings[6].Value = constrain((float)v, Settings[6].Min, Settings[6].Max);
    }
    else if (uuid == BLE_CHAR_BACKLIGHT_UUID) {
      int32_t v = getCharacteristicInt32(pChar);   // minutes *10
      float mins = ((float)v) / 10.0f;
      Settings[7].Value = constrain(mins, Settings[7].Min, Settings[7].Max);
    }
    else if (uuid == BLE_CHAR_LCD_LOCK_UUID) {
      Settings[9].Value = getCharacteristicBool(pChar) ? 1.0f : 0.0f;
    }
    else if (uuid == BLE_CHAR_DEBUG_UUID) {
      Settings[8].Value = getCharacteristicBool(pChar) ? 1.0f : 0.0f;
    }

    // ---------------- Controls Service ----------------
    else if (uuid == BLE_CHAR_TEMP_SP_UUID) {
      int32_t v = getCharacteristicInt32(pChar);   // C *10
      Settings[0].Value = constrain(((float)v) / 10.0f, Settings[0].Min, Settings[0].Max);
    }
    else if (uuid == BLE_CHAR_TEMP_HYS_UUID) {
      int32_t v = getCharacteristicInt32(pChar);   // C *10
      Settings[1].Value = constrain(((float)v) / 10.0f, Settings[1].Min, Settings[1].Max);
    }
    else if (uuid == BLE_CHAR_HUM_SP_UUID) {
      int32_t v = getCharacteristicInt32(pChar);
      Settings[2].Value = constrain((float)v, Settings[2].Min, Settings[2].Max);
    }
    else if (uuid == BLE_CHAR_HUM_HYS_UUID) {
      int32_t v = getCharacteristicInt32(pChar);
      Settings[3].Value = constrain((float)v, Settings[3].Min, Settings[3].Max);
    }
    else if (uuid == BLE_CHAR_WIND_MAX_UUID) {
      int32_t v = getCharacteristicInt32(pChar);   // mph *10
      Settings[4].Value = constrain(((float)v) / 10.0f, Settings[4].Min, Settings[4].Max);
    }
    else if (uuid == BLE_CHAR_LIGHT_MIN_UUID) {
      int32_t v = getCharacteristicInt32(pChar);
      Settings[5].Value = constrain((float)v, Settings[5].Min, Settings[5].Max);
    }
    else if (uuid == BLE_CHAR_LIGHT_OVR_UUID) {
      Controls[2].Value = getCharacteristicBool(pChar) ? 1.0f : 0.0f;
    }
    else if (uuid == BLE_CHAR_LIGHT_SET_UUID) {
      Controls[3].Value = getCharacteristicBool(pChar) ? 1.0f : 0.0f;
    }
    else if (uuid == BLE_CHAR_WIN_OVR_UUID) {
      Controls[0].Value = getCharacteristicBool(pChar) ? 1.0f : 0.0f;
    }
    else if (uuid == BLE_CHAR_WIN_SET_UUID) {
      Controls[1].Value = getCharacteristicBool(pChar) ? 1.0f : 0.0f;
    }

    DEBUG_PRINT("[BLE] Write UUID: %s\n", uuid.c_str());

    // Push new values back out so app and LCD stay in sync
    syncSettingsToBLE();
  }
};

// ============================================================
//  BLE CHARACTERISTIC CREATION HELPER
// ============================================================
//
//  Creates a BLE characteristic with optional notify and callback.
// ============================================================
BLECharacteristic* addCharacteristic(BLEService* svc,
                                     const char* uuid,
                                     uint32_t properties,
                                     bool addNotify = false,
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
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("[PAWS] v0.5 starting...");

  // Start I2C bus
  Wire.begin(PIN_SDA, PIN_SCL);

  // Initialise all subsystems
  initOutputs();
  initSensors();
  initLCD();
  initEncoder();
  initBLE();

  // Initial sensor read so the display has valid values
  readSensors();
  autoControl();

  // Start backlight timeout timer
  backlightOnMs = millis();

  Serial.println("[PAWS] Init complete");
}

// ============================================================
//  MAIN LOOP
// ============================================================
//
//  If LCD lock is active, the menu is disabled but the control
//  system still continues to run in the background.
// ============================================================
void loop() {
  // When LCD lock is active, prevent user menu access
  if (S_LCD_LOCK >= 1.0f) {
    unsigned long now = millis();

    // Keep system active while locked
    if (now - lastMeasurementMs >= (unsigned long)(S_MEAS_CLK * 1000.0f)) {
      lastMeasurementMs = now;
      readSensors();
      readBLE();
      writeBLE();
      autoControl();
    }

    // Display lock screen
    lcd.setCursor(0, 0); lcd.print("_____PAWS_LOCKED____");
    lcd.setCursor(0, 1); lcd.print("LCD/Encoder Locked  ");
    lcd.setCursor(0, 2); lcd.print("Use BLE to unlock   ");
    lcd.setCursor(0, 3); lcd.print(bleConnected ? "BLE Connected       " : "BLE Waiting...      ");

    delay(200);
    return;
  }

  // Normal operation: run menu system
  Menu_Select();
}

// ============================================================
//  INITIALISATION FUNCTIONS
// ============================================================

// Configure outputs and set safe startup states
void initOutputs() {
  pinMode(PIN_RELAY1, OUTPUT);    digitalWrite(PIN_RELAY1, HIGH);  // relay OFF
  pinMode(PIN_RELAY2, OUTPUT);    digitalWrite(PIN_RELAY2, HIGH);  // relay OFF
  pinMode(PIN_LED_BLE, OUTPUT);   digitalWrite(PIN_LED_BLE, LOW);
  pinMode(PIN_LED_LIGHT, OUTPUT); digitalWrite(PIN_LED_LIGHT, LOW);

  // Start servo in closed position
  windowServo.attach(PIN_SERVO);
  windowServo.write(SERVO_WINDOW_CLOSED);

  Serial.println("[INIT] Outputs ready");
}

// Configure sensors
void initSensors() {
  // ----------------------------------------------------------
  // AHT25 temperature / humidity sensor
  // Try to initialise it. If missing, use fallback values.
  // ----------------------------------------------------------
  if (!aht.begin()) {
    Serial.println("WARNING: AHT25 not found - using default values");
    ahtConnected = false;
  } else {
    Serial.println("AHT25 ready");
    ahtConnected = true;
  }

  // ----------------------------------------------------------
  // Rain tipping bucket input
  // Only attach interrupt if the rain sensor is enabled
  // ----------------------------------------------------------
  if (rainConnected) {
    pinMode(PIN_RAIN_DIG, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_RAIN_DIG), rainTipISR, FALLING);
    Serial.println("Rain sensor ready");
  } else {
    Serial.println("Rain sensor disabled - using default values");
  }

  // ----------------------------------------------------------
  // Wind sensor input
  // ----------------------------------------------------------
  if (windConnected) {
    pinMode(PIN_WIND_PWM, INPUT);
    Serial.println("Wind sensor ready");
  } else {
    Serial.println("Wind sensor disabled - using default values");
  }

  // ----------------------------------------------------------
  // Light sensor
  // ADC pin does not need pinMode, but log status anyway
  // ----------------------------------------------------------
  if (lightConnected) {
    Serial.println("Light sensor ready");
  } else {
    Serial.println("Light sensor disabled - using default values");
  }

  Serial.println("Sensors initialisation complete");
}

// Initialise LCD
void initLCD() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("   PAWS  v0.5   ");
  lcd.setCursor(0, 1); lcd.print("  Initialising..  ");
  delay(1500);
  lcd.clear();
  Serial.println("LCD ready");
}

// Initialise rotary encoder inputs
void initEncoder() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);
  lastStateCLK = digitalRead(PIN_ENC_CLK);
  Serial.println("[INIT] Encoder ready");
}

// ============================================================
//  BLE INITIALISATION
// ============================================================
//
//  Sets up all 3 BLE services and characteristics.
// ============================================================
void initBLE() {
  BLEDevice::init(localName.c_str());

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  SettingWriteCallback* writeCB = new SettingWriteCallback();

  // ---------------- Settings Service ----------------
  BLEService* pSettingsService = pServer->createService(BLEUUID(BLE_SERVICE_SETTINGS_UUID), 32);

  uint32_t propReadWrite = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE;
  uint32_t propReadOnly  = BLECharacteristic::PROPERTY_READ;

  pCharClock       = addCharacteristic(pSettingsService, BLE_CHAR_CLOCK_UUID,        propReadWrite, false, writeCB);
  pCharBacklight   = addCharacteristic(pSettingsService, BLE_CHAR_BACKLIGHT_UUID,    propReadWrite, false, writeCB);
  pCharLcdLock     = addCharacteristic(pSettingsService, BLE_CHAR_LCD_LOCK_UUID,     propReadWrite, false, writeCB);
  pCharDebug       = addCharacteristic(pSettingsService, BLE_CHAR_DEBUG_UUID,        propReadWrite, false, writeCB);
  pCharServiceName = addCharacteristic(pSettingsService, BLE_CHAR_SERVICE_NAME_UUID, propReadOnly);
  pCharLocalName   = addCharacteristic(pSettingsService, BLE_CHAR_LOCAL_NAME_UUID,   propReadOnly);

  // ---------------- Controls Service ----------------
  BLEService* pControlsService = pServer->createService(BLEUUID(BLE_SERVICE_CONTROLS_UUID), 48);

  pCharTempSp   = addCharacteristic(pControlsService, BLE_CHAR_TEMP_SP_UUID,   propReadWrite, false, writeCB);
  pCharTempHys  = addCharacteristic(pControlsService, BLE_CHAR_TEMP_HYS_UUID,  propReadWrite, false, writeCB);
  pCharHumSp    = addCharacteristic(pControlsService, BLE_CHAR_HUM_SP_UUID,    propReadWrite, false, writeCB);
  pCharHumHys   = addCharacteristic(pControlsService, BLE_CHAR_HUM_HYS_UUID,   propReadWrite, false, writeCB);
  pCharWindMax  = addCharacteristic(pControlsService, BLE_CHAR_WIND_MAX_UUID,  propReadWrite, false, writeCB);
  pCharLightMin = addCharacteristic(pControlsService, BLE_CHAR_LIGHT_MIN_UUID, propReadWrite, false, writeCB);
  pCharLightOvr = addCharacteristic(pControlsService, BLE_CHAR_LIGHT_OVR_UUID, propReadWrite, false, writeCB);
  pCharLightSet = addCharacteristic(pControlsService, BLE_CHAR_LIGHT_SET_UUID, propReadWrite, false, writeCB);
  pCharWinOvr   = addCharacteristic(pControlsService, BLE_CHAR_WIN_OVR_UUID,   propReadWrite, false, writeCB);
  pCharWinSet   = addCharacteristic(pControlsService, BLE_CHAR_WIN_SET_UUID,   propReadWrite, false, writeCB);

  // ---------------- Data Reading Service ----------------
  BLEService* pDataService = pServer->createService(BLEUUID(BLE_SERVICE_DATA_UUID), 32);

  uint32_t propNotifyOnly = BLECharacteristic::PROPERTY_NOTIFY;

  pCharTemp  = addCharacteristic(pDataService, BLE_CHAR_TEMP_UUID,  propNotifyOnly, true);
  pCharHumid = addCharacteristic(pDataService, BLE_CHAR_HUMID_UUID, propNotifyOnly, true);
  pCharWind  = addCharacteristic(pDataService, BLE_CHAR_WIND_UUID,  propNotifyOnly, true);
  pCharRain  = addCharacteristic(pDataService, BLE_CHAR_RAIN_UUID,  propNotifyOnly, true);
  pCharLight = addCharacteristic(pDataService, BLE_CHAR_LIGHT_UUID, propNotifyOnly, true);

  // Static read-only values
  pCharServiceName->setValue(serviceName.c_str());
  pCharLocalName->setValue(localName.c_str());

  // Start all services
  pSettingsService->start();
  pControlsService->start();
  pDataService->start();

  // Push initial settings values into characteristics
  syncSettingsToBLE();

  // Start advertising all services
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_SETTINGS_UUID);
  pAdvertising->addServiceUUID(BLE_SERVICE_CONTROLS_UUID);
  pAdvertising->addServiceUUID(BLE_SERVICE_DATA_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);

  BLEDevice::startAdvertising();

  Serial.println("BLE advertising as: " + localName);
}

// ============================================================
//  RAIN INTERRUPT SERVICE ROUTINE
// ============================================================
//
//  Each time the tipping bucket tips, the switch pulses.
//  This ISR counts those pulses.
// ============================================================
void IRAM_ATTR rainTipISR() {
  unsigned long nowUs = micros();

  // Debounce to reject contact bounce
  if ((nowUs - lastRainTipUs) > RAIN_DEBOUNCE_US) {
    rainTipCountISR++;
    lastRainTipUs = nowUs;
  }
}

// ============================================================
//  SENSOR READING
// ============================================================
//
//  Reads all sensor inputs and updates global variables.
// ============================================================
void readSensors() {
  // ==========================================================
  //  TEMPERATURE / HUMIDITY
  // ==========================================================
  if (ahtConnected) {
    sensors_event_t humidity_ev, temp_ev;

    if (aht.getEvent(&humidity_ev, &temp_ev)) {
      tempDegC     = temp_ev.temperature;
      humidPercent = (int)humidity_ev.relative_humidity;

      // Rolling average over the last 4 readings
      static float tempBuf[4] = {
        DEFAULT_TEMP_C,
        DEFAULT_TEMP_C,
        DEFAULT_TEMP_C,
        DEFAULT_TEMP_C
      };
      static int tempIdx = 0;

      tempBuf[tempIdx] = tempDegC;
      tempIdx = (tempIdx + 1) % 4;

      float sum = 0.0f;
      for (int i = 0; i < 4; i++) {
        sum += tempBuf[i];
      }
      tempDegCavg = sum / 4.0f;
    } else {
      // Sensor present but read failed this cycle
      tempDegC      = DEFAULT_TEMP_C;
      tempDegCavg   = DEFAULT_TEMP_C;
      humidPercent  = DEFAULT_HUMID_PCT;

      DEBUG_PRINTLN("[SENSOR] AHT25 read failed - using default values");
    }
  } else {
    // Sensor not connected
    tempDegC      = DEFAULT_TEMP_C;
    tempDegCavg   = DEFAULT_TEMP_C;
    humidPercent  = DEFAULT_HUMID_PCT;
  }

  // ==========================================================
  //  WIND
  // ==========================================================
  if (windConnected) {
    windLevel = measureWindLevel();
  } else {
    windLevel = DEFAULT_WIND_LEVEL;
    windSpeedRaw = 0;
  }

  // ==========================================================
  //  LIGHT
  // ==========================================================
  if (lightConnected) {
    long adcSum = 0;

    for (int i = 0; i < 8; i++) {
      adcSum += analogRead(PIN_LIGHT_ADC);
    }

    lightLevel = (int)(adcSum / 8);
  } else {
    lightLevel = DEFAULT_LIGHT_ADC;
  }

  // ==========================================================
  //  RAIN (TIPPING BUCKET)
  // ==========================================================
  if (rainConnected) {
    // Copy ISR-updated tip count safely
    uint32_t tips;
    noInterrupts();
    tips = rainTipCountISR;
    rainTipCountISR = 0;
    interrupts();

    rainTipsInterval = tips;
    rainTipCountTotal += tips;

    // Convert to tips/minute
    float intervalSec = S_MEAS_CLK;
    if (intervalSec < 1.0f) intervalSec = 1.0f;

    rainTipsPerMin = ((float)rainTipsInterval * 60.0f) / intervalSec;

    // Optional mm/hour conversion
    rainMmPerHour = rainTipsPerMin * RAIN_MM_PER_TIP * 60.0f;

    // Update rain activity timestamp if any tips occurred
    if (rainTipsInterval > 0) {
      lastRainActivityMs = millis();
    }

    // Keep "raining" true briefly after the last tip
    isRaining = (millis() - lastRainActivityMs < RAIN_HOLD_MS);
  } else {
    rainTipsInterval = 0;
    rainTipsPerMin = DEFAULT_RAIN_TIPS_MIN;
    rainMmPerHour = 0.0f;
    isRaining = false;
  }

  // ==========================================================
  //  DEBUG OUTPUT
  // ==========================================================
  DEBUG_PRINT(
    "[SENSOR] T=%.1fC H=%d%% Wind=%s Rain=%.2f tips/min Light=%d\n",
    tempDegC,
    humidPercent,
    windLevelStr[windLevel],
    rainTipsPerMin,
    lightLevel
  );
}

// ============================================================
//  WIND MEASUREMENT
// ============================================================
//
//  Reads PWM pulse width and converts it to a wind category.
//  This is still lookup-table based rather than true calibrated mph.
// ============================================================

// Measure one PWM high pulse width in microseconds
int measureWindPulseWidth() {
  int pw = (int)pulseIn(PIN_WIND_PWM, HIGH, 50000);
  return pw;
}

// Take several samples and classify wind level
WindLevel measureWindLevel() {
  const int SAMPLES = 5;
  long total = 0;

  for (int i = 0; i < SAMPLES; i++) {
    total += measureWindPulseWidth();
    delay(5);
  }

  int avgPW = (int)(total / SAMPLES);
  windSpeedRaw = avgPW;

  // Wider pulse = lower wind speed based on your sensor model
  if (avgPW == 0 || avgPW > 40000) return WIND_CALM;
  if (avgPW > 25000)               return WIND_LOW;
  if (avgPW > 15000)               return WIND_MEDIUM;
  if (avgPW > 8000)                return WIND_HIGH;
  return WIND_STORM;
}

// Convert internal wind category to approximate mph*10
int windLevelToMphX10(WindLevel wl) {
  return windLevelMphX10[(int)wl];
}

// ============================================================
//  BLE READ HANDLER
// ============================================================
//
//  BLE writes are already handled by the callback class.
//  This function is here in case you later want extra BLE logic.
// ============================================================
void readBLE() {
  // No polling needed at present
}

// ============================================================
//  BLE DATA WRITE
// ============================================================
//
//  Sends current live sensor data to the app using binary Int32.
// ============================================================
void writeBLE() {
  if (!bleConnected) return;

  // Scale data according to BLE specification
  int32_t tempX100    = (int32_t)(tempDegC * 100.0f);                      // °C *100
  int32_t humidX100   = (int32_t)(humidPercent * 100);                     // % *100
  int32_t windX10     = (int32_t)(windLevelToMphX10(windLevel));           // mph *10
  int32_t rainX10     = (int32_t)(rainTipsPerMin * 10.0f);                 // tips/min *10
  int32_t lightPctX10 = (int32_t)(((float)lightLevel * 1000.0f) / ADC_MAX); // % *10

  // ----------------------------------------------------------
  // Serial print of all BLE data being sent
  // First line: raw/scaled BLE values
  // Second line: human-readable values
  // ----------------------------------------------------------
  Serial.print("[BLE SEND] ");
  Serial.print("Temp=");
  Serial.print(tempX100);
  Serial.print(" | Humid=");
  Serial.print(humidX100);
  Serial.print(" | Wind=");
  Serial.print(windX10);
  Serial.print(" | Rain=");
  Serial.print(rainX10);
  Serial.print(" | Light=");
  Serial.println(lightPctX10);

  Serial.print("[BLE REAL] ");
  Serial.print("Temp=");
  Serial.print(tempDegC, 2);
  Serial.print(" C | Humid=");
  Serial.print(humidPercent);
  Serial.print(" % | Wind=");
  Serial.print(windX10 / 10.0f, 1);
  Serial.print(" mph | Rain=");
  Serial.print(rainTipsPerMin, 2);
  Serial.print(" tips/min | Light=");
  Serial.print(lightPctX10 / 10.0f, 1);
  Serial.println(" %");

  // Update and notify characteristics
  setCharacteristicInt32(pCharTemp, tempX100);     pCharTemp->notify();
  setCharacteristicInt32(pCharHumid, humidX100);   pCharHumid->notify();
  setCharacteristicInt32(pCharWind, windX10);      pCharWind->notify();
  setCharacteristicInt32(pCharRain, rainX10);      pCharRain->notify();
  setCharacteristicInt32(pCharLight, lightPctX10); pCharLight->notify();

  DEBUG_PRINTLN("[BLE] Data sent");
}

// ============================================================
//  SYNC SETTINGS TO BLE
// ============================================================
//
//  Updates BLE settings/control characteristics so the app sees
//  the current values after LCD edits or BLE connection.
// ============================================================
void syncSettingsToBLE() {
  // Settings service
  setCharacteristicInt32(pCharClock,      (int32_t)S_MEAS_CLK);
  setCharacteristicInt32(pCharBacklight,  (int32_t)(S_BL_TMR * 10.0f));
  setCharacteristicBool(pCharLcdLock,     S_LCD_LOCK >= 1.0f);
  setCharacteristicBool(pCharDebug,       S_DEBUG >= 1.0f);

  if (pCharServiceName) pCharServiceName->setValue(serviceName.c_str());
  if (pCharLocalName)   pCharLocalName->setValue(localName.c_str());

  // Controls service
  setCharacteristicInt32(pCharTempSp,   (int32_t)(S_TEMP_SP * 10.0f));
  setCharacteristicInt32(pCharTempHys,  (int32_t)(S_TEMP_HYS * 10.0f));
  setCharacteristicInt32(pCharHumSp,    (int32_t)(S_HUMID_SP));
  setCharacteristicInt32(pCharHumHys,   (int32_t)(S_HUMID_HYS));
  setCharacteristicInt32(pCharWindMax,  (int32_t)(S_WIND_MAX * 10.0f));
  setCharacteristicInt32(pCharLightMin, (int32_t)(S_LIGHT_MIN));
  setCharacteristicBool(pCharLightOvr,  C_LIGHT_OVR >= 1.0f);
  setCharacteristicBool(pCharLightSet,  C_LIGHT_SET >= 1.0f);
  setCharacteristicBool(pCharWinOvr,    C_WIN_OVR >= 1.0f);
  setCharacteristicBool(pCharWinSet,    C_WIN_SET >= 1.0f);
}

// ============================================================
//  AUTOMATIC CONTROL LOGIC
// ============================================================
//
//  Controls window, heater, cooler, and lighting.
// ============================================================
void autoControl() {
  bool targetWindowOpen = windowIsOpen;

  // ----- Window control -----
  if (C_WIN_OVR >= 1.0f) {
    // Manual override active
    targetWindowOpen = (C_WIN_SET >= 1.0f);
  } else {
    // Automatic mode
    bool windTooHigh = (windLevelToMphX10(windLevel) >= (int)(S_WIND_MAX * 10.0f));

    // Rain or high wind forces the window closed
    if (windTooHigh || isRaining) {
      targetWindowOpen = false;
    } else {
      // Temperature based window control
      if      (tempDegCavg > S_TEMP_SP + S_TEMP_HYS) targetWindowOpen = true;
      else if (tempDegCavg < S_TEMP_SP - S_TEMP_HYS) targetWindowOpen = false;

      // Humidity based control
      if (!isRaining) {
        if      (humidPercent > S_HUMID_SP + S_HUMID_HYS) targetWindowOpen = true;
        else if (humidPercent < S_HUMID_SP - S_HUMID_HYS) targetWindowOpen = false;
      }
    }
  }

  // Move window if state changed
  if (targetWindowOpen != windowIsOpen) setWindow(targetWindowOpen);

  // ----- Heater control -----
  bool heaterNeeded = (tempDegCavg < S_TEMP_SP - S_TEMP_HYS);
  if (heaterNeeded != relay1Set) {
    relay1Set = heaterNeeded;
    setRelay(PIN_RELAY1, relay1Set);
    DEBUG_PRINT("[CTRL] Heater %s\n", relay1Set ? "ON" : "OFF");
  }

  // ----- Cooler control -----
  bool coolerNeeded = (tempDegCavg > S_TEMP_SP + S_TEMP_HYS);
  if (coolerNeeded != relay2Set) {
    relay2Set = coolerNeeded;
    setRelay(PIN_RELAY2, relay2Set);
    DEBUG_PRINT("[CTRL] Cooler %s\n", relay2Set ? "ON" : "OFF");
  }

  // ----- Lighting control -----
  int adcThreshold = (int)(((long)S_LIGHT_MIN * ADC_MAX) / 100L);
  bool lightOn = (C_LIGHT_OVR >= 1.0f) ? (C_LIGHT_SET >= 1.0f) : (lightLevel < adcThreshold);
  digitalWrite(PIN_LED_LIGHT, lightOn ? HIGH : LOW);
}

// ============================================================
//  OUTPUT HELPERS
// ============================================================

// Open or close window and log action
void setWindow(bool open) {
  DEBUG_PRINT("[WINDOW] %s\n", open ? "OPENING" : "CLOSING");
  windowIsOpen = open;
  moveServoTo(open ? SERVO_WINDOW_OPEN : SERVO_WINDOW_CLOSED);
}

// Move servo gradually to target angle
void moveServoTo(int targetDeg) {
  int step = (targetDeg > currentServoDeg) ? 1 : -1;

  while (currentServoDeg != targetDeg) {
    currentServoDeg += step;
    windowServo.write(currentServoDeg);
    delay(SERVO_SPEED_DELAY);
  }
}

// Set active-LOW relay output
void setRelay(int pin, bool state) {
  digitalWrite(pin, state ? LOW : HIGH);
}

// ============================================================
//  UTILITY FUNCTIONS
// ============================================================

// Format float value as String for LCD display
String formatFloat(float val, int dp) {
  char buf[16];
  if      (dp == 1) snprintf(buf, sizeof(buf), "%.1f", val);
  else if (dp == 2) snprintf(buf, sizeof(buf), "%.2f", val);
  else              snprintf(buf, sizeof(buf), "%.0f", val);
  return String(buf);
}

// Turn LCD backlight back on and reset timer
void touchBacklight() {
  if (!backlightOn) {
    lcd.backlight();
    backlightOn = true;
  }
  backlightOnMs = millis();
}

// Turn LCD backlight off after timeout
void checkBacklightTimeout() {
  unsigned long timeout = (unsigned long)(S_BL_TMR * 60000.0f);
  if (backlightOn && (millis() - backlightOnMs >= timeout)) {
    lcd.noBacklight();
    backlightOn = false;
  }
}

// ============================================================
//  SETTING CHANGE SCREEN
// ============================================================
//
//  Generic editor used by both Settings and Controls menus.
// ============================================================
void Setting_Change(Set* arr, uint8_t idx) {
  bool   changing     = true;
  bool   valueChanged = true;
  float  changeHold   = arr[idx].Value;

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Setting:            ");
  lcd.setCursor(9, 0); lcd.print(arr[idx].Name);
  lcd.setCursor(0, 1); lcd.print("Default: ");
  lcd.print(formatFloat(arr[idx].Defult, 2));
  lcd.setCursor(0, 2); lcd.print("Current: ");
  lcd.print(formatFloat(arr[idx].Value, 2));

  lastStateCLK = digitalRead(PIN_ENC_CLK);

  do {
    checkBacklightTimeout();

    if (valueChanged) {
      lcd.setCursor(0, 3);  lcd.print("Adjust:             ");
      lcd.setCursor(8, 3);  lcd.print(formatFloat(changeHold, 2));
      valueChanged = false;
    }

    int curCLK = digitalRead(PIN_ENC_CLK);
    if (curCLK != lastStateCLK && curCLK == HIGH) {
      touchBacklight();

      // Encoder direction determines increase / decrease
      if (digitalRead(PIN_ENC_DT) != curCLK) {
        if (changeHold > arr[idx].Min) {
          changeHold -= arr[idx].Step;
          if (changeHold < arr[idx].Min) changeHold = arr[idx].Min;
        }
      } else {
        if (changeHold < arr[idx].Max) {
          changeHold += arr[idx].Step;
          if (changeHold > arr[idx].Max) changeHold = arr[idx].Max;
        }
      }
      valueChanged = true;
    }
    lastStateCLK = curCLK;

    // Push button confirms change
    if (digitalRead(PIN_ENC_SW) == LOW) {
      touchBacklight();
      arr[idx].Value = changeHold;
      syncSettingsToBLE();
      changing = false;
      delay(250); // debounce
    }

  } while (changing);
}

// ============================================================
//  SETTINGS MENU
// ============================================================
void Settings_Menu() {
  uint8_t selection  = 0;
  int8_t  selLast    = -1;
  uint8_t localSel   = 0;
  uint8_t dispTop    = 0;
  bool    run        = true;
  bool    needRedraw = true;

  lastStateCLK = digitalRead(PIN_ENC_CLK);
  lcd.clear();

  while (run) {
    checkBacklightTimeout();

    if (needRedraw) {
      lcd.clear();

      for (int j = 0; j < LCD_ROWS; j++) {
        if (j + dispTop >= SETTINGS_COUNT) break;

        lcd.setCursor(1, j);
        lcd.print(Settings[j + dispTop].Name);

        lcd.setCursor(15, j);
        if (j + dispTop == SETTINGS_COUNT - 1) {
          lcd.print("EXIT ");
        } else if (Settings[j + dispTop].Max == 1.0f &&
                   Settings[j + dispTop].Min == 0.0f &&
                   Settings[j + dispTop].Step == 1.0f) {
          lcd.print(Settings[j + dispTop].Value >= 1.0f ? "ON " : "OFF");
        } else {
          lcd.print(formatFloat(Settings[j + dispTop].Value, 1));
        }
      }

      lcd.setCursor(0, localSel);
      lcd.print(">");

      if (S_DEBUG) { lcd.setCursor(19, 3); lcd.print(selection); }

      needRedraw = false;
      selLast = selection;
    }

    int curCLK = digitalRead(PIN_ENC_CLK);
    if (curCLK != lastStateCLK && curCLK == HIGH) {
      touchBacklight();

      if (digitalRead(PIN_ENC_DT) != curCLK) {
        if (selection > 0) {
          selection--;
          if (localSel == 0) dispTop--;
          else               localSel--;
        }
      } else {
        if (selection < SETTINGS_COUNT - 1) {
          selection++;
          if (localSel == LCD_ROWS - 1) dispTop++;
          else                          localSel++;
        }
      }
      needRedraw = (selection != selLast);
    }
    lastStateCLK = curCLK;

    if (digitalRead(PIN_ENC_SW) == LOW) {
      touchBacklight();
      delay(250);

      if (selection == SETTINGS_COUNT - 1) {
        run = false;
      } else {
        Setting_Change(Settings, selection);
        needRedraw = true;
      }
    }
  }

  delay(250);
}

// ============================================================
//  CONTROLS MENU
// ============================================================
void Controls_Menu() {
  uint8_t selection  = 0;
  int8_t  selLast    = -1;
  uint8_t localSel   = 0;
  uint8_t dispTop    = 0;
  bool    run        = true;
  bool    needRedraw = true;

  lastStateCLK = digitalRead(PIN_ENC_CLK);
  lcd.clear();

  while (run) {
    checkBacklightTimeout();

    if (needRedraw) {
      lcd.clear();

      for (int j = 0; j < LCD_ROWS; j++) {
        if (j + dispTop >= CONTROLS_COUNT) break;

        lcd.setCursor(1, j);
        lcd.print(Controls[j + dispTop].Name);

        lcd.setCursor(15, j);
        if (j + dispTop == CONTROLS_COUNT - 1) {
          lcd.print("EXIT ");
        } else {
          lcd.print(Controls[j + dispTop].Value >= 1.0f ? "ON " : "OFF");
        }
      }

      lcd.setCursor(0, localSel);
      lcd.print(">");

      needRedraw = false;
      selLast = selection;
    }

    int curCLK = digitalRead(PIN_ENC_CLK);
    if (curCLK != lastStateCLK && curCLK == HIGH) {
      touchBacklight();

      if (digitalRead(PIN_ENC_DT) != curCLK) {
        if (selection > 0) {
          selection--;
          if (localSel == 0) dispTop--;
          else               localSel--;
        }
      } else {
        if (selection < CONTROLS_COUNT - 1) {
          selection++;
          if (localSel == LCD_ROWS - 1) dispTop++;
          else                          localSel++;
        }
      }
      needRedraw = (selection != selLast);
    }
    lastStateCLK = curCLK;

    if (digitalRead(PIN_ENC_SW) == LOW) {
      touchBacklight();
      delay(250);

      if (selection == CONTROLS_COUNT - 1) {
        run = false;
      } else {
        Setting_Change(Controls, selection);
        syncSettingsToBLE();
        needRedraw = true;
      }
    }
  }

  delay(250);
}

// ============================================================
//  MONITOR SCREEN
// ============================================================
//
//  Live display of main sensor and control values.
// ============================================================
void Monitor_Screen() {
  bool run = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("____PAWS_MONITOR____");

  lastStateCLK = digitalRead(PIN_ENC_CLK);

  while (run) {
    checkBacklightTimeout();

    // Periodic data update
    unsigned long now = millis();
    if (now - lastMeasurementMs >= (unsigned long)(S_MEAS_CLK * 1000.0f)) {
      lastMeasurementMs = now;
      readSensors();
      readBLE();
      writeBLE();
      autoControl();
    }

    // LCD update
    lcd.setCursor(0, 1);
    lcd.print("T:"); lcd.print(formatFloat(tempDegC, 1)); lcd.print("C ");
    lcd.print("H:"); lcd.print(humidPercent);            lcd.print("%   ");

    lcd.setCursor(0, 2);
    lcd.print("W:"); lcd.print(windLevelStr[windLevel]); lcd.print("   ");
    lcd.setCursor(12, 2);
    lcd.print("L:");
    lcd.print((lightLevel * 100) / ADC_MAX);
    lcd.print("% ");

    lcd.setCursor(0, 3);
    lcd.print("R:");
    lcd.print(formatFloat(rainTipsPerMin, 1));
    lcd.print("/m ");

    lcd.setCursor(9, 3);
    lcd.print(relay1Set ? "HT " : "   ");
    lcd.print(relay2Set ? "CL " : "   ");
    lcd.print(windowIsOpen ? "WIN" : "   ");

    lcd.setCursor(17, 0);
    lcd.print(bleConnected ? "BLE" : "   ");

    // Exit screen when encoder button pressed
    if (digitalRead(PIN_ENC_SW) == LOW) {
      touchBacklight();
      run = false;
      delay(250);
    }

    delay(200);
  }
}

// ============================================================
//  MAIN MENU
// ============================================================
//
//  Top-level menu with Monitor / Settings / Controls.
// ============================================================
void Menu_Select() {
  const char* mainMenu[] = { "Monitor", "Settings", "Controls" };
  const uint8_t MENU_LEN = 3;

  uint8_t selection  = 0;
  int8_t  selLast    = -1;
  uint8_t localSel   = 0;
  uint8_t dispTop    = 0;
  bool    run        = true;

  lastStateCLK = digitalRead(PIN_ENC_CLK);
  lcd.clear();

  while (run) {
    checkBacklightTimeout();

    // Keep system running even when idle in main menu
    unsigned long now = millis();
    if (now - lastMeasurementMs >= (unsigned long)(S_MEAS_CLK * 1000.0f)) {
      lastMeasurementMs = now;
      readSensors();
      readBLE();
      writeBLE();
      autoControl();
    }

    if (selection != selLast) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("_____PAWS_MENU______");

      for (int i = 0; i < LCD_ROWS - 1; i++) {
        if (i + dispTop < MENU_LEN) {
          lcd.setCursor(2, i + 1);
          lcd.print(mainMenu[i + dispTop]);
        }
      }

      lcd.setCursor(1, localSel + 1);
      lcd.print(">");

      if (S_DEBUG) { lcd.setCursor(19, 3); lcd.print(selection); }

      selLast = selection;
    }

    int curCLK = digitalRead(PIN_ENC_CLK);
    if (curCLK != lastStateCLK && curCLK == HIGH) {
      touchBacklight();

      if (digitalRead(PIN_ENC_DT) != curCLK) {
        if (selection > 0) {
          selection--;
          if (localSel == 0) dispTop--;
          else               localSel--;
        }
      } else {
        if (selection < MENU_LEN - 1) {
          selection++;
          if (localSel == LCD_ROWS - 2) dispTop++;
          else                          localSel++;
        }
      }
    }
    lastStateCLK = curCLK;

    // Select highlighted menu item
    if (digitalRead(PIN_ENC_SW) == LOW) {
      touchBacklight();
      delay(250);
      run = false;
    }

    delay(50);
  }

  delay(250);
  lcd.clear();

  switch (selection) {
    case 0: Monitor_Screen(); break;
    case 1: Settings_Menu();  break;
    case 2: Controls_Menu();  break;
  }
}