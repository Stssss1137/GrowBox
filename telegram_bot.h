#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <FirebaseESP32.h>

extern float temp, hum;
extern int soilPct, soilTarget, humTarget, tempTarget, airQuality;
extern bool pumpOn, fanOn, lightOn, soilError, ahtError;
extern unsigned long pumpStartTime; 
extern String currentMode, spectrum, currentClimate, currentStage;
extern FirebaseData fbData;

#define BOT_TOKEN "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
String storedChatId = "";

void setupTelegram() { secured_client.setInsecure(); }
void sendBotMessage(String msg) { if (storedChatId != "") bot.sendMessage(storedChatId, msg); }

void setFirebaseState(String path, String val) { Firebase.setString(fbData, path, val); }
void setFirebaseState(String path, bool val) { Firebase.setBool(fbData, path, val); }
void setFirebaseState(String path, int val) { Firebase.setInt(fbData, path, val); }

void applyClimateChange(String c, String s) {
  int tTemp = 24, tHum = 55, tSoil = 40;
  String spec = "full";

  if (c == "temperate") {
    tTemp = 24; tHum = 55;
    if (s == "seedling") { tSoil = 60; spec = "blue"; }
    else if (s == "veg") { tSoil = 40; spec = "full"; }
    else if (s == "flower") { tSoil = 30; spec = "red"; }
  } 
  else if (c == "tropical") {
    tTemp = 28; tHum = 80;
    if (s == "seedling") { tSoil = 75; spec = "blue"; }
    else if (s == "veg") { tSoil = 60; spec = "full"; }
    else if (s == "flower") { tSoil = 50; spec = "red"; }
  }
  else if (c == "arid") {
    tTemp = 32; tHum = 25;
    if (s == "seedling") { tSoil = 30; spec = "blue"; }
    else if (s == "veg") { tSoil = 15; spec = "full"; }
    else if (s == "flower") { tSoil = 10; spec = "full"; }
  }
  else if (c == "mediterranean") {
    tTemp = 26; tHum = 45;
    if (s == "seedling") { tSoil = 50; spec = "blue"; }
    else if (s == "veg") { tSoil = 30; spec = "full"; }
    else if (s == "flower") { tSoil = 20; spec = "red"; }
  }

  currentClimate = c; currentStage = s;
  tempTarget = tTemp; humTarget = tHum; soilTarget = tSoil; spectrum = spec;
  currentMode = "AUTO";

  setFirebaseState("/growbox/controls/climate", c);
  setFirebaseState("/growbox/controls/stage", s);
  setFirebaseState("/growbox/controls/tempTarget", tTemp);
  setFirebaseState("/growbox/controls/humTarget", tHum);
  setFirebaseState("/growbox/controls/soilTarget", tSoil);
  setFirebaseState("/growbox/controls/spectrum", spec);
  setFirebaseState("/growbox/controls/mode", "AUTO");
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    storedChatId = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;

    if (text.startsWith("/start") || text.startsWith("/help")) {
      String helpMsg = "Команди для керування боксом:\n\n";
      helpMsg += "📊 /status - Поточний стан \n";
      helpMsg += " /auto - АВТО режим\n";
      helpMsg += " /manual - РУЧНЕ керування\n\n";
      
      helpMsg += "🌍 КЛІМАТИЧНІ ПОЯСИ:\n";
      helpMsg += "🌳 /c_temp - Помірний\n";
      helpMsg += "🌴 /c_trop - Тропічний\n";
      helpMsg += "🌵 /c_arid - Пустельний\n";
      helpMsg += "🌿 /c_med - Середземноморський\n\n";
      
      helpMsg += "🌱 СТАДІЯ РОСТУ:\n";
      helpMsg += "1️⃣ /s_seed - Розсада\n";
      helpMsg += "2️⃣ /s_veg - Вегетація\n";
      helpMsg += "3️⃣ /s_flow - Цвітіння\n\n";

      helpMsg += " РУЧНЕ УПРАВЛІННЯ:\n";
      helpMsg += " /pump_on | /pump_off (Полив) \n";
      helpMsg += " /fan_on | /fan_off (Вентиляція)\n";
      helpMsg += " /light_blue | /light_red | /light_full | /light_off";
      
      bot.sendMessage(storedChatId, helpMsg); 
    }
    else if (text.startsWith("/status")) {
      String m = "📊 СТАТУС СИСТЕМИ\n";
      m += "Режим: " + currentMode + "\n";
      m += "Клімат: " + currentClimate + " (Стадія: " + currentStage + ")\n\n";
      
      if (ahtError) m += "⚠️ Темп/Волога: ПОМИЛКА ДАТЧИКА!\n";
      else m += "🌡 Темп: " + String(temp,1) + "C | Волога: " + String(hum,0) + "%\n";
      
      if (soilError) m += "⚠️ ДАТЧИК гігрометра ВІДКЛЮЧЕНО!\n";
      else m += "🌱 Ґрунт: " + String(soilPct) + "% (Ціль: " + String(soilTarget) + "%)\n";
      
      m += "Чистота повітря: " + String(airQuality) + "%\n";
      m += "Полив: " + String(pumpOn?"ПРАЦЮЄ":"ВИМК") + "\n";
      m += "Вентиляція.: " + String(fanOn?"ПРАЦЮЄ":"ВИМК") + "\n";
      m += "Світло: " + String(lightOn?"УВІМК ("+spectrum+")":"ВИМК");
      bot.sendMessage(storedChatId, m);
    }
    

    else if (text.startsWith("/c_temp")) { applyClimateChange("temperate", currentStage); bot.sendMessage(storedChatId, "🌳 Клімат: Помірний"); }
    else if (text.startsWith("/c_trop")) { applyClimateChange("tropical", currentStage); bot.sendMessage(storedChatId, "🌴 Клімат: Тропіки"); }
    else if (text.startsWith("/c_arid")) { applyClimateChange("arid", currentStage); bot.sendMessage(storedChatId, "🌵 Клімат: Пустеля"); }
    else if (text.startsWith("/c_med")) { applyClimateChange("mediterranean", currentStage); bot.sendMessage(storedChatId, "🌿 Клімат: Середземноморський"); }
    

    else if (text.startsWith("/s_seed")) { applyClimateChange(currentClimate, "seedling"); bot.sendMessage(storedChatId, "🌱 Стадія: Розсада"); }
    else if (text.startsWith("/s_veg")) { applyClimateChange(currentClimate, "veg"); bot.sendMessage(storedChatId, "🌿 Стадія: Вегетація"); }
    else if (text.startsWith("/s_flow")) { applyClimateChange(currentClimate, "flower"); bot.sendMessage(storedChatId, "🌸 Стадія: Цвітіння"); }


    else if (text.startsWith("/auto")) { currentMode = "AUTO"; setFirebaseState("/growbox/controls/mode", "AUTO"); bot.sendMessage(storedChatId, " АВТО режим увімкнено"); }
    else if (text.startsWith("/manual")) { currentMode = "MANUAL"; setFirebaseState("/growbox/controls/mode", "MANUAL"); bot.sendMessage(storedChatId, " РУЧНИЙ режим увімкнено"); }

    else if (text.startsWith("/pump_on")) { 
      if (soilError) {
        bot.sendMessage(storedChatId, "⛔ Помилка! Датчик ґрунту відключено. Полив заблоковано!");
      } else {
        setFirebaseState("/growbox/controls/mode", "MANUAL"); 
        pumpStartTime = millis(); 
        setFirebaseState("/growbox/controls/pumpOn", true); 
        bot.sendMessage(storedChatId, " Помпа увімкнена (На 4 секунди)"); 
      }
    }
    else if (text.startsWith("/pump_off")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/pumpOn", false); bot.sendMessage(storedChatId, "🛑 Помпа вимкнена"); }
    else if (text.startsWith("/fan_on")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/fanOn", true); bot.sendMessage(storedChatId, "💨 Вентилятор увімкнено"); }
    else if (text.startsWith("/fan_off")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/fanOn", false); bot.sendMessage(storedChatId, "🛑 Вентилятор вимкнено"); }
    else if (text.startsWith("/light_blue")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/lightOn", true); setFirebaseState("/growbox/controls/spectrum", "blue"); bot.sendMessage(storedChatId, "🔵 СИНІЙ спектр"); }
    else if (text.startsWith("/light_red")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/lightOn", true); setFirebaseState("/growbox/controls/spectrum", "red"); bot.sendMessage(storedChatId, "🔴 ЧЕРВОНИЙ спектр"); }
    else if (text.startsWith("/light_full")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/lightOn", true); setFirebaseState("/growbox/controls/spectrum", "full"); bot.sendMessage(storedChatId, "🟣 ПОВНИЙ спектр"); }
    else if (text.startsWith("/light_off")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/lightOn", false); bot.sendMessage(storedChatId, "🌑 Світло вимкнено"); }
  }
}

void handleTelegram() {
  static unsigned long lastBotCheck = 0;
  if (millis() - lastBotCheck > 1500) {
    int n = bot.getUpdates(bot.last_message_received + 1);
    while (n) { handleNewMessages(n); n = bot.getUpdates(bot.last_message_received + 1); }
    lastBotCheck = millis();
  }
}
#endif
