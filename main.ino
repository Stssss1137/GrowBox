#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <LiquidCrystal_I2C.h>
#include <FastLED.h>
#include <FirebaseESP32.h>
#include "telegram_bot.h"

// ПІДКЛЮЧЕННЯ
#define PIN_SOIL     32
#define PIN_LDR_DO   33
#define PIN_MQ135    34  
#define PIN_WS2812   18
#define PIN_PUMP     25
#define PIN_FAN      26
#define NUM_LEDS     8

// ДАНІ FIREBASE і WIFI 
#define WIFI_SSID "XXXXXXXX"
#define WIFI_PASS "XXXXXXXX"
#define FIREBASE_HOST "XXXXXXXXXXXXXXXXXXXXXXXX.firebaseio.com"
#define FIREBASE_AUTH "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"

FirebaseData fbData;
FirebaseAuth fbAuth;
FirebaseConfig fbConfig;
CRGB leds[NUM_LEDS];
Adafruit_AHTX0 aht;
LiquidCrystal_I2C lcd(0x27, 20, 4);

unsigned long lastUpdate = 0;
float temp = 0.0, hum = 0.0;
int soilPct = 0;
int airQuality = 0; 
bool isDark = false;
bool soilError = false;
bool ahtError = false;
bool lastSoilErrorState = false;
unsigned long pumpStartTime = 0;
unsigned long lastPumpRunTime = 0;
const unsigned long PUMP_DURATION = 4000;  
const unsigned long PUMP_COOLDOWN = 20000;

String currentMode = "AUTO";
bool lightOn = false;
String spectrum = "full";
bool pumpOn = false;
bool fanOn = false;
String currentClimate = "temperate";
String currentStage = "veg";
int soilTarget = 40;
int humTarget = 55;
int tempTarget = 24;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LDR_DO, INPUT);
  pinMode(PIN_MQ135, INPUT);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_FAN, OUTPUT);
  digitalWrite(PIN_PUMP, LOW);
  digitalWrite(PIN_FAN, LOW);
  delay(500);

  Wire.begin(21, 22);
  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("--- SYSTEM BOOT ---");
  delay(1000);

  lcd.setCursor(0, 1); lcd.print("1. Sensors: ");
  if (aht.begin()) lcd.print("OK"); else lcd.print("ERR!");
  delay(1000);

  lcd.setCursor(0, 2); lcd.print("2. LED: OK");
  FastLED.addLeds<WS2812, PIN_WS2812, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(100);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  delay(1000);

  lcd.setCursor(0, 3); lcd.print("3. WiFi: ");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) { delay(500); wifiAttempts++; }
  if (WiFi.status() == WL_CONNECTED) lcd.print("OK"); else lcd.print("ERR");
  delay(1000);
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("4. NTP: ");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  int timeAttempts = 0;
  while (now < 8 * 3600 * 2 && timeAttempts < 15) { delay(500); now = time(nullptr); timeAttempts++; }
  lcd.print((now > 100000) ? "OK" : "SKIP");
  delay(1000);
  lcd.setCursor(0, 1); lcd.print("5. Cloud: INIT...");
  setupTelegram();
  fbConfig.host = FIREBASE_HOST;
  fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);
  lcd.setCursor(10, 1); lcd.print("OK     ");
  delay(1500);
  lcd.clear(); lcd.setCursor(0, 1); lcd.print(" >> SYSTEM READY << ");
  delay(2000); lcd.clear();
}

void loop() {
  handleTelegram();

  if (pumpOn && (millis() - pumpStartTime >= PUMP_DURATION)) {
    pumpOn = false;
    lastPumpRunTime = millis();
    Firebase.setBool(fbData, "/growbox/controls/pumpOn", false);
  }
  if (millis() - lastUpdate > 3000) {
    lastUpdate = millis();
    readSensors();
    checkSensorErrors(); 
    updateFirebaseSensors();
    getFirebaseControls();
    applyControls();
    updateLCD();
  }
}

void readSensors() {
  sensors_event_t h, t;
  if(aht.getEvent(&h, &t)) { 
    temp = t.temperature; hum = h.relative_humidity; 
    ahtError = false;
  } else {
    temp = -99; hum = -99; ahtError = true;
  }
  int rawSoil = analogRead(PIN_SOIL);
  if (rawSoil >= 4090) { 
    soilPct = -1; soilError = true;
  } else {
    soilPct = constrain(map(rawSoil, 4095, 1500, 0, 100), 0, 100);
    soilError = false;
  }

  airQuality = constrain(map(analogRead(PIN_MQ135), 0, 4095, 0, 100), 0, 100);
  isDark = (digitalRead(PIN_LDR_DO) == HIGH);
}

void checkSensorErrors() {
  if (soilError && !lastSoilErrorState) {
    sendBotMessage("Датчик ґрунту ВІДКЛЮЧЕНО! Полив заблоковано");
    lastSoilErrorState = true;
    if (pumpOn) { pumpOn = false; Firebase.setBool(fbData, "/growbox/controls/pumpOn", false); }
  } else if (!soilError && lastSoilErrorState) {
    sendBotMessage(" Датчик ґрунту відновлено");
    lastSoilErrorState = false;
  }
}

void updateFirebaseSensors() {
  Firebase.setFloat(fbData, "/growbox/sensors/temp", temp);
  Firebase.setFloat(fbData, "/growbox/sensors/hum", hum);
  Firebase.setInt(fbData, "/growbox/sensors/soil", soilPct);
  Firebase.setInt(fbData, "/growbox/sensors/air", airQuality);
}

void getFirebaseControls() {
  if (Firebase.getString(fbData, "/growbox/controls/mode")) currentMode = fbData.stringData();
  if (Firebase.getBool(fbData, "/growbox/controls/lightOn")) lightOn = fbData.boolData();
  if (Firebase.getString(fbData, "/growbox/controls/spectrum")) spectrum = fbData.stringData();
  
  bool requestedPump = false;
  if (Firebase.getBool(fbData, "/growbox/controls/pumpOn")) requestedPump = fbData.boolData();
  
  if (requestedPump && !pumpOn && !soilError) {
    pumpOn = true; pumpStartTime = millis(); 
  } else if (!requestedPump && pumpOn) {
    pumpOn = false;
  }
  if (Firebase.getBool(fbData, "/growbox/controls/fanOn")) fanOn = fbData.boolData();
  if (Firebase.getInt(fbData, "/growbox/controls/soilTarget")) soilTarget = fbData.intData();
  if (Firebase.getInt(fbData, "/growbox/controls/humTarget")) humTarget = fbData.intData();
  if (Firebase.getString(fbData, "/growbox/controls/climate")) currentClimate = fbData.stringData();
  if (Firebase.getString(fbData, "/growbox/controls/stage")) currentStage = fbData.stringData();
}

void applyControls() {
  if (currentMode == "AUTO") {
    if (!soilError) {
      if (!pumpOn && soilPct < soilTarget && (millis() - lastPumpRunTime > PUMP_COOLDOWN)) {
        pumpOn = true; pumpStartTime = millis();
        Firebase.setBool(fbData, "/growbox/controls/pumpOn", true);
        sendBotMessage(" Авто-полив");
      }
    } else {
      pumpOn = false; 
    }
    
    // Вентиляція
    if (!ahtError && ((hum > humTarget + 5) || airQuality > 40) && !fanOn) {
      fanOn = true; Firebase.setBool(fbData, "/growbox/controls/fanOn", true);
    } else if (hum < humTarget && airQuality <= 35 && fanOn) {
      fanOn = false; Firebase.setBool(fbData, "/growbox/controls/fanOn", false);
    }

    // Світло
    if (isDark && !lightOn) {
      lightOn = true; Firebase.setBool(fbData, "/growbox/controls/lightOn", true);
    } else if (!isDark && lightOn) {
      lightOn = false; Firebase.setBool(fbData, "/growbox/controls/lightOn", false);
    }
  }

  digitalWrite(PIN_PUMP, pumpOn ? HIGH : LOW);

  if (pumpOn) {
    digitalWrite(PIN_FAN, LOW); 
    FastLED.setBrightness(20);
  } else {
    digitalWrite(PIN_FAN, fanOn ? HIGH : LOW);
    FastLED.setBrightness(100);
  }

  if (!lightOn) { fill_solid(leds, NUM_LEDS, CRGB::Black); } 
  else {
    if (spectrum == "blue") fill_solid(leds, NUM_LEDS, CRGB(0, 0, 255));
    else if (spectrum == "red") fill_solid(leds, NUM_LEDS, CRGB(255, 0, 0));
    else fill_solid(leds, NUM_LEDS, CRGB(255, 0, 255)); 
  }
  FastLED.show();
}

void updateLCD() {
  lcd.setCursor(0,0); lcd.printf("T:%.1fC H:%.0f%%  ", temp, hum);
  lcd.setCursor(0,1);
  if (soilError) lcd.print("Soil: ERR! Air:"); else lcd.printf("Soil:%d%% Air:", soilPct);
  lcd.printf("%d%% ", airQuality);
  lcd.setCursor(0,2); lcd.printf("Mode: %s   ", currentMode.c_str());
  lcd.setCursor(0,3);
  if (pumpOn) {
    lcd.printf("P:1 F:0(ECO) L:D   ");
  } else {
    lcd.printf("P:0 F:%d L:%d      ", fanOn, lightOn);
  }
}
