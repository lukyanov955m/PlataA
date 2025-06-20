const char* versiyaProshivki = "1.2";
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFiMulti.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include <UniversalTelegramBot.h>
#include <ESP8266HTTPClient.h>
#include <time.h>
#include <string.h>

#define MAKS_ZAYAVOK 1

void sohranitPolzovateley();
void proveritWiFi();
void otpravkaMoiIP();
void priemPaketa();
void statusSvyazi();
void obrabotatGlavnuyuStranicu();
void obrabotatSohranenie();
void podozhdat(unsigned long ms);

char ssid[33] = "";
char password[65] = "";
char token_bot[65] = "";
char glavniy_admin_id[32] = "";

WiFiClientSecure bezopasnyClient;
UniversalTelegramBot* bot = nullptr;

const char* fail_polzovateli = "/polzovateli.json";

struct Zayavka {
  String id;
  String imya;
};
Zayavka spisokZayavok[MAKS_ZAYAVOK];
int kolvoZayavok = 0;

void podozhdat(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    yield();
  }
}

struct Polzovatel {
  String id;
  String imya;
  String roli;
  unsigned long konetsDostupa;
};

void vremya(){
configTime(5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  // Дождаться обновления времени!
  time_t now = 0;
  int attempts = 0;
  while (now < 1672531200 && attempts < 30) { // 1672531200 — это 2023 год для примера
    podozhdat(200);
    now = time(nullptr);
    attempts++;
  }
  Serial.print(F("Текущее время: "));
  Serial.println(now);
}

#define MAKS_POLZOVATELEY 10
Polzovatel spisokPolzovateley[MAKS_POLZOVATELEY];
int kolvoPolzovateley = 0;

int naytiZayavku(String id) {
  for (int i = 0; i < kolvoZayavok; i++) {
    if (spisokZayavok[i].id == id) return i;
  }
  return -1;
}

void otpravitSpisokPolzovateley(String chat_id) {
  String otvet = "📋 *Список пользователей:*\n\n";
  unsigned long tekTime = time(nullptr);

  for (int i = 0; i < kolvoPolzovateley; i++) {
        if (spisokPolzovateley[i].id == glavniy_admin_id) continue; // <-- эта строка пропускает главного админа
    String stroka = spisokPolzovateley[i].imya + " (ID: " + spisokPolzovateley[i].id + ")";
    stroka += " — " + spisokPolzovateley[i].roli;
    if (spisokPolzovateley[i].roli == "gost") {
      unsigned long ostatok = spisokPolzovateley[i].konetsDostupa - tekTime;
      if (ostatok > 0) {
        unsigned long ch = ostatok / 3600;
        unsigned long min = (ostatok % 3600) / 60;
        stroka += " (осталось " + String(ch) + "ч " + String(min) + "м)";
      } else {
        stroka += " (срок прошёл)";
      }
    }
    otvet += stroka + "\n";
  }
  otvet += "\n🗑 Для удаления: `Удалить-ID`, пример: `Удалить-123456789`\n";
  bot->sendMessage(chat_id, otvet, "Markdown");
}

void udalitPolzovatelyaPoId(String id, String chat_id) {
  int idx = naytiPolzovatelya(id);
  if (idx == -1) {
    bot->sendMessage(chat_id, "❌ Пользователь с таким ID не найден.");
    return;
  }
  if (spisokPolzovateley[idx].id == glavniy_admin_id) {
    bot->sendMessage(chat_id, "🚫 Главного админа нельзя удалить.");
    return;
  }
  String imya = spisokPolzovateley[idx].imya;
  String id_udalyayuschego = chat_id;
  String imya_udalyayuschego = "";
  // Получим имя того, кто удаляет
  int k = naytiPolzovatelya(chat_id);
  if (k != -1) {
    imya_udalyayuschego = spisokPolzovateley[k].imya;
  }
  String id_polzovatelya = spisokPolzovateley[idx].id;
  // Уведомим удаляемого пользователя
    bot->sendMessageWithReplyKeyboard(
    id_polzovatelya,
    "🚫 Ваши права доступа были отменены администратором.\nМожно запросить доступ снова.",
    "Markdown",
    menuOtvet("user"), true, false, false);
  // Удаляем
  for (int i = idx; i < kolvoPolzovateley - 1; i++) {
    spisokPolzovateley[i] = spisokPolzovateley[i + 1];
  }
  kolvoPolzovateley--;
  sohranitPolzovateley();
  // Подтверждение администратору
  bot->sendMessage(chat_id, "✅ Пользователь '" + imya + "' удалён.");
  // Лог всем админам
  String log = "🗑 Админ *" + imya_udalyayuschego + "* удалил пользователя *" + imya + "* (ID: " + id + ")";
  for (int i = 0; i < kolvoPolzovateley; i++) {
    if (spisokPolzovateley[i].roli == "admin" || spisokPolzovateley[i].id == glavniy_admin_id) {
      bot->sendMessage(spisokPolzovateley[i].id, log, "Markdown");
    }
  }
}

void dobavitZayavku(String id, String imya) {
  if (naytiZayavku(id) == -1 && kolvoZayavok < MAKS_ZAYAVOK) {
    spisokZayavok[kolvoZayavok].id = id;
    spisokZayavok[kolvoZayavok].imya = imya;
    kolvoZayavok++;
  }
}

void otpravitZayavkuAdminam(String id, String imya) {
  String text = "📥 Новая заявка от: *" + imya + "*\nID: `" + id + "`";
  String keyboard = "[[\"6 ч\", \"12 ч\", \"24 ч\"], [\"Админ\", \"Отклонить\"]]";
  for (int i = 0; i < kolvoPolzovateley; i++) {
    if (spisokPolzovateley[i].roli == "admin" || spisokPolzovateley[i].id == glavniy_admin_id) {
      bot->sendMessageWithReplyKeyboard(
        spisokPolzovateley[i].id, text, "Markdown", keyboard, true, false, false);
    }
  }
}


void odobritZayavku(String id, String imya, String roli, int chasy) {
  int index = naytiPolzovatelya(id);
  if (index == -1 && kolvoPolzovateley < MAKS_POLZOVATELEY) {
    index = kolvoPolzovateley++;
  }
  spisokPolzovateley[index].id = id;
  spisokPolzovateley[index].imya = imya;
  spisokPolzovateley[index].roli = roli;
  spisokPolzovateley[index].konetsDostupa = (chasy > 0) ? time(nullptr) + chasy * 3600 : 0;
  sohranitPolzovateley();
  // Сообщение пользователю
  bot->sendMessageWithReplyKeyboard(id,
                                    "✅ Вам выданы права: " + roli + (chasy > 0 ? " на " + String(chasy) + " часов" : ""),
                                    "", menuOtvet(roli), true, false, false);
  // Удаляем из заявок
  int idx = naytiZayavku(id);
  if (idx != -1) {
    for (int j = idx; j < kolvoZayavok - 1; j++) {
      spisokZayavok[j] = spisokZayavok[j + 1];
    }
    kolvoZayavok--;
  }
  // Получим имя того, кто назначил (ищем по ID админа — временно chat_id храним)
  String imya_admina = "Админ";
  for (int i = 0; i < kolvoPolzovateley; i++) {
    if (spisokPolzovateley[i].id == glavniy_admin_id || spisokPolzovateley[i].roli == "admin") {
      if (spisokPolzovateley[i].id == bot->messages[0].chat_id) {
        imya_admina = spisokPolzovateley[i].imya;
        break;
      }
    }
  }
  // Формируем текст уведомления
  String log = "✅ *" + imya_admina + "* выдал доступ *" + imya + "*\n";
  if (roli == "gost") {
    log += "🕒 Роль: *Гость*, на " + String(chasy) + " часов";
  } else if (roli == "admin") {
    log += "🛡 Роль: *Админ* (бессрочно)";
  }
  // Отправляем всем админам
  for (int i = 0; i < kolvoPolzovateley; i++) {
    if (spisokPolzovateley[i].roli == "admin" || spisokPolzovateley[i].id == glavniy_admin_id) {
      bot->sendMessage(spisokPolzovateley[i].id, log, "Markdown");
    }
  }
}

void otklonitZayavku(String id) {
  bot->sendMessage(id, "Ваша заявка на доступ была отклонена администратором дома.");

  int idx = naytiZayavku(id);
  if (idx != -1) {
    for (int j = idx; j < kolvoZayavok - 1; j++) {
      spisokZayavok[j] = spisokZayavok[j + 1];
    }
    kolvoZayavok--;
  }
}

void otpravitSoobshenieAdminam(String text) {
  for (int i = 0; i < kolvoPolzovateley; i++) {
    if (spisokPolzovateley[i].roli == "admin" || spisokPolzovateley[i].id == glavniy_admin_id) {
      bot->sendMessage(spisokPolzovateley[i].id, text, "Markdown");
    }
  }
}

unsigned long posledneeObnovlenie = 0;
int posledniyUpdateID = 0;

unsigned long vremyaPosledneyProverkiGostey = 0;
const unsigned long intervalGostey = 300000;  // 5 минут = 300000 мс

const char* ota_login = "admin";      // Логин для браузера OTA
const char* ota_password = "961023";  // Пароль для браузера и Arduino IDE

#define PIN_KNOPKA 14      // кнопка очистки памяти
#define ZADERZHKA_MS 5000  // 5 секунд в миллисекундах
unsigned long vremyaNazhatiya = 0;
bool knopkaUderzhana = false;

#define PIN_SVETODIOD 2  // Подкорректируй пин, если нужен другой

const char* imyaTochki = "HEAD_Setup";
const char* parolTochki = "12345678";
const int portUDP = 4210;
const unsigned long vremyaTimeout = 5000;

WiFiUDP udp;
ESP8266WebServer server(3654);
ESP8266HTTPUpdateServer httpUpdater;
ESP8266WiFiMulti wifiMulti;

IPAddress ipDrugoyPlaty;
bool ipNayden = false;
bool platyVidyatDrugDruga = false;

bool wifiPodklyuchen = false;
bool wifiRezhimTochki = false;
bool spisokSeteyGotov = false;

unsigned long vremyaPoslednegoOtklika = 0;
unsigned long vremyaPoslednegoVivoda = 0;
unsigned long poslednyayaProverkaWiFi = 0;

void StartTelegramBot() {
  bezopasnyClient.setInsecure();  // Без проверки сертификата

  if (bot) delete bot;
  bot = new UniversalTelegramBot(token_bot, bezopasnyClient);

  if (!bot->getMe()) {
    Serial.println(F("[ОШИБКА] Бот не авторизовался! Проверь токен."));
  } else {
    Serial.println(F("[OK] Бот Telegram готов."));
  }
  Serial.print(F("[DEBUG] WiFi статус: ")); Serial.println(WiFi.status());
  Serial.print(F("[DEBUG] IP: ")); Serial.println(WiFi.localIP());
  Serial.print(F("[DEBUG] Токен: >")); Serial.print(token_bot); Serial.print(F("< длина: ")); Serial.println(strlen(token_bot));
otpravitSoobshenieAdminam("⚡️ *Устройство перезапущено!*\n" 
  "Имя устройства: HEAD\n" 
  "IP: `" + (WiFi.softAPIP().toString()) + "` / `" + WiFi.localIP().toString() + "`\n"
  "Время: " + String(millis() / 1000) + " сек с запуска.");
  vremya();
}

int naytiPolzovatelya(String id) {
  for (int i = 0; i < kolvoPolzovateley; i++) {
    if (spisokPolzovateley[i].id == id) return i;
  }
  return -1;
}

String menuOtvet(String roli) {
  if (roli == "admin") {
return "[[\"Статус первой платы\", \"Статус второй платы\"], [\"Список пользователей\"], [\"Разрешить заявки\", \"Запретить заявки\"], [\"Перезапуск\"]]";
  } else if (roli == "gost") {
    return "[[\"Открыть\"]]";
  } else {
    return "[[\"Запросить доступ\"]]";
  }
}

void obrabotatSoobsheniya(int skolko) {
  for (int i = 0; i < skolko; i++) {
    String chat_id = bot->messages[i].chat_id;
    String text = bot->messages[i].text;
    String from_id = String(bot->messages[i].from_id);
    String imya = bot->messages[i].from_name;

    int index = naytiPolzovatelya(from_id);
    String roli = (index != -1) ? spisokPolzovateley[index].roli : "user";

    // === Быстрое одобрение заявки текстом (если нет inline) ===
    if (naytiZayavku(from_id) == -1 && roli == "admin") {
      if (text == "6 ч" || text == "12 ч" || text == "24 ч" || text == "Админ" || text == "Отклонить") {
        if (kolvoZayavok > 0) {
          String idZ = spisokZayavok[0].id;
          String imyaZ = spisokZayavok[0].imya;
          if (text == "6 ч") odobritZayavku(idZ, imyaZ, "gost", 6);
          else if (text == "12 ч") odobritZayavku(idZ, imyaZ, "gost", 12);
          else if (text == "24 ч") odobritZayavku(idZ, imyaZ, "gost", 24);
          else if (text == "Админ") odobritZayavku(idZ, imyaZ, "admin", 0);
          else if (text == "Отклонить") otklonitZayavku(idZ);

          bot->sendMessageWithReplyKeyboard(chat_id, "✅ Действие выполнено.", "", menuOtvet(roli), true, false, false);
        } else {
          bot->sendMessageWithReplyKeyboard(chat_id, "Нет активных заявок для обработки.", "", menuOtvet(roli), true, false, false);
        }
        continue;
      }
    }

    // === Команды ===
    if (text == "/start" || text == "Помощь") {
      bot->sendMessageWithReplyKeyboard(chat_id, "Добро пожаловать в мой дом! Ваша роль: " + roli, "", menuOtvet(roli), true, false, false);
    }
    else if (text == "Статус второй платы" && roli == "admin") {
      zaprositStatus(chat_id);
    }
    else if (text == "Статус первой платы" && roli == "admin"){
      String otchet = polnayaInformaciyaObUstroystve();
      bot->sendMessageWithReplyKeyboard(chat_id, otchet, "", menuOtvet(roli), true, false, false);
    }
    else if (text == "Перезапуск" && roli == "admin") {
      bot->sendMessageWithReplyKeyboard(chat_id, "🔄 Позже будет реализован перезапуск двух плат...", "", menuOtvet(roli), true, false, false);
    }
    else if (text == "Список пользователей" && roli == "admin") {
      otpravitSpisokPolzovateley(chat_id);
    }
    else if (text == "Открыть" && (roli == "admin" || roli == "gost")) {
      bot->sendMessageWithReplyKeyboard(chat_id, "🔓 Функция открытия скоро будет реализована.", "", menuOtvet(roli), true, false, false);
    }
    else if ((text.startsWith("Удалить-") || text.startsWith("удалить-")) && roli == "admin") {
      String idDlyaUdalenia = text.substring(text.indexOf('-') + 1);
      if (idDlyaUdalenia == glavniy_admin_id) {
        bot->sendMessageWithReplyKeyboard(chat_id, "🚫 Этого пользователя удалить нельзя.", "", menuOtvet(roli), true, false, false);
      } else {
        udalitPolzovatelyaPoId(idDlyaUdalenia, chat_id);
        bot->sendMessageWithReplyKeyboard(chat_id, "Пользователь удалён.", "", menuOtvet(roli), true, false, false);
      }
    }
    else if (text == "Запросить доступ") {
      if (kolvoZayavok >= MAKS_ZAYAVOK) {
        bot->sendMessageWithReplyKeyboard(chat_id, "Ваша заявка не может быть принята. Попробуйте позже.", "", menuOtvet(roli), true, false, false);
      } else {
        dobavitZayavku(from_id, imya);
        otpravitZayavkuAdminam(from_id, imya);
        bot->sendMessageWithReplyKeyboard(chat_id, "📨 Ваша заявка отправлена администратору. Пожалуйста, ожидайте уведомление.", "", menuOtvet(roli), true, false, false);
      }
    }
    else if (text == "Запретить заявки" && roli == "admin") {
      kolvoZayavok = MAKS_ZAYAVOK;
      bot->sendMessageWithReplyKeyboard(chat_id, "❌ Новые заявки теперь не принимаются.", "", menuOtvet(roli), true, false, false);
    }
    else if (text == "Разрешить заявки" && roli == "admin") {
      for (int j = 0; j < kolvoZayavok; j++) {
        spisokZayavok[j].id = "";
        spisokZayavok[j].imya = "";
      }
      kolvoZayavok = 0;
      bot->sendMessageWithReplyKeyboard(chat_id, "✅ Теперь новые заявки принимаются. Все старые заявки удалены.", "", menuOtvet(roli), true, false, false);
    }
    else {
      bot->sendMessageWithReplyKeyboard(chat_id, "❓ Неизвестная команда, либо у Вас нет доступа.", "", menuOtvet(roli), true, false, false);
    }
  }
}

  void obrabotatTelegram() {
    if (WiFi.status() == WL_CONNECTED && bot) {
      if (millis() - posledneeObnovlenie > 2000) {
        int novye = bot->getUpdates(posledniyUpdateID + 1);
        while (novye) {
          obrabotatSoobsheniya(novye);
          for (int i = 0; i < novye; i++) {
            posledniyUpdateID = bot->messages[i].update_id;
          }
          novye = bot->getUpdates(posledniyUpdateID + 1);
        }
        posledneeObnovlenie = millis();
      }
    }
  }

  void zagruzitPolzovateley() {
    if (!SPIFFS.exists(fail_polzovateli)) {
      File f = SPIFFS.open(fail_polzovateli, "w");
      if (f) {
        f.print("[]");
        f.close();
      }
      kolvoPolzovateley = 0;
      return;
    }

    File f = SPIFFS.open(fail_polzovateli, "r");
    if (!f) {
      Serial.println(F("[ОШИБКА] Не удалось открыть файл пользователей"));
      kolvoPolzovateley = 0;
      return;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, f);
    if (err) {
      kolvoPolzovateley = 0;
      f.close();
      return;
    }

    kolvoPolzovateley = 0;
    for (JsonObject obj : doc.as<JsonArray>()) {
      if (kolvoPolzovateley >= MAKS_POLZOVATELEY) break;
      spisokPolzovateley[kolvoPolzovateley].id = obj["id"].as<String>();
      spisokPolzovateley[kolvoPolzovateley].imya = obj["imya"].as<String>();
      spisokPolzovateley[kolvoPolzovateley].roli = obj["roli"].as<String>();
      spisokPolzovateley[kolvoPolzovateley].konetsDostupa = obj["konetsDostupa"] | 0;
      kolvoPolzovateley++;
    }

    f.close();
  }

  void sohranitPolzovateley() {
    StaticJsonDocument<1024> doc;
    JsonArray massiv = doc.to<JsonArray>();

    for (int i = 0; i < kolvoPolzovateley; i++) {
      JsonObject obj = massiv.createNestedObject();
      obj["id"] = spisokPolzovateley[i].id;
      obj["imya"] = spisokPolzovateley[i].imya;
      obj["roli"] = spisokPolzovateley[i].roli;
      obj["konetsDostupa"] = spisokPolzovateley[i].konetsDostupa;
    }

    File f = SPIFFS.open(fail_polzovateli, "w");
    if (!f) {
      Serial.println(F("[ОШИБКА] Не удалось открыть файл пользователей для записи"));
      return;
    }
    serializeJson(doc, f);
    f.close();
  }

  void proveritProshliLiGosti() {
    if (millis() - vremyaPosledneyProverkiGostey >= intervalGostey) {
      vremyaPosledneyProverkiGostey = millis();
      Serial.println(F("Вызвана proveritProshliLiGosti, по логике раз в 5 мин"));
      unsigned long tekushcheeVremya = time(nullptr);
      bool izmeneniya = false;

      for (int i = 0; i < kolvoPolzovateley; i++) {
        if (spisokPolzovateley[i].roli == "gost" && spisokPolzovateley[i].konetsDostupa > 0 && spisokPolzovateley[i].konetsDostupa <= tekushcheeVremya) {
          spisokPolzovateley[i].roli = "user";
          spisokPolzovateley[i].konetsDostupa = 0;
          izmeneniya = true;
        }
      }

      if (izmeneniya) sohranitPolzovateley();
    }
  }

  void ubeditGlavnyiAdminEst() {
    for (int i = 0; i < kolvoPolzovateley; i++) {
      if (spisokPolzovateley[i].id == glavniy_admin_id) return;
    }

    if (kolvoPolzovateley < MAKS_POLZOVATELEY) {
      Polzovatel admin;
      admin.id = glavniy_admin_id;
      admin.imya = "System";
      admin.roli = "admin";
      admin.konetsDostupa = 0;
      spisokPolzovateley[kolvoPolzovateley++] = admin;
      sohranitPolzovateley();
    }
  }

  // === Загрузка Wi-Fi настроек из SPIFFS ===
  void zagruzitNastroiki() {
    File f = SPIFFS.open("/wifi.json", "r");
    if (!f) return;
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
      strlcpy(ssid, doc["ssid"] | "", sizeof(ssid));
      strlcpy(password, doc["pass"] | "", sizeof(password));
      strlcpy(token_bot, doc["token"] | "", sizeof(token_bot));
      strlcpy(glavniy_admin_id, doc["admin_id"] | "", sizeof(glavniy_admin_id));
      f.close();
    }
  }

  // === Сохранение Wi-Fi настроек в SPIFFS ===
  void sohranitNastroiki(String s, String p, String token, String admin_id) {
    StaticJsonDocument<512> doc;
    doc["ssid"] = s;
    doc["pass"] = p;
    doc["token"] = token;
    doc["admin_id"] = admin_id;
    File f = SPIFFS.open("/wifi.json", "w");
    if (!f) {
      Serial.println(F("[ОШИБКА] Не удалось открыть wifi.json для записи"));
      return;
    }
    serializeJson(doc, f);
    f.close();
  }


  void indikatorSostoyaniya() {
    static unsigned long t = 0;
    static bool led = false;
    static int migCount = 0;

    unsigned long tekuscheeVremya = millis();

    // === Платы видят друг друга — светодиод постоянно ВКЛ ===
    if (wifiPodklyuchen && platyVidyatDrugDruga) {
      digitalWrite(PIN_SVETODIOD, LOW);  // LOW — включено (если активный LOW)
      return;
    }

    // === Режим точки доступа — 5 миганий по 0.3 сек, затем пауза 2 сек ===
    if (wifiRezhimTochki) {
      if (migCount < 10) {  // 5 миганий = 10 смен (вкл/выкл)
        if (tekuscheeVremya - t >= 300) {
          t = tekuscheeVremya;
          led = !led;
          digitalWrite(PIN_SVETODIOD, led);
          migCount++;
        }
      } else {
        digitalWrite(PIN_SVETODIOD, LOW);  // Выключено на паузе
        if (tekuscheeVremya - t >= 2000) {
          t = tekuscheeVremya;
          migCount = 0;
        }
      }
      return;
    }

    // === Подключено к Wi-Fi, но вторая плата недоступна — мигает каждые 0.3 сек ===
    if (wifiPodklyuchen && !platyVidyatDrugDruga) {
      if (tekuscheeVremya - t >= 300) {
        t = tekuscheeVremya;
        led = !led;
        digitalWrite(PIN_SVETODIOD, led);
      }
      return;
    }

    // === Если ничего из вышеперечисленного — выключаем ===
    digitalWrite(PIN_SVETODIOD, LOW);
  }

  void ochistitSPIFFS() {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) SPIFFS.remove(dir.fileName());
  }

  ////отправка запросов на другую плату
  void otpravitHttpKomandu(String cmd) {
    if (!ipDrugoyPlaty || WiFi.status() != WL_CONNECTED) {
      Serial.println(F("Нет IP другой платы или нет Wi-Fi"));
      return;
    }

    WiFiClient client;
    if (client.connect(ipDrugoyPlaty, 3654)) {
      String zapros = "GET /do?cmd=" + cmd + " HTTP/1.1\r\n";
      zapros += "Host: " + ipDrugoyPlaty.toString() + "\r\n";
      zapros += "Connection: close\r\n\r\n";

      client.print(zapros);
      Serial.print(F("HTTP-запрос отправлен: ")); Serial.println(zapros);

      // Чтение ответа (опционально)
      unsigned long timeout = millis();
      while (client.available() == 0) {
        if (millis() - timeout > 3000) {
          Serial.println(F("Время ожидания ответа истекло"));
          client.stop();
          return;
        }
      }

      while (client.available()) {
        String stroka = client.readStringUntil('\n');
        Serial.println(stroka);
      }

      client.stop();
    } else {
      Serial.print(F("Ошибка подключения к ")); Serial.println(ipDrugoyPlaty);
    }
  }

String polnayaInformaciyaObUstroystve() {  String out = "";

  // 1. Название устройства и версия
  out += "🛠️ УСТРОЙСТВО: Головное (NodeMCU, Plata A) \n";
  out += "🔖 Версия прошивки: " + String(versiyaProshivki) + "\n";

  // 2. Время и аптайм
  time_t now = time(nullptr);
  char buf[32];
  strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", localtime(&now));

  unsigned long uptimeSec = millis() / 1000;
unsigned long uptimeDay = uptimeSec / 86400;
unsigned long uptimeHr = (uptimeSec % 86400) / 3600;
unsigned long uptimeMin = (uptimeSec % 3600) / 60;

if (uptimeDay > 0)
  out += "⏳ Аптайм: " + String(uptimeDay) + " д " + String(uptimeHr) + " ч " + String(uptimeMin) + " мин\n";
else
  out += "⏳ Аптайм: " + String(uptimeHr) + " ч " + String(uptimeMin) + " мин\n";

  // 3. Wi-Fi
  out += "📡 Режим работы: " + String(wifiRezhimTochki ? "Точка доступа (AP)" : "Клиент Wi-Fi (STA)") + "\n";
  out += "📶 Статус Wi-Fi: " + String(wifiPodklyuchen ? "ПОДКЛЮЧЕН" : "НЕТ СЕТИ") + "\n";
  out += String(F("📂 SSID: ")) + (strlen(ssid) ? String(ssid) : F("не задан")) + "\n";
  out += "🌐 IP устройства: " + (wifiRezhimTochki ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\n";

  // 4. Информация о второй плате
  if (ipNayden) {
    out += "🤝 Вторая плата: " + ipDrugoyPlaty.toString() + "\n";
    out += "🔗 Связь: " + String(platyVidyatDrugDruga ? "НА СВЯЗИ" : "НЕТ ОТКЛИКА") + "\n";
  } else {
    out += "❌ Вторая плата: IP не найден\n";
  }

  // 5. Список файлов в SPIFFS
  out += "📁 Файлы во внутренней памяти (SPIFFS):\n";
  Dir dir = SPIFFS.openDir("/");
  bool estFiles = false;
  while (dir.next()) {
    out += "  • " + dir.fileName();
    File f = dir.openFile("r");
    out += " (" + String(f.size()) + " байт)\n";
    f.close();
    estFiles = true;
  }
  if (!estFiles) out += "  — Нет файлов\n";

  // 6. Использование памяти
  FSInfo info;
  SPIFFS.info(info);
  out += "💾 Память: всего " + String(info.totalBytes/1024) + " КБ, занято " + String(info.usedBytes/1024) + " КБ\n";

  // 7. Прочие данные (по желанию)
  // out += "🔋 Напряжение питания: " + String(ESP.getVcc()/1024.0) + " В\n";

  return out;
}


  void zapustitpriemhttp() {
    server.on("/do", []() {
      if (server.hasArg("cmd")) {
        String komanda = server.arg("cmd");
        server.send(200, "text/plain", "Получена команда: " + komanda);
        Serial.print(F("HTTP-команда получена: ")); Serial.println(komanda);
      } else {
        server.send(400, "text/plain", F("Параметр cmd не передан"));
      }
    });
    //server.begin(); //удалил чтобы проерить ++++++++++++++++
  }

void zaprositStatus(String chat_id) {
  if (!ipDrugoyPlaty || WiFi.status() != WL_CONNECTED) {
    bot->sendMessage(chat_id, "❌ Нет связи со второй платой или Wi-Fi не подключён.", "Markdown");
    return;
  }

  static int popitka = 0;
  static unsigned long lastTryTime = 0;
  static bool waitingResponse = false;
  static WiFiClient client;
  static unsigned long startWait = 0;
  static String allStatus = "";

  if (!waitingResponse && popitka == 0) {
    popitka = 1;
    lastTryTime = millis();
  }

  while (popitka <= 3) {
    if (!waitingResponse) {
      // Пытаемся подключиться и отправить запрос
      if (client.connect(ipDrugoyPlaty, 3654)) {
        String zapros = "GET /status HTTP/1.1\r\n";
        zapros += "Host: " + ipDrugoyPlaty.toString() + "\r\n";
        zapros += "Connection: close\r\n\r\n";
        client.print(zapros);
        bot->sendMessage(chat_id, "📤 Запрос статуса (попытка " + String(popitka) + ")", "Markdown");
        Serial.print(F("📤 Отправлен запрос статуса (попытка ")); Serial.print(popitka); Serial.println(F(")"));
        waitingResponse = true;
        startWait = millis();
        allStatus = "";
      } else {
        bot->sendMessage(chat_id, "❌ Не удалось подключиться к плате (попытка " + String(popitka) + ")", "Markdown");
        Serial.print(F("❌ Не удалось подключиться к ")); Serial.println(ipDrugoyPlaty);
        lastTryTime = millis();
        popitka++;
        continue;
      }
    }

    // Если ожидаем ответ, но не дождались
    if (waitingResponse && millis() - startWait > 5000) {
      bot->sendMessage(chat_id, "⛔ Время ожидания ответа истекло (попытка " + String(popitka) + ")", "Markdown");
      Serial.println(F("⛔ Время ожидания ответа истекло"));
      client.stop();
      waitingResponse = false;
      popitka++;
      continue;
    }

    // Если есть ответ
    if (waitingResponse && client.available() > 0) {
      bool telo_nachalos = false;
      while (client.available()) {
        String stroka = client.readStringUntil('\n');
        if (!telo_nachalos) {
          if (stroka == "\r") telo_nachalos = true;
          continue;
        }
        stroka.trim();
        if (stroka.length() > 0) {
          Serial.println(stroka);
          allStatus += stroka + "\n";
        }
      }
      client.stop();
      bot->sendMessage(chat_id, allStatus, "Markdown");
      Serial.println(F("✅ Статус получен и выведен"));
      popitka = 0;
      waitingResponse = false;
      return;
    }

    // Если ничего не произошло и 1 сек с последней попытки прошло, переходим к следующей попытке
    if (!waitingResponse && millis() - lastTryTime > 1000) {
      popitka++;
    }
    return; // Вернуть управление loop, всё остальное будет допроверено в следующем вызове
  }

  // Если не удалось за 3 попытки
  if (popitka > 3) {
    bot->sendMessage(chat_id, "🚫 Три попытки завершились неудачей. Статус второй платы не получен.", "Markdown");
    popitka = 0;
    waitingResponse = false;
  }
}



  void setup() {
    Serial.begin(115200);
    podozhdat(1000);
    pinMode(PIN_KNOPKA, INPUT_PULLUP);  // Используем внутреннюю подтяжку к VCC
    pinMode(PIN_SVETODIOD, OUTPUT);
    digitalWrite(PIN_SVETODIOD, LOW);  // чтобы стартовать в выкл состоянии
    if (!SPIFFS.begin()) {
    }

    zagruzitNastroiki();
    Serial.print(F("SSID: ")); Serial.println(ssid);
    Serial.print(F("Пароль: ")); Serial.println(password);
    Serial.print(F("Токен: ")); Serial.println(token_bot);
    Serial.print(F("ID админа: ")); Serial.println(glavniy_admin_id);
    proveritWiFi();
    udp.begin(portUDP);
    // --- OTA через браузер ---
    httpUpdater.setup(&server, "/update", ota_login, ota_password);

    // --- OTA через Arduino IDE ---
    ArduinoOTA.setHostname("Gate_config_Plata_A_Telegram");
    ArduinoOTA.setPassword(ota_password);  // защита прошивки
    ArduinoOTA.begin();
    server.on("/", obrabotatGlavnuyuStranicu);
    server.on("/sohranit", HTTP_POST, obrabotatSohranenie);
    server.begin();
    zagruzitPolzovateley();
    Serial.print(F("Пользователи загружены: ")); Serial.println(kolvoPolzovateley);
    ubeditGlavnyiAdminEst();
  }

  void loop() {
    ///////////ПРОВЕРКА КНОПКИ СБРОСА/////////
    bool sostoyanie = digitalRead(PIN_KNOPKA) == LOW;

    if (sostoyanie && !knopkaUderzhana) {
      // Кнопка нажата — запоминаем время
      vremyaNazhatiya = millis();
      knopkaUderzhana = true;
    }

    if (!sostoyanie && knopkaUderzhana) {
      // Кнопку отпустили — сбрасываем флаг
      knopkaUderzhana = false;
    }

    if (sostoyanie && knopkaUderzhana) {
      // Кнопка удерживается — проверяем длительность
      if (millis() - vremyaNazhatiya >= ZADERZHKA_MS) {
        Serial.println(F("Кнопка удерживалась 5 секунд — выполняем функцию"));
        otpravitHttpKomandu("cleanspiff");
        Serial.println(F("На вторую палту передано сообщение очистки файлов в системе"));

        // Ждём отпускания, чтобы не сработало снова
        while (digitalRead(PIN_KNOPKA) == LOW) {
          podozhdat(10);
        }
        knopkaUderzhana = false;
      }
    }
    ///////////ПРОВЕРКА КНОПКИ СБРОСА/////////
    proveritWiFi();
    otpravkaMoiIP();
    priemPaketa();
    statusSvyazi();
    indikatorSostoyaniya();
    server.handleClient();
    ArduinoOTA.handle();  // обязательно!
    obrabotatTelegram();
  }

  // === Проверка Wi-Fi подключения ===
  void proveritWiFi() {
    if (millis() - poslednyayaProverkaWiFi < 10000) return;
    poslednyayaProverkaWiFi = millis();

    if (WiFi.status() == WL_CONNECTED) {
      wifiPodklyuchen = true;
      wifiRezhimTochki = false;
      return;
    }

    if (wifiRezhimTochki) {
      // УЖЕ в режиме точки доступа – ничего не делаем
      return;
    }

    Serial.print(F("Пытаюсь подключиться к: "));
    Serial.print(ssid);
    Serial.print(F(" / "));
    Serial.println(password);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
      podozhdat(500);
      Serial.print(F("."));
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiPodklyuchen = true;
      wifiRezhimTochki = false;
      Serial.print(F("\nWi-Fi ПОДКЛЮЧЕН! IP: ")); Serial.println(WiFi.localIP());
      zapustitpriemhttp();
      StartTelegramBot();

    } else {
      Serial.println(F("Не удалось подключиться. Включаю точку доступа..."));
      WiFi.mode(WIFI_AP);
      WiFi.softAP(imyaTochki, parolTochki);
      wifiPodklyuchen = false;
      wifiRezhimTochki = true;
      Serial.print(F("Точка доступа: ")); Serial.println(WiFi.softAPIP());
    }
  }


  // === Запуск веб-интерфейса ===
  void obrabotatGlavnuyuStranicu() {
    String html = "<html><head><meta charset='utf-8'><title>Настройка</title></head><body>";
    html += "<h2>Конфигурация ESP</h2><form method='POST' action='/sohranit'>";

    html += String(F("Wi-Fi SSID:<br><input name='ssid' value='")) + ssid + F("'><br>");
    html += String(F("Wi-Fi Пароль:<br><input name='pass' type='password' value='")) + password + F("'><br>");
    html += String(F("Telegram Token:<br><input name='token' value='")) + token_bot + F("'><br>");
    html += String(F("ID Главного Админа:<br><input name='admin_id' value='")) + glavniy_admin_id + F("'><br><br>");

    html += "<input type='submit' value='Сохранить и перезапустить'>";
    html += "</form></body></html>";

    server.send(200, "text/html", html);
  }

  void obrabotatSohranenie() {
    String new_ssid = server.arg("ssid");
    String new_pass = server.arg("pass");
    String new_token = server.arg("token");
    String new_admin = server.arg("admin_id");

    sohranitNastroiki(new_ssid, new_pass, new_token, new_admin);
    strlcpy(ssid, new_ssid.c_str(), sizeof(ssid));
    strlcpy(password, new_pass.c_str(), sizeof(password));
    strlcpy(token_bot, new_token.c_str(), sizeof(token_bot));
    strlcpy(glavniy_admin_id, new_admin.c_str(), sizeof(glavniy_admin_id));

    server.send(200, "text/html", F("<html><body><h3>Сохранено! Перезапуск через 3 сек...</h3></body></html>"));
    podozhdat(3000);
    ESP.restart();
  }


  // === Отправка IP по UDP ===
  void otpravkaMoiIP() {
    String prefiks = "HEAD;";
    IPAddress moiIP = wifiRezhimTochki ? WiFi.softAPIP() : WiFi.localIP();
    udp.beginPacket("255.255.255.255", portUDP);
    udp.print(prefiks + moiIP.toString());
    udp.endPacket();
  }

  // === Приём пакета ===
  void priemPaketa() {
    if (wifiPodklyuchen) {
      int razmer = udp.parsePacket();
      if (razmer) {
        char dannye[64];
        udp.read(dannye, sizeof(dannye));
        dannye[razmer] = '\0';

        String soobshenie = String(dannye);
        if (soobshenie.startsWith("RS;")) {
          String ipTekst = soobshenie.substring(3);
          ipDrugoyPlaty.fromString(ipTekst);
          ipNayden = true;
          vremyaPoslednegoOtklika = millis();
        }
      }
    }
  }

  void statusSvyazi() {
    platyVidyatDrugDruga = ipNayden && millis() - vremyaPoslednegoOtklika < vremyaTimeout;
  }
