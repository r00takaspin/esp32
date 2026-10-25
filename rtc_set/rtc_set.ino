// Установка времени в DS3231 по NTP (UTC)
#include <WiFi.h>
#include <time.h>
#include <Wire.h>

const char* WIFI_SSID = "Vault 23";
const char* WIFI_PASS = "01855081";

uint8_t decToBcd(uint8_t val) {
  return ((val / 10) << 4) | (val % 10);
}
uint8_t bcdToDec(uint8_t bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

void setDS3231(struct tm *t) {
  Wire.beginTransmission(0x68);
  Wire.write(0x00); // начать с регистра секунд
  Wire.write(decToBcd(t->tm_sec));
  Wire.write(decToBcd(t->tm_min));
  Wire.write(decToBcd(t->tm_hour)); // 24-часовой режим
  Wire.write(decToBcd(t->tm_wday == 0 ? 7 : t->tm_wday)); // день недели 1-7
  Wire.write(decToBcd(t->tm_mday));
  Wire.write(decToBcd(t->tm_mon + 1));
  Wire.write(decToBcd(t->tm_year - 100)); // год: 00-99
  Wire.endTransmission();
}

void readDS3231() {
  Wire.beginTransmission(0x68);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(0x68, 7);
  uint8_t sec = bcdToDec(Wire.read() & 0x7F);
  uint8_t mn  = bcdToDec(Wire.read());
  uint8_t hr  = bcdToDec(Wire.read() & 0x3F);
  Wire.read();
  uint8_t dy  = bcdToDec(Wire.read());
  uint8_t mo  = bcdToDec(Wire.read() & 0x1F);
  uint8_t yr  = bcdToDec(Wire.read());
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d:%02d", dy, mo, 2000 + yr, hr, mn, sec);
  Serial.println(buf);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  Serial.println("Подключение к WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 40) {
    delay(500); t++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi FAILED");
    return;
  }
  Serial.println("WiFi OK. Синхронизация NTP...");

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  int t2 = 0;
  while (time(nullptr) < 1000000000 && t2 < 40) {
    delay(500); t2++;
  }

  time_t now = time(nullptr);
  struct tm tm;
  gmtime_r(&now, &tm);

  Serial.printf("NTP время (UTC): %04d-%02d-%02d %02d:%02d:%02d\n",
    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  Serial.printf("Unix timestamp: %lu\n", (unsigned long)now);

  setDS3231(&tm);
  Serial.println("Записано в DS3231.");

  delay(200);
  Serial.print("Проверка (читаю из DS3231): ");
  readDS3231();
}

void loop() {}
