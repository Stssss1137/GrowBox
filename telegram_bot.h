#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

const char* WIFI_SSID = "XXXXXXXXXXXX";
const char* WIFI_PASS = "XXXXXXXXXXXX";
const char* BOT_TOKEN = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"; 

extern float getAHTTemp();
extern float getAHTHum();
extern int getMQ135();
extern int getSoilRaw(); 
extern int getSoilPercent();
extern bool isDark();
extern bool getPumpState();
extern bool isAutoMode();
extern void changePumpFromBot(bool on, String user, String id);
extern void setAutoModeFromBot(bool on, String user, String id);
extern void setHumThreshold(int val);
extern int soilThreshold; 


WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

unsigned long lastBotCheck = 0;
const unsigned long BOT_INTERVAL = 1000; 

void setupWiFiAndBot() {
  Serial.print(F("WiFi Connecting to: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++attempt > 40) { 
      Serial.println(F("\nWiFi Connect Failed! Rebooting..."));
      delay(1000);
      ESP.restart();
    }
  }

  Serial.println(F("\nWiFi Connected."));
  Serial.print(F("IP: ")); Serial.println(WiFi.localIP());
  client.setInsecure();
}

String getSystemStatus() {
  String msg = "📊 *System Status*\n"; 
  float t = getAHTTemp();
  float h = getAHTHum();
  
  msg += "Temp: " + (isnan(t) ? "N/A" : String(t, 1) + "°C") + "\n";
  msg += "Hum: "  + (isnan(h) ? "N/A" : String(h, 0) + "%") + "\n";
  
  int soil = getSoilPercent();
  msg += "Soil: " + String(soil) + "% (Thr: " + String(soilThreshold) + "%)\n";
  
  msg += "Air Qual (MQ): " + String(getMQ135()) + "\n";
  msg += "Light: " + String(isDark() ? "Dark 🌙" : "Bright ☀️") + "\n";
  msg += "Pump: "  + String(getPumpState() ? "ON 💧" : "OFF 🛑") + "\n";
  msg += "Mode: "  + String(isAutoMode() ? "AUTO " : "MANUAL ") + "\n";

  return msg;
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chatId = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String fromName = bot.messages[i].from_name;
    if (fromName == "") fromName = "User";

    Serial.printf("MSG from %s: %s\n", fromName.c_str(), text.c_str());

    if (text == "/start" || text == "/help") {
      String help = "🌿 *GrowBox Control*\n\n"
                    "/status - Show sensors\n"
                    "/auto - Auto mode ON\n"
                    "/manual - Manual mode (Auto OFF)\n"
                    "/pump_on - Force Pump ON\n"
                    "/pump_off - Force Pump OFF\n"
                    "/set_limit 50 - Set soil target %";
      bot.sendMessage(chatId, help, "Markdown");
    }
    else if (text == "/status") {
      bot.sendMessage(chatId, getSystemStatus(), "Markdown");
    }
    else if (text == "/auto") {
      setAutoModeFromBot(true, fromName, chatId);
      bot.sendMessage(chatId, "✅ Auto Mode Enabled");
    }
    else if (text == "/manual") {
      setAutoModeFromBot(false, fromName, chatId);
      bot.sendMessage(chatId, "⚠️ Manual Mode Enabled");
    }
    else if (text == "/pump_on") {
      changePumpFromBot(true, fromName, chatId);
      bot.sendMessage(chatId, "💧 Pump Turned ON");
    }
    else if (text == "/pump_off") {
      changePumpFromBot(false, fromName, chatId);
      bot.sendMessage(chatId, "🛑 Pump Turned OFF");
    }
    else if (text.startsWith("/set_limit") || text.startsWith("/set_hum_threshold")) {
      // Підтримка обох команд
      int spaceIndex = text.indexOf(' ');
      if (spaceIndex != -1) {
        int val = text.substring(spaceIndex + 1).toInt();
        val = constrain(val, 0, 100);
        setHumThreshold(val);
        bot.sendMessage(chatId, "⚙️ Threshold set to " + String(val) + "%");
      } else {
        bot.sendMessage(chatId, "Usage: /set_limit 50");
      }
    }
    else {
      bot.sendMessage(chatId, "Unknown command. Try /help");
    }
  }
}

void handleTelegram() {
  if (millis() - lastBotCheck > BOT_INTERVAL) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastBotCheck = millis();
  }
}

#endif
