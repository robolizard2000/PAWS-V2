// ============================================================
//  Automated Environmental Control System
//  ESP32 Embedded Software  |  Version 0.1
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
// A single BLE service with multiple characteristics.
// UUIDs are arbitrary but must be consistent with the app.
#define BLE_SERVICE_UUID          "12345678-1234-1234-1234-123456789000"

// --- Sensor data characteristics (Notify only - W in spec) ---
#define BLE_CHAR_TEMP_UUID        "12345678-1234-1234-1234-123456789001"
#define BLE_CHAR_HUMID_UUID       "12345678-1234-1234-1234-123456789002"
#define BLE_CHAR_WIND_UUID        "12345678-1234-1234-1234-123456789003"
#define BLE_CHAR_RAIN_UUID        "12345678-1234-1234-1234-123456789004"
#define BLE_CHAR_LIGHT_UUID       "12345678-1234-1234-1234-123456789005"

// --- Settings/controls (Read + Write + Notify - R/N in spec) ---
#define BLE_CHAR_MEAS_CLK_UUID    "12345678-1234-1234-1234-12345678900A"
#define BLE_CHAR_BL_TMR_UUID      "12345678-1234-1234-1234-12345678900B"
#define BLE_CHAR_LCD_LOCK_UUID    "12345678-1234-1234-1234-12345678900C"
#define BLE_CHAR_DEBUG_UUID       "12345678-1234-1234-1234-12345678900D"
#define BLE_CHAR_TEMP_SP_UUID     "12345678-1234-1234-1234-12345678900E"
#define BLE_CHAR_TEMP_HYS_UUID    "12345678-1234-1234-1234-12345678900F"
#define BLE_CHAR_HUM_SP_UUID      "12345678-1234-1234-1234-123456789010"
#define BLE_CHAR_HUM_HYS_UUID     "12345678-1234-1234-1234-123456789011"
#define BLE_CHAR_WIND_MAX_UUID    "12345678-1234-1234-1234-123456789012"
#define BLE_CHAR_LIGHT_MIN_UUID   "12345678-1234-1234-1234-123456789013"
#define BLE_CHAR_LIGHT_OVR_UUID   "12345678-1234-1234-1234-123456789014"
#define BLE_CHAR_LIGHT_SET_UUID   "12345678-1234-1234-1234-123456789015"
#define BLE_CHAR_WIN_OVR_UUID     "12345678-1234-1234-1234-123456789016"
#define BLE_CHAR_WIN_SET_UUID     "12345678-1234-1234-1234-123456789017"

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
int   measurementClk      = 10;    // seconds between sensor reads / BLE updates
int   backlightSleepTMR   = 2;     // minutes before LCD backlight off
bool  LCDLock             = false; // if true, encoder input is ignored
bool  debugEN             = false; // debug info on LCD / BLE

String serviceName = "PAWS_BLE";   // BLE advertised service name
String localName   = "PAWS";       // BLE local name

// ============================================================
//  CONTROL VARIABLES
// ============================================================
float tempSetPoint        = 22.0;  // °C
float tempHysteresis      = 1.0;   // °C
float humidSetPoint       = 55.0;  // %RH
float humidHysteresis     = 5.0;   // %RH
int   windMax             = WIND_HIGH;  // max wind level before forced close
int   lightMin            = 1000;  // ADC val below this = lights on adjust as tested

bool  lightOverride       = false; // true = force lights on via app/menu
bool  lightSet            = false; // Light state
bool  windowOverride      = false; // true = force window open via app/menu
bool  windowSet           = false; // window state

bool  relay1Set           = false; // heater state
bool  relay2Set           = false; // cooler state

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
BLEServer*          pServer       = nullptr;
BLECharacteristic*  pCharTemp     = nullptr;
BLECharacteristic*  pCharHumid    = nullptr;
BLECharacteristic*  pCharWind     = nullptr;
BLECharacteristic*  pCharRain     = nullptr;
BLECharacteristic*  pCharLight    = nullptr;
bool bleConnected = false;
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

// BLE write callback - called when app writes a setting characteristic.
// Add cases here if more writeable characteristics are implemented.
class SettingWriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    String uuid = pChar->getUUID().toString();
    String val  = pChar->getValue();
    if (val.length() == 0) return;

    if (uuid == BLE_CHAR_MEAS_CLK_UUID) {
      measurementClk = val.toInt();
      Serial.printf("[BLE] measurementClk set to %d s\n", measurementClk);
    } else if (uuid == BLE_CHAR_TEMP_SP_UUID) {
      tempSetPoint = val.toFloat();
    } else if (uuid == BLE_CHAR_TEMP_HYS_UUID) {
      tempHysteresis = val.toFloat();
    } else if (uuid == BLE_CHAR_HUM_SP_UUID) {
      humidSetPoint = val.toFloat();
    } else if (uuid == BLE_CHAR_HUM_HYS_UUID) {
      humidHysteresis = val.toFloat();
    } else if (uuid == BLE_CHAR_WIND_MAX_UUID) {
      windMax = val.toInt();
    } else if (uuid == BLE_CHAR_LIGHT_MIN_UUID) {
      lightMin = val.toInt();
    } else if (uuid == BLE_CHAR_LIGHT_OVR_UUID) {
      lightOverride = (val != "0");
    } else if (uuid == BLE_CHAR_LIGHT_SET_UUID) {
      lightSet = (val != "0");
    } else if (uuid == BLE_CHAR_WIN_OVR_UUID) {
      windowOverride = (val != "0");
    } else if (uuid == BLE_CHAR_WIN_SET_UUID) {
      windowSet = (val != "0");
    } else if (uuid == BLE_CHAR_LCD_LOCK_UUID) {
      LCDLock = (val != "0");
    } else if (uuid == BLE_CHAR_DEBUG_UUID) {
      debugEN = (val != "0");
    }
  }
};

// ============================================================
//  HELPER: add a BLE characteristic to the service
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
  if (backlightOn && (now - backlightOnMs >= (unsigned long)backlightSleepTMR * 60000UL)) {
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
  lcd.setCursor(0, 0); lcd.print("   PAWS  v0.1   ");
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

  BLEService* pService = pServer->createService(BLEUUID(BLE_SERVICE_UUID), 64);

  SettingWriteCallback* writeCB = new SettingWriteCallback();

  // Sensor data (notify only - app to receive values)
  uint32_t notifyProp = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY;
  pCharTemp  = addCharacteristic(pService, BLE_CHAR_TEMP_UUID,  notifyProp, true);
  pCharHumid = addCharacteristic(pService, BLE_CHAR_HUMID_UUID, notifyProp, true);
  pCharWind  = addCharacteristic(pService, BLE_CHAR_WIND_UUID,  notifyProp, true);
  pCharRain  = addCharacteristic(pService, BLE_CHAR_RAIN_UUID,  notifyProp, true);
  pCharLight = addCharacteristic(pService, BLE_CHAR_LIGHT_UUID, notifyProp, true);

  // Settings / controls (read + write + notify so app can also read current value)
  uint32_t rwProp = BLECharacteristic::PROPERTY_READ |
                    BLECharacteristic::PROPERTY_WRITE |
                    BLECharacteristic::PROPERTY_NOTIFY;

  addCharacteristic(pService, BLE_CHAR_MEAS_CLK_UUID, rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_BL_TMR_UUID,   rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_LCD_LOCK_UUID,  rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_DEBUG_UUID,     rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_TEMP_SP_UUID,   rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_TEMP_HYS_UUID,  rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_HUM_SP_UUID,    rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_HUM_HYS_UUID,   rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_WIND_MAX_UUID,  rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_LIGHT_MIN_UUID, rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_LIGHT_OVR_UUID, rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_LIGHT_SET_UUID, rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_WIN_OVR_UUID,   rwProp, true, writeCB);
  addCharacteristic(pService, BLE_CHAR_WIN_SET_UUID,   rwProp, true, writeCB);

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising as: " + localName);
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
//  WRITE BLE - push current sensor data + state to app
// ============================================================
void writeBLE() {
  if (!bleConnected) return;

  // Formatted values as null-terminated strings for simplicity.
  // The mobile app should parse these accordingly.
  char buf[24];

  snprintf(buf, sizeof(buf), "%.1f", tempDegC);
  pCharTemp->setValue(buf);
  pCharTemp->notify();

  snprintf(buf, sizeof(buf), "%d", humidPercent);
  pCharHumid->setValue(buf);
  pCharHumid->notify();

  snprintf(buf, sizeof(buf), "%s", windLevelStr[windLevel]);
  pCharWind->setValue(buf);
  pCharWind->notify();

  snprintf(buf, sizeof(buf), "%d", isRaining ? 1 : 0);
  pCharRain->setValue(buf);
  pCharRain->notify();

  snprintf(buf, sizeof(buf), "%d", lightLevel);
  pCharLight->setValue(buf);
  pCharLight->notify();
}

// ============================================================
//  AUTOMATIC CONTROL LOGIC
// ============================================================
void autoControl() {

  // ---- Window control ----
  bool targetWindowOpen = windowIsOpen;  // default: no change

  if (windowOverride) {
    // Manual override from app or menu
    targetWindowOpen = windowSet;
  } else {
    // Safety rules - force closed regardless of other conditions
    if ((int)windLevel >= windMax || isRaining) {
      targetWindowOpen = false;
    } else {
      // Temperature-driven logic
      if (tempDegCavg > (tempSetPoint + tempHysteresis)) {
        targetWindowOpen = true;   // too hot → open to ventilate
      } else if (tempDegCavg < (tempSetPoint - tempHysteresis)) {
        targetWindowOpen = false;  // too cold → close
      }

      // Humidity-driven logic (only when not raining)
      if (!isRaining) {
        if (humidPercent > (int)(humidSetPoint + humidHysteresis)) {
          targetWindowOpen = true;   // too humid → open to ventilate
        } else if (humidPercent < (int)(humidSetPoint - humidHysteresis)) {
          targetWindowOpen = true;   // too dry → open window per spec
        }
      }
    }
  }

  if (targetWindowOpen != windowIsOpen) {
    setWindow(targetWindowOpen);
  }

  // ---- Relay: Heater (Relay1) ----
  // Turn on heater if temp is below setpoint - hysteresis
  bool heaterNeeded = (tempDegCavg < (tempSetPoint - tempHysteresis));
  if (heaterNeeded != relay1Set) {
    relay1Set = heaterNeeded;
    setRelay(PIN_RELAY1, relay1Set);
    Serial.printf("Heater %s\n", relay1Set ? "ON" : "OFF");
  }

  // ---- Relay: Cooler (Relay2) ----
  bool coolerNeeded = (tempDegCavg > (tempSetPoint + tempHysteresis));
  if (coolerNeeded != relay2Set) {
    relay2Set = coolerNeeded;
    setRelay(PIN_RELAY2, relay2Set);
    Serial.printf("Cooler %s\n", relay2Set ? "ON" : "OFF");
  }

  // ---- Lighting LED ----
  bool lightOn;
  if (lightOverride) {
    lightOn = lightSet;
  } else {
    lightOn = (lightLevel < lightMin);  // below threshold → lights needed
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
      lcd.setCursor(0, 2); lcd.print("Setpt:   " + formatFloat(tempSetPoint, 1) + " C    ");
      lcd.setCursor(0, 3); lcd.print("Hys: +/-" + formatFloat(tempHysteresis, 1) + " C   ");
      break;

    case PAGE_HUMID:
      lcd.setCursor(0, 0); lcd.print("-- Humidity -----   ");
      lcd.setCursor(0, 1); lcd.print("Current: " + String(humidPercent) + " %      ");
      lcd.setCursor(0, 2); lcd.print("Setpt:   " + formatFloat(humidSetPoint, 1) + " %  ");
      lcd.setCursor(0, 3); lcd.print("Hys: +/-" + formatFloat(humidHysteresis, 1) + " % ");
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
      lcd.setCursor(0, 1); lcd.print("Clk: " + String(measurementClk) + "s BL:" + String(backlightSleepTMR) + "m ");
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
