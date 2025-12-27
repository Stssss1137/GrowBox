#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include "telegram_bot.h"

const int PIN_MQ135    = 34;
const int PIN_SOIL     = 32;
const int PIN_LDR_DO   = 33; 
const int PIN_LED      = 18; 
const int PIN_PUMP     = 25; 

const long SERIAL_LOG_INTERVAL = 60000;
const long SENSOR_READ_DELAY   = 2000; 
const int GMT_OFFSET_SEC       = 2 * 3600; 
const int DST_OFFSET_SEC       = 0; 

const unsigned long PUMP_DURATION = 5000;
const unsigned long PUMP_COOLDOWN = 20000;

Adafruit_AHTX0 aht;
LiquidCrystal_I2C lcd(0x27, 20, 4);

unsigned long timerSensors = 0;
unsigned long timerSerial = 0;
unsigned long pumpStartTime = 0;
unsigned long lastPumpRunTime = 0;

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
int lightDurationHours = 14; 
String currentModeName = "Custom";
bool lastLedState = false; 

uint8_t LT[8] = {0x07,0x0F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};
uint8_t UB[8] = {0x1F,0x1F,0x1F,0x00,0x00,0x00,0x00,0x00};
uint8_t RT[8] = {0x1C,0x1E,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};
uint8_t LL[8] = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x0F,0x07};
uint8_t LB[8] = {0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F};
uint8_t LR[8] = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1E,0x1C};
uint8_t UMB[8]={0x1F,0x1F,0x1F,0x00,0x00,0x00,0x1F,0x1F};
uint8_t LMB[8]={0x1F,0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F};

const uint8_t bigDigits[10][2][3] = {
  {{0,1,2}, {3,4,5}}, {{1,2,32}, {4,5,4}}, {{6,6,2}, {3,4,4}}, {{6,6,2}, {4,4,5}},
  {{3,4,5}, {32,32,5}}, {{0,6,6}, {4,4,5}}, {{0,6,6}, {3,4,5}}, {{0,1,2}, {32,32,5}},
  {{0,6,2}, {3,7,5}}, {{0,6,2}, {32,32,5}}
};

enum ScreenMode { SCREEN_CLOCK, SCREEN_STATUS };
ScreenMode currentScreen = SCREEN_CLOCK;

void updateSensors();
void checkPumpTimer();
void runAutoLogic();
void renderLCD();
void renderBigTime(int h, int m);
void drawDigit(int col, int num);
int mapSoilToPercent(int raw);
void turnPumpOn(bool isManual);
void turnPumpOff();

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LDR_DO, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_PUMP, OUTPUT); 
  digitalWrite(PIN_PUMP, HIGH);
  digitalWrite(PIN_LED, LOW);
  pumpState = false;
  lastPumpRunTime = millis() - PUMP_COOLDOWN - 1000;

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  
  uint8_t* chars[] = {LT, UB, RT, LL, LB, LR, UMB, LMB};
  for (int i = 0; i < 8; i++) lcd.createChar(i, chars[i]);

  if (aht.begin()) {
    sensors.ahtReady = true;
    Serial.println(F("AHT20 OK"));
  } else {
    Serial.println(F("AHT20 Error"));
  }

  setupWiFiAndBot(); 
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.google.com");
  
  Serial.println(F(">>> System Ready <<<"));
}

void loop() {
  unsigned long now = millis();

  checkPumpTimer();

  if (now - timerSensors > SENSOR_READ_DELAY) {
    timerSensors = now;
    updateSensors();
    runAutoLogic();
    renderLCD();
  }

  if (now - timerSerial > SERIAL_LOG_INTERVAL) {
    timerSerial = now;
    Serial.printf("[LOG] Mode:%s Soil:%d%% Pump:%d LED:%d Dark:%d\n", 
      autoMode ? "AUTO" : "MANUAL", sensors.soilPct, pumpState, lastLedState, sensors.isDark);
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
  sensors.isDark = (digitalRead(PIN_LDR_DO) == HIGH);
}

int mapSoilToPercent(int raw) {
  return constrain(map(raw, 4095, 1500, 0, 100), 0, 100);
}

void turnPumpOn(bool isManual) {
  pumpState = true;
  pumpStartTime = millis(); 
  digitalWrite(PIN_PUMP, LOW);
  if (isManual) Serial.println(F(">>> MANUAL PUMP"));
  else Serial.println(F(">>> AUTO PUMP"));
}

void turnPumpOff() {
  if (pumpState) {
    pumpState = false;
    lastPumpRunTime = millis();
    digitalWrite(PIN_PUMP, HIGH);
    Serial.println(F(">>> PUMP STOP"));
  }
}

void checkPumpTimer() {
  if (pumpState && (millis() - pumpStartTime >= PUMP_DURATION)) {
    turnPumpOff();
  }
}

void runAutoLogic() {
  if (!autoMode) return; 

  struct tm timeinfo;
  bool timeSynced = getLocalTime(&timeinfo);
  if (timeSynced) {
    int startHour = 6; 
    int endHour = startHour + lightDurationHours;
    
    bool isDaySchedule = (timeinfo.tm_hour >= startHour && timeinfo.tm_hour < endHour);
    
    bool shouldLedBeOn = isDaySchedule && sensors.isDark;
    digitalWrite(PIN_LED, shouldLedBeOn ? HIGH : LOW);

    if (shouldLedBeOn != lastLedState) {
      if (shouldLedBeOn) {
        sendBotMessage("🌑 Сонце сіло (темно). Освітлення УВІМКНЕНО 💡");
      } else {
        if (!isDaySchedule) {
          sendBotMessage("🌙 Час підтримання освітленості вийшов. Освітлення ВИМКНЕНО 😴");
        } else {
          sendBotMessage("☀️ Зійшло сонце (світло). Освітлення ВИМКНЕНО 🌤");
        }
      }
      lastLedState = shouldLedBeOn;
    }
  }
  if (sensors.soilPct < soilThreshold && !pumpState) {
    if (millis() - lastPumpRunTime > PUMP_COOLDOWN) {
       Serial.println(F("Auto: Soil dry. Pump ON."));
       turnPumpOn(false);
    }
  }
}
void renderLCD() {
  lcd.clear();
  if (currentScreen == SCREEN_CLOCK) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) { lcd.print("WiFi connecting..."); return; }
    renderBigTime(timeinfo.tm_hour, timeinfo.tm_min);
    lcd.setCursor(0,3);
    lcd.printf("T:%.0f S:%d%% %s", sensors.temp, sensors.soilPct, autoMode?"A":"M");
  } else {
    lcd.setCursor(0,0); lcd.printf("Mode: %s", currentModeName.c_str());
    lcd.setCursor(0,1); lcd.printf("Soil:%d%% Thr:%d%%", sensors.soilPct, soilThreshold);
    lcd.setCursor(0,2); lcd.printf("LED:%s Dark:%s", lastLedState?"ON":"OFF", sensors.isDark?"Y":"N");
    lcd.setCursor(0,3); lcd.printf("Pump: %s", pumpState?"ON":"OFF");
  }
}
void renderBigTime(int h, int m) {
  int startCol = 1; 
  drawDigit(startCol, h/10); drawDigit(startCol+4, h%10);
  lcd.setCursor(startCol+7,0); lcd.print(" "); lcd.setCursor(startCol+7,1); lcd.print(":");
  drawDigit(startCol+9, m/10); drawDigit(startCol+13, m%10);
}
void drawDigit(int col, int num) {
  for (int row=0; row<2; row++) {
    for (int i=0; i<3; i++) {
      lcd.setCursor(col+i, row);
      uint8_t c = bigDigits[num][row][i];
      if(c==32) lcd.print(" "); else lcd.write(c);
    }
  }
}
float getAHTTemp()     { return sensors.temp; }
float getAHTHum()      { return sensors.hum; }
int getMQ135()         { return sensors.mq135; }
int getSoilPercent()   { return sensors.soilPct; } 
bool isDark()          { return sensors.isDark; }
bool getPumpState()    { return pumpState; }
bool isAutoMode()      { return autoMode; }
int getSoilThreshold() { return soilThreshold; }
int getLightDuration() { return lightDurationHours; }
String getModeName()   { return currentModeName; }
bool getLedState()     { return lastLedState; }

void setPlantMode(int mode) {
  autoMode = true;
  switch (mode) {
    case 1: currentModeName="Tropical"; soilThreshold=60; lightDurationHours=12; break;
    case 2: currentModeName="Succulent"; soilThreshold=20; lightDurationHours=14; break;
    case 3: currentModeName="Vegetable"; soilThreshold=45; lightDurationHours=16; break;
    default: currentModeName="Custom"; break;
  }
}
void manualPumpControl(bool on) {
  autoMode = false; 
  currentModeName = "Manual";
  if (on) turnPumpOn(true); else turnPumpOff();
}
void setCustomThreshold(int val) {
  autoMode = true;
  currentModeName = "Custom";
  soilThreshold = constrain(val, 0, 100);
}
