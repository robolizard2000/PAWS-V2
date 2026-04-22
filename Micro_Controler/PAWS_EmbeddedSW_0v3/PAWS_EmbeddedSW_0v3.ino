// ============================================================
//  PAWS - Automated Environmental Control System
//  ESP32 Embedded Software  |  Version 0.3
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
//  BLE CONFIGURATION  (unchanged from v0.2 UUID map)
// ============================================================
#define BLE_SERVICE_UUID          "12345678-1234-1234-1234-123456789000"
#define BLE_CHAR_TEMP_UUID        "12345678-1234-1234-1234-123456789001"
#define BLE_CHAR_HUMID_UUID       "12345678-1234-1234-1234-123456789002"
#define BLE_CHAR_WIND_UUID        "12345678-1234-1234-1234-123456789003"
#define BLE_CHAR_RAIN_UUID        "12345678-1234-1234-1234-123456789004"
#define BLE_CHAR_LIGHT_UUID       "12345678-1234-1234-1234-123456789005"
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
enum WindLevel { WIND_CALM = 0, WIND_LOW, WIND_MEDIUM, WIND_HIGH, WIND_STORM };
const char* windLevelStr[] = { "Calm", "Low", "Medium", "High", "Storm" };

#define SERVO_WINDOW_OPEN    90
#define SERVO_WINDOW_CLOSED   0
#define SERVO_SPEED_DELAY    20

#define LCD_COLS      20
#define LCD_ROWS       4
#define LCD_I2C_ADDR  0x27
#define ADC_MAX       4095

#define RAIN_DEBOUNCE_US      50000UL   // 50 ms debounce for tipping bucket switch
#define RAIN_MM_PER_TIP       0.2794f   // example number change to  gauge spec

// ============================================================
//  Set STRUCT  (mirrors Carter's reference implementation)
//  Holds everything needed to display and edit one setting.
// ============================================================
struct Set {
  const char* Name;    // Label shown on LCD (max ~14 chars to fit neatly)
  float       Value;   // Current value
  float       Defult;  // Default / reset value
  float       Step;    // Increment per encoder click
  float       Min;     // Lower bound
  float       Max;     // Upper bound

  Set(const char* name, float value, float defult, float step, float min, float max)
    : Name(name), Value(value), Defult(defult), Step(step), Min(min), Max(max) {}
};

// ============================================================
//  SETTINGS ARRAY  (index reference in comments)
//  These drive the Settings menu and map back to BLE
//  characteristics via syncSettingsToBLE() called after any edit.
// ============================================================
//                    Name            Val   Def   Step  Min    Max
Set Settings[] = {
  Set("Temp Setpoint", 22.0, 22.0, 0.5,  0.0,  50.0),  // 0  °C
  Set("Temp Hyster.",   1.0,  1.0, 0.1,  0.0,  10.0),  // 1  °C
  Set("Humid Setpoint",55.0, 55.0, 1.0, 10.0,  90.0),  // 2  %RH
  Set("Humid Hyster.",  5.0,  5.0, 0.5,  0.0,  20.0),  // 3  %RH
  Set("Wind Max",       3.0,  3.0, 1.0,  0.0,   4.0),  // 4  enum index (0=Calm..4=Storm)
  Set("Light Min %",   20.0, 20.0, 1.0,  0.0, 100.0),  // 5  % (0-100)
  Set("Meas. Clock",   10.0, 10.0, 1.0,  1.0, 300.0),  // 6  seconds
  Set("Backlight min",  2.0,  2.0, 0.5,  0.5,  30.0),  // 7  minutes
  Set("Debug",          0.0,  0.0, 1.0,  0.0,   1.0),  // 8  bool (0/1)
  Set("Exit",           0.0,  0.0, 0.0,  0.0,   0.0),  // 9  EXIT sentinel
};
const uint8_t SETTINGS_COUNT = sizeof(Settings) / sizeof(Settings[0]);

// ============================================================
//  CONTROLS ARRAY  (manual overrides shown in Controls menu)
//  Step=1, Min=0, Max=1 for all booleans.
// ============================================================
Set Controls[] = {
  Set("Win Override",  0.0, 0.0, 1.0, 0.0, 1.0),  // 0  bool
  Set("Window Set",    0.0, 0.0, 1.0, 0.0, 1.0),  // 1  bool (1=open)
  Set("Light Override",0.0, 0.0, 1.0, 0.0, 1.0),  // 2  bool
  Set("Light Set",     0.0, 0.0, 1.0, 0.0, 1.0),  // 3  bool (1=on)
  Set("Exit",          0.0, 0.0, 0.0, 0.0, 0.0),  // 4  EXIT sentinel
};
const uint8_t CONTROLS_COUNT = sizeof(Controls) / sizeof(Controls[0]);

// Convenience accessors — keeps autoControl() readable
#define S_TEMP_SP     Settings[0].Value
#define S_TEMP_HYS    Settings[1].Value
#define S_HUMID_SP    Settings[2].Value
#define S_HUMID_HYS   Settings[3].Value
#define S_WIND_MAX    Settings[4].Value
#define S_LIGHT_MIN   Settings[5].Value
#define S_MEAS_CLK    Settings[6].Value
#define S_BL_TMR      Settings[7].Value
#define S_DEBUG       Settings[8].Value

#define C_WIN_OVR     Controls[0].Value
#define C_WIN_SET     Controls[1].Value
#define C_LIGHT_OVR   Controls[2].Value
#define C_LIGHT_SET   Controls[3].Value

// ============================================================
//  SENSOR / DATA VARIABLES
// ============================================================
float     tempDegC        = 0.0;
float     tempDegCavg     = 0.0;
int       humidPercent    = 0;
int       windSpeedRaw    = 0;
WindLevel windLevel       = WIND_CALM;
float    rainTipsPerMin    = 0.0f;         // rate of rain sensor toggle
float    rainMmPerHour     = 0.0f;         // will need calibration
bool     isRaining         = false;        // derived from sensor switch activity
volatile uint32_t rainTipCountISR = 0;     // incremented in ISR
volatile unsigned long lastRainTipUs = 0;  // debounce timer for ISR
uint32_t rainTipCountTotal = 0;            // total tips since startup
uint32_t rainTipsInterval  = 0;            // tips since last sensor update
unsigned long lastRainActivityMs = 0;
int       lightLevel      = 0;

// ============================================================
//  TIMING
// ============================================================
unsigned long lastMeasurementMs = 0;
unsigned long backlightOnMs     = 0;
bool          backlightOn       = true;

// ============================================================
//  RUNTIME STATE
// ============================================================
bool relay1Set     = false;
bool relay2Set     = false;
bool windowIsOpen  = false;
int  currentServoDeg = SERVO_WINDOW_CLOSED;

// ============================================================
//  HARDWARE OBJECTS
// ============================================================
Adafruit_AHTX0    aht;
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
Servo             windowServo;

// ============================================================
//  BLE OBJECTS
// ============================================================
BLEServer*          pServer    = nullptr;
BLECharacteristic*  pCharTemp  = nullptr;
BLECharacteristic*  pCharHumid = nullptr;
BLECharacteristic*  pCharWind  = nullptr;
BLECharacteristic*  pCharRain  = nullptr;
BLECharacteristic*  pCharLight = nullptr;
bool bleConnected = false;
String localName   = "PAWS";

// ============================================================
//  ENCODER STATE  (used inside blocking menu loops)
// ============================================================
int lastStateCLK = HIGH;
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

// BLE write callback — updates the Settings / Controls arrays and
// re-syncs so autoControl() always reads the latest values.
class SettingWriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    String uuid = pChar->getUUID().toString();
    String val  = pChar->getValue();
    if (val.length() == 0) return;

    if      (uuid == BLE_CHAR_TEMP_SP_UUID)  { Settings[0].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_TEMP_HYS_UUID) { Settings[1].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_HUM_SP_UUID)   { Settings[2].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_HUM_HYS_UUID)  { Settings[3].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_WIND_MAX_UUID) { Settings[4].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_LIGHT_MIN_UUID){ Settings[5].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_MEAS_CLK_UUID) { Settings[6].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_BL_TMR_UUID)   { Settings[7].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_DEBUG_UUID)    { Settings[8].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_WIN_OVR_UUID)  { Controls[0].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_WIN_SET_UUID)  { Controls[1].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_LIGHT_OVR_UUID){ Controls[2].Value = val.toFloat(); }
    else if (uuid == BLE_CHAR_LIGHT_SET_UUID){ Controls[3].Value = val.toFloat(); }

    if (S_DEBUG) Serial.printf("[BLE] Write: %s = %s\n", uuid.c_str(), val.c_str());
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

void IRAM_ATTR rainTipISR() {
  unsigned long nowUs = micros();

  // debounce mechanical switch bounce
  if ((nowUs - lastRainTipUs) > RAIN_DEBOUNCE_US) {
    rainTipCountISR++;
    lastRainTipUs = nowUs;
  }
}

// ============================================================
//  FUNCTION PROTOTYPES
// ============================================================
void initBLE();
void initLCD();
void initSensors();
void initOutputs();
void initEncoder();

void IRAM_ATTR rainTipISR();
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

// Menu screens (blocking — each owns its encoder loop)
void Menu_Select();
void Monitor_Screen();
void Settings_Menu();
void Controls_Menu();
void Setting_Change(Set* arr, uint8_t idx);

String formatFloat(float val, int dp);

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("[PAWS] v0.3 starting...");

  Wire.begin(PIN_SDA, PIN_SCL);

  initOutputs();
  initSensors();
  initLCD();
  initEncoder();
  initBLE();

  // First sensor read so Monitor_Screen has values on first entry
  readSensors();
  backlightOnMs = millis();

  Serial.println("[PAWS] Init complete");
}

// ============================================================
//  LOOP  — delegates entirely to the blocking menu system.
//  Sensor reads and BLE writes happen inside Monitor_Screen
//  on the measurementClk interval, keeping the same timing
//  behaviour as before while screens are active.
// ============================================================
void loop() {
  Menu_Select();
}

// ============================================================
//  INIT FUNCTIONS
// ============================================================
void initOutputs() {
  pinMode(PIN_RELAY1,    OUTPUT);  digitalWrite(PIN_RELAY1, HIGH);  // active LOW relay - off at start
  pinMode(PIN_RELAY2,    OUTPUT);  digitalWrite(PIN_RELAY2, HIGH);
  pinMode(PIN_LED_BLE,   OUTPUT);  digitalWrite(PIN_LED_BLE, LOW);
  pinMode(PIN_LED_LIGHT, OUTPUT);  digitalWrite(PIN_LED_LIGHT, LOW);

  windowServo.attach(PIN_SERVO);
  windowServo.write(SERVO_WINDOW_CLOSED);
  Serial.println("[INIT] Outputs ready");
}

void initSensors() {
  // AHT25
  if (!aht.begin()) {
    Serial.println("ERROR: AHT25 not found - check wiring");
  } else {
    Serial.println("AHT25 ready");
  }

  // Rain sensor - digital input
  pinMode(PIN_RAIN_DIG, INPUT_PULLUP);   // usually best for a bucket switch
  attachInterrupt(digitalPinToInterrupt(PIN_RAIN_DIG), rainTipISR, FALLING);

  // Wind - PWM input (pulse width measurement via pulseIn)
  pinMode(PIN_WIND_PWM, INPUT);

  // Light - ADC input (no pinMode needed for ADC1 on ESP32)
  Serial.println("Sensors ready");
}

void initLCD() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("   PAWS  v0.3   ");
  lcd.setCursor(0, 1); lcd.print("  Initialising..  ");
  delay(1500);
  lcd.clear();
  Serial.println("LCD ready");
}

void initEncoder() {
  // Simple polling — matches Carter's reference approach.
  // No ISR needed: menus are blocking so encoder is polled
  // at the top of each while() iteration.
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);
  lastStateCLK = digitalRead(PIN_ENC_CLK);
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
  sensors_event_t humidity_ev, temp_ev;
  if (aht.getEvent(&humidity_ev, &temp_ev)) {
    tempDegC     = temp_ev.temperature;
    humidPercent = (int)humidity_ev.relative_humidity;

    // Rolling average — last 4 readings
    static float tempBuf[4] = {0};
    static int   tempIdx    = 0;
    tempBuf[tempIdx] = tempDegC;
    tempIdx = (tempIdx + 1) % 4;
    float sum = 0;
    for (int i = 0; i < 4; i++) sum += tempBuf[i];
    tempDegCavg = sum / 4.0f;
  } else {
    Serial.println("[SENSOR] AHT25 read failed");
  }

    // ----- Rain tipping bucket -----
  uint32_t tips;
  noInterrupts();
  tips = rainTipCountISR;
  rainTipCountISR = 0;   // clear interval count after reading
  interrupts();

  rainTipsInterval = tips;
  rainTipCountTotal += tips;

  float intervalSec = S_MEAS_CLK;
  if (intervalSec < 1.0f) intervalSec = 1.0f;

  rainTipsPerMin = (rainTipsInterval * 60.0f) / intervalSec;
  rainMmPerHour  = rainTipsPerMin * RAIN_MM_PER_TIP * 60.0f;

  // if at least one tip happened during the last measurement interval,
  // treat that as raining
  if (rainTipsInterval > 0) {
  lastRainActivityMs = millis();
}

isRaining = (millis() - lastRainActivityMs < 120000UL); // true for 2 min after last tip

  windLevel = measureWindLevel();

  long adcSum = 0;
  for (int i = 0; i < 8; i++) adcSum += analogRead(PIN_LIGHT_ADC);
  lightLevel = (int)(adcSum / 8);

  if (S_DEBUG) {
    Serial.printf("[SENSOR] T=%.1f H=%d Wind=%s RainTips=%lu Rate=%.2f tips/min Rain=%s Light=%d\n",
    tempDegC,
    humidPercent,
    windLevelStr[windLevel],
    (unsigned long)rainTipsInterval,
    rainTipsPerMin,
    isRaining ? "YES" : "NO",
    lightLevel);
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
//  WRITE BLE — push sensor data and current settings to app
// ============================================================
void writeBLE() {
  if (!bleConnected) return;
  char buf[24];

  snprintf(buf, sizeof(buf), "%.1f", tempDegC);
  pCharTemp->setValue(buf);   pCharTemp->notify();

  snprintf(buf, sizeof(buf), "%d", humidPercent);
  pCharHumid->setValue(buf);  pCharHumid->notify();

  snprintf(buf, sizeof(buf), "%s", windLevelStr[windLevel]);
  pCharWind->setValue(buf);   pCharWind->notify();

  snprintf(buf, sizeof(buf), "%.2f", rainMmPerHour);
  pCharRain->setValue(buf);  pCharRain->notify();

  snprintf(buf, sizeof(buf), "%d", lightLevel);
  pCharLight->setValue(buf);  pCharLight->notify();
}

// ============================================================
//  SYNC SETTINGS TO BLE
//  Called after any LCD menu edit so the app immediately
//  sees the updated values when it next reads a characteristic.
// ============================================================
void syncSettingsToBLE() {
  if (!bleConnected) return;
  char buf[24];
  // Settings
  snprintf(buf, sizeof(buf), "%.2f", S_TEMP_SP);   /* Temp Setpoint  */
  // (extend with setValue/notify calls once BLE char pointers
  //  are stored — placeholder for now, BLE writes already work
  //  in the opposite direction via SettingWriteCallback)
}

// ============================================================
//  AUTOMATIC CONTROL LOGIC
// ============================================================
void autoControl() {

  // ---- Window ----
  bool targetWindowOpen = windowIsOpen;

  if (C_WIN_OVR >= 1.0f) {
    targetWindowOpen = (C_WIN_SET >= 1.0f);
  } else {
    bool windTooHigh = ((int)windLevel >= (int)S_WIND_MAX);
    if (windTooHigh || isRaining) {
      targetWindowOpen = false;
    } else {
      if      (tempDegCavg > S_TEMP_SP + S_TEMP_HYS) targetWindowOpen = true;
      else if (tempDegCavg < S_TEMP_SP - S_TEMP_HYS) targetWindowOpen = false;

      if (!isRaining) {
        if      (humidPercent > S_HUMID_SP + S_HUMID_HYS) targetWindowOpen = true;
        else if (humidPercent < S_HUMID_SP - S_HUMID_HYS) targetWindowOpen = true;
      }
    }
  }
  if (targetWindowOpen != windowIsOpen) setWindow(targetWindowOpen);

  // ---- Heater (Relay1) ----
  bool heaterNeeded = (tempDegCavg < S_TEMP_SP - S_TEMP_HYS);
  if (heaterNeeded != relay1Set) {
    relay1Set = heaterNeeded;
    setRelay(PIN_RELAY1, relay1Set);
    if (S_DEBUG) Serial.printf("[CTRL] Heater %s\n", relay1Set ? "ON" : "OFF");
  }

  // ---- Cooler (Relay2) ----
  bool coolerNeeded = (tempDegCavg > S_TEMP_SP + S_TEMP_HYS);
  if (coolerNeeded != relay2Set) {
    relay2Set = coolerNeeded;
    setRelay(PIN_RELAY2, relay2Set);
    if (S_DEBUG) Serial.printf("[CTRL] Cooler %s\n", relay2Set ? "ON" : "OFF");
  }

  // ---- Lighting LED ----
  // S_LIGHT_MIN is 0-100%; map to ADC counts for comparison
  int adcThreshold = (int)(((long)S_LIGHT_MIN * ADC_MAX) / 100L);
  bool lightOn = (C_LIGHT_OVR >= 1.0f) ? (C_LIGHT_SET >= 1.0f) : (lightLevel < adcThreshold);
  digitalWrite(PIN_LED_LIGHT, lightOn ? HIGH : LOW);
}

// ============================================================
//  OUTPUT HELPERS
// ============================================================
void setWindow(bool open) {
  if (S_DEBUG) Serial.printf("[WINDOW] %s\n", open ? "OPENING" : "CLOSING");
  windowIsOpen = open;
  moveServoTo(open ? SERVO_WINDOW_OPEN : SERVO_WINDOW_CLOSED);
}

void moveServoTo(int targetDeg) {
  int step = (targetDeg > currentServoDeg) ? 1 : -1;
  while (currentServoDeg != targetDeg) {
    currentServoDeg += step;
    windowServo.write(currentServoDeg);
    delay(SERVO_SPEED_DELAY);
  }
}

void setRelay(int pin, bool state) {
  digitalWrite(pin, state ? LOW : HIGH);  // active LOW relay
}

// ============================================================
//  UTILITY
// ============================================================
String formatFloat(float val, int dp) {
  char buf[12];
  if      (dp == 1) snprintf(buf, sizeof(buf), "%.1f", val);
  else if (dp == 2) snprintf(buf, sizeof(buf), "%.2f", val);
  else              snprintf(buf, sizeof(buf), "%.0f", val);
  return String(buf);
}

// ============================================================
//  BACKLIGHT HELPER — call whenever encoder activity happens
// ============================================================
void touchBacklight() {
  if (!backlightOn) { lcd.backlight(); backlightOn = true; }
  backlightOnMs = millis();
}

void checkBacklightTimeout() {
  unsigned long timeout = (unsigned long)(S_BL_TMR * 60000.0f);
  if (backlightOn && (millis() - backlightOnMs >= timeout)) {
    lcd.noBacklight();
    backlightOn = false;
  }
}

// ============================================================
//  SETTING_CHANGE
//  Identical pattern to Carter's reference:
//    - shows Name, Default, Current on lines 0-2
//    - line 3 shows the live adjustment value
//    - turn encoder to step up/down, press to confirm
//  Works for both Settings[] and Controls[] arrays.
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
      if (digitalRead(PIN_ENC_DT) != curCLK) {
        // Counter-clockwise — decrease
        if (changeHold > arr[idx].Min) {
          changeHold -= arr[idx].Step;
          if (changeHold < arr[idx].Min) changeHold = arr[idx].Min;
        }
      } else {
        // Clockwise — increase
        if (changeHold < arr[idx].Max) {
          changeHold += arr[idx].Step;
          if (changeHold > arr[idx].Max) changeHold = arr[idx].Max;
        }
      }
      valueChanged = true;
    }
    lastStateCLK = curCLK;

    if (digitalRead(PIN_ENC_SW) == LOW) {
      touchBacklight();
      arr[idx].Value = changeHold;   // commit value
      changing = false;
      delay(250);                    // debounce
    }

  } while (changing);
}

// ============================================================
//  SETTINGS MENU  — scrollable list, press to edit each item
//  Last entry ("Exit") press returns to main menu.
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
        lcd.setCursor(1, j);
        lcd.print(Settings[j + dispTop].Name);
        // Show current value to the right on the same row
        lcd.setCursor(15, j);
        if (j + dispTop == SETTINGS_COUNT - 1) {
          lcd.print("EXIT ");  // Exit entry — no value
        } else if (Settings[j + dispTop].Max == 1.0f && Settings[j + dispTop].Min == 0.0f
                   && Settings[j + dispTop].Step == 1.0f) {
          // Boolean display
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
        // Up
        if (selection > 0) {
          selection--;
          if (localSel == 0) dispTop--;
          else                localSel--;
        }
      } else {
        // Down
        if (selection < SETTINGS_COUNT - 1) {
          selection++;
          if (localSel == LCD_ROWS - 1) dispTop++;
          else                           localSel++;
        }
      }
      needRedraw = (selection != selLast);
    }
    lastStateCLK = curCLK;

    if (digitalRead(PIN_ENC_SW) == LOW) {
      touchBacklight();
      delay(250);
      if (selection == SETTINGS_COUNT - 1) {
        run = false;  // Exit
      } else {
        Setting_Change(Settings, selection);
        needRedraw = true;  // Refresh menu to show updated value
      }
    }
  }
  delay(250);
}

// ============================================================
//  CONTROLS MENU  — window and light overrides
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
          else                localSel--;
        }
      } else {
        if (selection < CONTROLS_COUNT - 1) {
          selection++;
          if (localSel == LCD_ROWS - 1) dispTop++;
          else                           localSel++;
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
        needRedraw = true;
      }
    }
  }
  delay(250);
}

// ============================================================
//  MONITOR SCREEN  — live sensor data + control status
//  Runs sensor reads and BLE on the measurementClk interval.
//  Press encoder button to exit back to main menu.
// ============================================================
void Monitor_Screen() {
  bool run = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("____PAWS_MONITOR____");

  lastStateCLK = digitalRead(PIN_ENC_CLK);

  while (run) {
    checkBacklightTimeout();

    // Periodic measurement
    unsigned long now = millis();
    if (now - lastMeasurementMs >= (unsigned long)(S_MEAS_CLK * 1000.0f)) {
      lastMeasurementMs = now;
      readSensors();
      readBLE();
      writeBLE();
      autoControl();
    }

    // Refresh display
    lcd.setCursor(0, 1);
    lcd.print("T:"); lcd.print(formatFloat(tempDegC, 1)); lcd.print("C ");
    lcd.print("H:"); lcd.print(humidPercent);              lcd.print("%   ");

    lcd.setCursor(0, 2);
    lcd.print("Wind:"); lcd.print(windLevelStr[windLevel]); lcd.print("        ");

    lcd.setCursor(0, 3);
    lcd.print(isRaining ? "Rain:YES " : "Rain:NO  ");
    // Show heater/cooler/window state on right side of row 3
    lcd.setCursor(9, 3);
    lcd.print(relay1Set ? "HT " : "   ");
    lcd.print(relay2Set ? "CL " : "   ");
    lcd.print(windowIsOpen ? "WIN" : "   ");

    // Show BLE status top-right on row 0
    lcd.setCursor(17, 0);
    lcd.print(bleConnected ? "BLE" : "   ");

    // Debug: show raw light level on row 2 right
    if (S_DEBUG) {
      lcd.setCursor(13, 2);
      lcd.print("L:"); lcd.print(lightLevel);
    }

    if (digitalRead(PIN_ENC_SW) == LOW) {
      touchBacklight();
      run = false;
      delay(250);
    }
    delay(200);  // ~5 Hz display refresh
  }
}

// ============================================================
//  MAIN MENU  — three entries: Monitor, Settings, Controls
//  Scrollable with encoder, press to enter, auto-returns here
//  after any sub-screen exits.
// ============================================================
void Menu_Select() {
  const char* mainMenu[]  = { "Monitor", "Settings", "Controls" };
  const uint8_t MENU_LEN  = 3;

  uint8_t selection  = 0;
  int8_t  selLast    = -1;
  uint8_t localSel   = 0;
  uint8_t dispTop    = 0;
  bool    run        = true;

  lastStateCLK = digitalRead(PIN_ENC_CLK);
  lcd.clear();

  while (run) {
    checkBacklightTimeout();

    if (selection != selLast) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("_____PAWS_MENU______");
      for (int i = 0; i < LCD_ROWS - 1; i++) {       // rows 1-3 for items
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
          else                localSel--;
        }
      } else {
        if (selection < MENU_LEN - 1) {
          selection++;
          if (localSel == LCD_ROWS - 2) dispTop++;
          else                           localSel++;
        }
      }
    }
    lastStateCLK = curCLK;

    if (digitalRead(PIN_ENC_SW) == LOW) {
      touchBacklight();
      delay(250);
      run = false;   // exit loop, then dispatch below
    }
  }

  delay(250);
  lcd.clear();

  switch (selection) {
    case 0: Monitor_Screen();  break;
    case 1: Settings_Menu();   break;
    case 2: Controls_Menu();   break;
  }
  // Returns here → loop() calls Menu_Select() again automatically
}

