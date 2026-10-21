// LCD 1602A (I2C): 4 режима + автосон через 10 сек без нажатия BOOT
// Стартовый экран — сеть (IP/MAC/hostname, ротация), далее Unix/Ping/Система
// I2C: SDA = GPIO21, SCL = GPIO22, адрес 0x27
#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>
#include "ping/ping_sock.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "secrets.h" // WIFI_SSID / WIFI_PASS (файл в .gitignore)

const char* PING_HOST = "ya.ru";
const char* HOSTNAME = "esp32-lcd";

const int BOOT_PIN = 0; // GPIO0 — кнопка BOOT (активный LOW)

LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- режимы (0 — стартовый экран сети) ---
enum { MODE_NET = 0, MODE_UNIX = 1, MODE_PING = 2, MODE_SYS = 3, MODE_COUNT = 4 };
int mode = MODE_NET;

// --- сетевой экран (ротация IP/MAC/hostname) ---
int netPage = 0;
unsigned long lastNetPage = 0;
const unsigned long NET_PAGE_INTERVAL = 3000;

// --- unix time ---
unsigned long lastUpdate = 0;
char lastTs[16] = "";

// --- ping ---
volatile bool pingRunning = false;
volatile int pingMs = -1;
volatile bool pingResultReady = false;
int pingSuccessCount = 0;
int pingSuccessSum = 0;
unsigned long lastPingStart = 0;
const unsigned long PING_INTERVAL = 1000;
uint32_t pingTargetIp = 0;
char lastPingLine[16] = "";

// --- системная инфа ---
unsigned long lastSysUpdate = 0;
const unsigned long SYS_INTERVAL = 1000;
char lastSysLine[16] = "";
int lastSegments = -1;

// --- сон ---
unsigned long lastActivity = 0;
const unsigned long SLEEP_AFTER = 10000;

// --- кнопка ---
bool lastBtn = false;
bool btnReported = false;
unsigned long btnDebounce = 0;

// колбэки ping
static void on_ping_success(esp_ping_handle_t hdl, void *args) {
  uint32_t ms = 0;
  esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &ms, sizeof(ms));
  pingSuccessCount++;
  pingSuccessSum += (int)ms;
  pingMs = pingSuccessSum / pingSuccessCount;
}
static void on_ping_timeout(esp_ping_handle_t hdl, void *args) {}
static void on_ping_end(esp_ping_handle_t hdl, void *args) {
  if (pingSuccessCount == 0) pingMs = -2;
  pingSuccessCount = 0;
  pingSuccessSum = 0;
  pingRunning = false;
  pingResultReady = true;
  esp_ping_delete_session(hdl);
}

void startPing() {
  if (pingRunning || pingTargetIp == 0) return;
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.count = 3;
  config.timeout_ms = 1000;
  config.interval_ms = 200;
  config.target_addr.type = IPADDR_TYPE_V4;
  config.target_addr.u_addr.ip4.addr = pingTargetIp;
  esp_ping_callbacks_t cbs;
  cbs.on_ping_success = on_ping_success;
  cbs.on_ping_timeout = on_ping_timeout;
  cbs.on_ping_end = on_ping_end;
  cbs.cb_args = NULL;
  esp_ping_handle_t ping;
  if (esp_ping_new_session(&config, &cbs, &ping) == ESP_OK) {
    pingRunning = true;
    esp_ping_start(ping);
  }
}

void showPingResult(int ms) {
  char buf[16];
  if (ms >= 0) snprintf(buf, sizeof(buf), "%d ms", ms);
  else if (ms == -2) snprintf(buf, sizeof(buf), "timeout");
  else snprintf(buf, sizeof(buf), "...");

  int len = strlen(buf);
  int oldLen = strlen(lastPingLine);
  int changePos = 0;
  while (changePos < len && changePos < oldLen && lastPingLine[changePos] == buf[changePos]) changePos++;
  lcd.setCursor(changePos, 1);
  lcd.print(&buf[changePos]);
  if (len < oldLen) for (int i = len; i < oldLen; i++) lcd.print(" ");
  strcpy(lastPingLine, buf);
}

// --- экран сети (IP/MAC/hostname) ---
void showNetPage() {
  lcd.clear();
  if (netPage == 0) {
    lcd.setCursor(0, 0);
    lcd.print("IP address");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
  } else if (netPage == 1) {
    lcd.setCursor(0, 0);
    lcd.print("MAC");
    lcd.setCursor(0, 1);
    String mac = WiFi.macAddress();
    mac.replace(":", ""); // убираем двоеточия: 17 -> 12 символов
    lcd.print(mac);
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Hostname");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.getHostname());
  }
}

void drawNetScreen() {
  netPage = 0;
  lastNetPage = 0;
  showNetPage();
}

void drawUnixScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Unix time:");
  unsigned long now = (unsigned long)time(nullptr);
  snprintf(lastTs, sizeof(lastTs), "%lu", now);
  lcd.setCursor(0, 1);
  lcd.print(lastTs);
}

void drawPingScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ping ");
  lcd.print(PING_HOST);
  lastPingLine[0] = '\0';
  showPingResult(-1);
  pingMs = -1;
  pingResultReady = false;
  lastPingStart = millis();
  startPing();
}

void updateSysInfo(); // объявление вперёд

void drawSysScreen() {
  lcd.clear();
  lastSysLine[0] = '\0';
  lastSegments = -1;
  lastSysUpdate = 0;
  updateSysInfo();
}

void updateSysInfo() {
  int tempC = (int)(temperatureRead() + 0.5);
  int freq = (int)getCpuFrequencyMhz();
  uint32_t total = ESP.getHeapSize();
  uint32_t freeHeap = ESP.getFreeHeap();
  int usedPct = (int)(((total - freeHeap) * 100L) / total);

  char buf[16];
  snprintf(buf, sizeof(buf), "T:%dC %dM %d%%", tempC, freq, usedPct);

  int len = strlen(buf);
  int oldLen = strlen(lastSysLine);
  int changePos = 0;
  while (changePos < len && changePos < oldLen && lastSysLine[changePos] == buf[changePos]) changePos++;
  lcd.setCursor(changePos, 0);
  lcd.print(&buf[changePos]);
  if (len < oldLen) for (int i = len; i < oldLen; i++) lcd.print(" ");
  strcpy(lastSysLine, buf);

  int rssi = WiFi.RSSI();
  int segments = map(rssi, -90, -30, 0, 16);
  segments = constrain(segments, 0, 16);
  if (segments != lastSegments) {
    if (segments > lastSegments) {
      for (int i = (lastSegments < 0 ? 0 : lastSegments); i < segments; i++) {
        lcd.setCursor(i, 1);
        lcd.write((uint8_t)0xFF);
      }
    } else {
      for (int i = segments; i < lastSegments; i++) {
        lcd.setCursor(i, 1);
        lcd.write((uint8_t)' ');
      }
    }
    lastSegments = segments;
  }
}

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 30) {
    delay(500);
    t++;
  }
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  int t2 = 0;
  while (time(nullptr) < 1000000000 && t2 < 20) {
    delay(500);
    t2++;
  }
}

void goToSleep() {
  lcd.noBacklight();
  delay(50);
  rtc_gpio_pullup_en(GPIO_NUM_0);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  esp_light_sleep_start();

  // --- проснулись ---
  lastBtn = (digitalRead(BOOT_PIN) == LOW);
  btnReported = lastBtn;
  btnDebounce = millis();
  lastActivity = millis();

  Wire.begin(21, 22);
  ensureWifi();
  lcd.backlight();

  lastUpdate = 0;
  lastPingStart = 0;
  lastSysUpdate = 0;
  lastNetPage = 0; // обновить сетевой экран сразу
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  lcd.init();
  pinMode(BOOT_PIN, INPUT_PULLUP);
  lastActivity = millis();

  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  lcd.setCursor(0, 1);
  lcd.print("...");

  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 40) {
    delay(500);
    t++;
  }

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.setCursor(0, 0);
    lcd.print("WiFi OK");
    lcd.setCursor(0, 1);
    lcd.print("syncing NTP...");
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    int t2 = 0;
    while (time(nullptr) < 1000000000 && t2 < 40) {
      delay(500);
      t2++;
    }
    IPAddress ip;
    if (WiFi.hostByName(PING_HOST, ip)) {
      pingTargetIp = (uint32_t)ip;
    }
  } else {
    lcd.setCursor(0, 0);
    lcd.print("WiFi FAILED");
    lcd.setCursor(0, 1);
    lcd.print("check ssid/pass");
  }

  drawNetScreen(); // стартовый экран — сеть
}

void loop() {
  // 1) подсветка ~6% (ШИМ)
  lcd.backlight();
  delayMicroseconds(150);
  lcd.noBacklight();
  delayMicroseconds(2100);

  // 2) кнопка BOOT
  bool btn = (digitalRead(BOOT_PIN) == LOW);
  if (btn != lastBtn) {
    btnDebounce = millis();
    lastBtn = btn;
  }
  if (millis() - btnDebounce > 40) {
    if (btn && !btnReported) {
      btnReported = true;
      lastActivity = millis();
      mode = (mode + 1) % MODE_COUNT;
      if (mode == MODE_NET) drawNetScreen();
      else if (mode == MODE_UNIX) drawUnixScreen();
      else if (mode == MODE_PING) drawPingScreen();
      else drawSysScreen();
    }
    if (!btn) btnReported = false;
  }

  // 3) обновление экрана по режиму
  unsigned long nowMs = millis();

  if (mode == MODE_NET) {
    if (nowMs - lastNetPage >= NET_PAGE_INTERVAL) {
      lastNetPage = nowMs;
      netPage = (netPage + 1) % 3;
      showNetPage();
    }
  } else if (mode == MODE_UNIX) {
    if (nowMs - lastUpdate >= 1000) {
      lastUpdate = nowMs;
      unsigned long now = (unsigned long)time(nullptr);
      char buf[16];
      snprintf(buf, sizeof(buf), "%lu", now);
      int len = strlen(buf);
      int changePos = 0;
      while (changePos < len && lastTs[changePos] == buf[changePos]) changePos++;
      if (changePos < len) {
        lcd.setCursor(changePos, 1);
        lcd.print(&buf[changePos]);
      }
      strcpy(lastTs, buf);
    }
  } else if (mode == MODE_PING) {
    if (!pingRunning && (nowMs - lastPingStart >= PING_INTERVAL)) {
      lastPingStart = nowMs;
      startPing();
    }
    if (pingResultReady) {
      pingResultReady = false;
      showPingResult(pingMs);
    }
  } else { // MODE_SYS
    if (nowMs - lastSysUpdate >= SYS_INTERVAL) {
      lastSysUpdate = nowMs;
      updateSysInfo();
    }
  }

  // 4) автосон через 10 сек без нажатий
  if (millis() - lastActivity >= SLEEP_AFTER) {
    goToSleep();
  }
}
