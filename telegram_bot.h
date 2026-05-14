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
extern String dynamicAdmins; 
extern String dynamicUsers; 

#define BOT_TOKEN "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

void setupTelegram() { secured_client.setInsecure(); }

bool isIDInList(String list, String chat_id) {
  if (list == "") return false;
  return (list.indexOf(chat_id) != -1);
}

bool isAdmin(String chat_id) { return isIDInList(dynamicAdmins, chat_id); }
bool isUser(String chat_id) { return isIDInList(dynamicUsers, chat_id); }
bool isAuthorized(String chat_id) { return isAdmin(chat_id) || isUser(chat_id); }

void sendToList(String list, String msg) {
  if (list.length() < 5) return; 
  String l = list + ",";
  int startIdx = 0;
  int commaIdx = l.indexOf(',');

  while (commaIdx != -1) {
    String id = l.substring(startIdx, commaIdx);
    id.trim();
    if (id.length() > 5) bot.sendMessage(id, msg); 
    startIdx = commaIdx + 1;
    commaIdx = l.indexOf(',', startIdx);
  }
}

void broadcastToAdmins(String msg) {
  sendToList(dynamicAdmins, msg);
}

void broadcastToAll(String msg) {
  sendToList(dynamicAdmins, msg);
  sendToList(dynamicUsers, msg);
}

void sendBotReply(String chat_id, String msg) { 
  bot.sendMessage(chat_id, msg); 
}

void setFirebaseState(String path, String val) { Firebase.setString(fbData, path, val); }
void setFirebaseState(String path, bool val) { Firebase.setBool(fbData, path, val); }
void setFirebaseState(String path, int val) { Firebase.setInt(fbData, path, val); }

void applyClimateChange(String c, String s) {
  int tTemp = 24, tHum = 55, tSoil = 40; String spec = "full";
  if (c == "temperate") { tTemp = 24; tHum = 55; if (s == "seedling") { tSoil = 60; spec = "blue"; } else if (s == "veg") { tSoil = 40; spec = "full"; } else if (s == "flower") { tSoil = 30; spec = "red"; } } 
  else if (c == "tropical") { tTemp = 28; tHum = 80; if (s == "seedling") { tSoil = 75; spec = "blue"; } else if (s == "veg") { tSoil = 60; spec = "full"; } else if (s == "flower") { tSoil = 50; spec = "red"; } }
  else if (c == "arid") { tTemp = 32; tHum = 25; if (s == "seedling") { tSoil = 30; spec = "blue"; } else if (s == "veg") { tSoil = 15; spec = "full"; } else if (s == "flower") { tSoil = 10; spec = "full"; } }
  else if (c == "mediterranean") { tTemp = 26; tHum = 45; if (s == "seedling") { tSoil = 50; spec = "blue"; } else if (s == "veg") { tSoil = 30; spec = "full"; } else if (s == "flower") { tSoil = 20; spec = "red"; } }

  currentClimate = c; currentStage = s; tempTarget = tTemp; humTarget = tHum; soilTarget = tSoil; spectrum = spec; currentMode = "AUTO";

  setFirebaseState("/growbox/controls/climate", c); setFirebaseState("/growbox/controls/stage", s);
  setFirebaseState("/growbox/controls/tempTarget", tTemp); setFirebaseState("/growbox/controls/humTarget", tHum);
  setFirebaseState("/growbox/controls/soilTarget", tSoil); setFirebaseState("/growbox/controls/spectrum", spec);
  setFirebaseState("/growbox/controls/mode", "AUTO");
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;


    if (!isAuthorized(chat_id)) {
      sendBotReply(chat_id, "Доступ заборонено! Ваш ID: " + chat_id );
      broadcastToAdmins("Спроба доступу ID:\n" + chat_id); 
      continue;
    }

    if (text.startsWith("/start") || text.startsWith("/help")) {
      String helpMsg = "Команди для керування боксом:\n\n";
      helpMsg += "📊 /status - Поточний стан \n🤖 /auto - АВТО режим\n🖐 /manual - РУЧНЕ керування\n\n";
      helpMsg += "🌍КЛІМАТ:\n🌳 /c_temp | 🌴 /c_trop | 🌵 /c_arid | 🌿 /c_med\n\n";
      helpMsg += "🌱СТАДІЯ:\n1️⃣ /s_seed | 2️⃣ /s_veg | 3️⃣ /s_flow\n\n";
      helpMsg += " РУЧНЕ УПРАВЛІННЯ:\n💦 /pump_on | /pump_off \n💨 /fan_on | /fan_off \n💡 /light_blue | /light_red | /light_full | /light_off";
      sendBotReply(chat_id, helpMsg); 
    }
    else if (text.startsWith("/status")) {
      String m = "📊СТАТУС СИСТЕМИ\nРежим: " + currentMode + "\nКлімат: " + currentClimate + " (Стадія: " + currentStage + ")\n\n";
      if (ahtError) m += "Темп/Волога: ПОМИЛКА ДАТЧИКА\n";
      else m += "🌡Темп: " + String(temp,1) + "C | Волога: " + String(hum,0) + "%\n";
      if (soilError) m += "ДАТЧИК гігрометра ВІДКЛЮЧЕНО\n";
      else m += "🌱Ґрунт: " + String(soilPct) + "% (Ціль: " + String(soilTarget) + "%)\n";
      m += "Забруднення: " + String(airQuality) + "%\nПолив: " + String(pumpOn?"ПРАЦЮЄ":"ВИМК") + "\n";
      m += "Вентиляція: " + String(fanOn?"ПРАЦЮЄ":"ВИМК") + "\nСвітло: " + String(lightOn?"УВІМК ("+spectrum+")":"ВИМК");
      sendBotReply(chat_id, m);
    }
    else if (text.startsWith("/c_temp")) { applyClimateChange("temperate", currentStage); broadcastToAll("🌳 Змінено клімат: Помірний"); }
    else if (text.startsWith("/c_trop")) { applyClimateChange("tropical", currentStage); broadcastToAll("🌴 Змінено клімат: Тропіки"); }
    else if (text.startsWith("/c_arid")) { applyClimateChange("arid", currentStage); broadcastToAll("🌵 Змінено клімат: Пустеля"); }
    else if (text.startsWith("/c_med")) { applyClimateChange("mediterranean", currentStage); broadcastToAll("🌿 Змінено клімат: Середземноморський"); }
    
    else if (text.startsWith("/s_seed")) { applyClimateChange(currentClimate, "seedling"); broadcastToAll("🌱 Стадія змінена: Розсада"); }
    else if (text.startsWith("/s_veg")) { applyClimateChange(currentClimate, "veg"); broadcastToAll("🌿 Стадія змінена: Вегетація"); }
    else if (text.startsWith("/s_flow")) { applyClimateChange(currentClimate, "flower"); broadcastToAll("🌸 Стадія змінена: Цвітіння"); }

    else if (text.startsWith("/auto")) { currentMode = "AUTO"; setFirebaseState("/growbox/controls/mode", "AUTO"); broadcastToAll("🤖 АВТО режим увімкнено"); }
    else if (text.startsWith("/manual")) { currentMode = "MANUAL"; setFirebaseState("/growbox/controls/mode", "MANUAL"); broadcastToAll("🖐 РУЧНИЙ режим увімкнено"); }

    else if (text.startsWith("/pump_on")) { 
      if (soilError) { sendBotReply(chat_id, "Вірогідно гігрометр відключений, перевірте"); } 
      else { setFirebaseState("/growbox/controls/mode", "MANUAL"); pumpStartTime = millis(); setFirebaseState("/growbox/controls/pumpOn", true); broadcastToAll("💦Полив увімкнено вручну"); }
    }
    else if (text.startsWith("/pump_off")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/pumpOn", false); broadcastToAll("Полив вимкненено вручну"); }
    else if (text.startsWith("/fan_on")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/fanOn", true); broadcastToAll("Вентилятор увімкнено"); }
    else if (text.startsWith("/fan_off")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/fanOn", false); broadcastToAll("Вентилятор вимкнено"); }
    else if (text.startsWith("/light_blue")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/lightOn", true); setFirebaseState("/growbox/controls/spectrum", "blue"); broadcastToAll("🔵 СИНІЙ спектр увімкнено"); }
    else if (text.startsWith("/light_red")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/lightOn", true); setFirebaseState("/growbox/controls/spectrum", "red"); broadcastToAll("🔴 ЧЕРВОНИЙ спектр увімкнено"); }
    else if (text.startsWith("/light_full")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/lightOn", true); setFirebaseState("/growbox/controls/spectrum", "full"); broadcastToAll("🟣 ПОВНИЙ спектр увімкнено"); }
    else if (text.startsWith("/light_off")) { setFirebaseState("/growbox/controls/mode", "MANUAL"); setFirebaseState("/growbox/controls/lightOn", false); broadcastToAll("Світло вимкнено"); }
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
