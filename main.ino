#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include "telegram_bot.h"

const int PIN_MQ135    = 34;
const int PIN_SOIL     = 32;
const int PIN_LDR_DO   = 33;
const int PIN_LED      = 2;
const int PIN_PUMP     = 25;

const long SERIAL_LOG_INTERVAL = 60000;
const long SENSOR_READ_DELAY   = 5000; 
const int GMT_OFFSET_SEC       = 2 * 3600; // GMT+2 
const int DST_OFFSET_SEC       = 0; 

Adafruit_AHTX0 aht;
LiquidCrystal_I2C lcd(0x27, 20, 4);
unsigned long timerSensors = 0;
unsigned long timerSerial = 0;

struct SensorData {
  float temp = 0.0;
  float hum = 0.0;
  int soilRaw = 0;
  int soilPct = 0;
  int mq135 = 0;
  bool isDark = false;
  bool ahtReady = false;
} sensors;

bool pumpState = false;
bool autoMode = true;
int soilThreshold = 50;
uint8_t LT[8]  = {0x07, 0x0F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
uint8_t UB[8]  = {0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t RT[8]  = {0x1C, 0x1E, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
uint8_t LL[8]  = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x0F, 0x07};
uint8_t LB[8]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F, 0x1F};
uint8_t LR[8]  = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1E, 0x1C};
uint8_t UMB[8] = {0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x1F, 0x1F};
uint8_t LMB[8] = {0x1F, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F, 0x1F};

const uint8_t bigDigits[10][2][3] = {
  { {0, 1, 2}, {3, 4, 5} },   // 0
  { {1, 2, 32}, {4, 5, 4} },  // 1
  { {6, 6, 2}, {3, 4, 4} },   // 2
  { {6, 6, 2}, {4, 4, 5} },   // 3
  { {3, 4, 5}, {32, 32, 5} }, // 4
  { {0, 6, 6}, {4, 4, 5} },   // 5
  { {0, 6, 6}, {3, 4, 5} },   // 6
  { {0, 1, 2}, {32, 32, 5} }, // 7
  { {0, 6, 2}, {3, 7, 5} },   // 8
  { {0, 6, 2}, {32, 32, 5} }  // 9
};

enum ScreenMode { SCREEN_CLOCK, SCREEN_STATUS };
ScreenMode currentScreen = SCREEN_CLOCK;

void updateSensors();
void controlPump(bool state);
void runAutoLogic();
void renderLCD();
void renderBigTime(int h, int m);
void drawDigit(int col, int num);
int mapSoilToPercent(int raw);

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LDR_DO, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_PUMP, OUTPUT);
  controlPump(false); 


  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  
  uint8_t* chars[] = {LT, UB, RT, LL, LB, LR, UMB, LMB};
  for (int i = 0; i < 8; i++) lcd.createChar(i, chars[i]);

  if (aht.begin()) {
    sensors.ahtReady = true;
    Serial.println(F("AHT20 connected."));
  } else {
    Serial.println(F("Error: AHT20 not found!"));
  }

  setupWiFiAndBot(); 
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.google.com");
  
  Serial.println(F("System Started"));
}

void loop() {
  unsigned long now = millis();

  if (now - timerSensors > SENSOR_READ_DELAY) {
    timerSensors = now;
    updateSensors();
    runAutoLogic();
    renderLCD();
  }

  if (now - timerSerial > SERIAL_LOG_INTERVAL) {
    timerSerial = now;
    Serial.printf("[REPORT] T:%.1fC H:%.1f%% Soil:%d%%(%d) Air:%d Light:%s Pump:%d\n", 
      sensors.temp, sensors.hum, sensors.soilPct, sensors.soilRaw, 
      sensors.mq135, sensors.isDark ? "OFF" : "ON", pumpState);
  }
  handleTelegram(); 
  delay(10);
}
void updateSensors() {
  if (sensors.ahtReady) {
    sensors_event_t h, t;
    aht.getEvent(&h, &t);
    sensors.temp = t.temperature;
    sensors.hum = h.relative_humidity;
  }

  sensors.mq135 = analogRead(PIN_MQ135);
  sensors.soilRaw = analogRead(PIN_SOIL);
  sensors.soilPct = mapSoilToPercent(sensors.soilRaw);
  
  sensors.isDark = (digitalRead(PIN_LDR_DO) == LOW);

  digitalWrite(PIN_LED, sensors.isDark ? LOW : HIGH);
}

int mapSoilToPercent(int raw) {
  const int DRY_VAL = 4095;
  const int WET_VAL = 0; 
  
  int p = map(raw, DRY_VAL, WET_VAL, 0, 100);
  return constrain(p, 0, 100);
}

void controlPump(bool state) {
  pumpState = state;
  digitalWrite(PIN_PUMP, state ? LOW : HIGH); 
}

void runAutoLogic() {
  if (!autoMode) return;
  if (sensors.soilPct < soilThreshold && !pumpState) {
    Serial.println(F("Auto: Soil dry -> Pump ON"));
    controlPump(true);
  } else if (sensors.soilPct >= (soilThreshold + 5) && pumpState) {
    Serial.println(F("Auto: Soil wet -> Pump OFF"));
    controlPump(false);
  }
}

void renderLCD() {
  lcd.clear();
  
  if (currentScreen == SCREEN_CLOCK) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      lcd.setCursor(0, 1);
      lcd.print("Waiting for NTP...");
      return;
    }
    
    renderBigTime(timeinfo.tm_hour, timeinfo.tm_min);
    lcd.setCursor(0, 3);
    if (sensors.ahtReady) {
      lcd.printf("T:%.0fC ", sensors.temp);
    } else {
      lcd.print("Err ");
    }
    lcd.printf("Soil:%d%%", sensors.soilPct);

  } else {
    lcd.setCursor(0, 0); lcd.print("--- SYSTEM STATUS ---");
    lcd.setCursor(0, 1); lcd.printf("Mode: %s", autoMode ? "AUTO" : "MANUAL");
    lcd.setCursor(0, 2); lcd.printf("Pump: %s", pumpState ? "ON" : "OFF");
    lcd.setCursor(0, 3); lcd.printf("Thresh: %d%%", soilThreshold);
  }
}

void renderBigTime(int h, int m) {
  int startCol = 1; 
  
  drawDigit(startCol, h / 10);
  drawDigit(startCol + 4, h % 10);
  
  // Colon
  lcd.setCursor(startCol + 7, 0); lcd.print(" ");
  lcd.setCursor(startCol + 7, 1); lcd.print(":");
  
  drawDigit(startCol + 9, m / 10);
  drawDigit(startCol + 13, m % 10);
}

void drawDigit(int col, int num) {
  for (int row = 0; row < 2; row++) {
    for (int i = 0; i < 3; i++) {
      lcd.setCursor(col + i, row);
      uint8_t charIndex = bigDigits[num][row][i];
      if (charIndex == 32) lcd.print(" ");
      else lcd.write(charIndex);
    }
  }
}

float getAHTTemp()     { return sensors.temp; }
float getAHTHum()      { return sensors.hum; }
int getMQ135()         { return sensors.mq135; }
int getSoil()          { return sensors.soilRaw; }
int getSoilPercent()   { return sensors.soilPct; } 
bool isDark()          { return sensors.isDark; }
bool getPumpState()    { return pumpState; }
bool isAutoMode()      { return autoMode; }

void changePumpFromBot(bool on, String user, String id) {
  autoMode = false; 
  controlPump(on);
  Serial.printf("Bot: Pump %s by %s\n", on ? "ON" : "OFF", user.c_str());
}

void setAutoModeFromBot(bool on, String user, String id) {
  autoMode = on;
  Serial.printf("Bot: AutoMode %s by %s\n", on ? "ON" : "OFF", user.c_str());
  if (on) runAutoLogic(); 
}

void setHumThreshold(int val) {
  soilThreshold = constrain(val, 0, 100);
  Serial.printf("Bot: Threshold set to %d%%\n", soilThreshold);
}
