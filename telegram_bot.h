#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

const char* WIFI_SSID = "XXXXXXX";
const char* WIFI_PASS = "XXXXXXX";
const char* BOT_TOKEN = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"; 

String storedChatId = ""; 

extern float getAHTTemp();
extern float getAHTHum();
extern int getSoilPercent();
extern bool getPumpState();
extern bool isAutoMode();
extern int getSoilThreshold();
extern int getLightDuration();
extern String getModeName();
extern bool getLedState();
extern void manualPumpControl(bool on);
extern void setPlantMode(int mode);
extern void setCustomThreshold(int val);

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);
unsigned long lastBotCheck = 0;
const unsigned long BOT_INTERVAL = 1000; 

void setupWiFiAndBot() {
  Serial.print(F("WiFi Connecting..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
    if (++attempt > 30) { ESP.restart(); }
  }
  Serial.println(F("\nConnected."));
  client.setInsecure();
}

void sendBotMessage(String msg) {
  if (storedChatId != "") {
    bot.sendMessage(storedChatId, msg);
  }
}

String getStatusMsg() {
  String m = "📊 *STATUS*\n";
  m += "Mode: " + getModeName() + (isAutoMode()?" (AUTO)":" (MANUAL)") + "\n";
  m += "Temp: " + String(getAHTTemp(),1) + "C, Hum: " + String(getAHTHum(),0) + "%\n";
  m += "Soil: " + String(getSoilPercent()) + "% (Limit: " + String(getSoilThreshold()) + "%)\n";
  m += "Light Sched: " + String(getLightDuration()) + "h\n";
  m += "LED Actual: " + String(getLedState() ? "ON 💡" : "OFF 🌑") + "\n";
  m += "Pump: " + String(getPumpState()?"ON 💦":"OFF");
  return m;
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chatId = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    
    storedChatId = chatId; 

    if (text == "/start" || text.startsWith("/help")) {
      String help = "🌿 *GrowBox*\n"
                    "`/status` - Інфо\n"
                    "`/pump_on` - Полити (5 сек)\n"
                    "`/pump_off` - Стоп\n\n"
                    "⚙️ *Режими (Вмикає AUTO)*\n"
                    "`/mode_tropical` (60%, 12г)\n"
                    "`/mode_succulent` (20%, 14г)\n"
                    "`/mode_veg` (45%, 16г)\n"
                    "`/set_limit 50` (Свій %)";
      bot.sendMessage(chatId, help, "Markdown");
    }
    else if (text == "/status") {
      bot.sendMessage(chatId, getStatusMsg(), "Markdown");
    }
    else if (text == "/pump_on" || text == "/pump_pulse") {
      manualPumpControl(true);
      bot.sendMessage(chatId, "💦 Полив (5 сек). Авто-режим вимкнено.");
    }
    else if (text == "/pump_off") {
      manualPumpControl(false);
      bot.sendMessage(chatId, "🛑 Помпа стоп.");
    }
    else if (text == "/mode_tropical") {
      setPlantMode(1);
      bot.sendMessage(chatId, "🌴 Режим: Тропіки\nГрафік світла: 12 год (Тільки якщо темно)");
    }
    else if (text == "/mode_succulent") {
      setPlantMode(2);
      bot.sendMessage(chatId, "🌵 Режим: Сукуленти\nГрафік світла: 14 год (Тільки якщо темно)");
    }
    else if (text == "/mode_veg") {
      setPlantMode(3);
      bot.sendMessage(chatId, "🍅 Режим: Овочі\nГрафік світла: 16 год (Тільки якщо темно)");
    }
    else if (text.startsWith("/set_limit")) {
      int val = text.substring(11).toInt();
      if(val > 0) {
        setCustomThreshold(val);
        bot.sendMessage(chatId, "⚙️ Поріг: " + String(val) + "%");
      } else {
        bot.sendMessage(chatId, "Приклад: /set_limit 60");
      }
    }
  }
}

void handleTelegram() {
  if (millis() - lastBotCheck > BOT_INTERVAL) {
    int n = bot.getUpdates(bot.last_message_received + 1);
    while (n) {
      handleNewMessages(n);
      n = bot.getUpdates(bot.last_message_received + 1);
    }
    lastBotCheck = millis();
  }
}
#endif
