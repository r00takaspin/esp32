// TOTP-генератор (2FA): DS3231 (время) + LCD 1602 (вывод кода) + KY-040.
// Время синхронизируется по Wi-Fi через ESP BLE Provisioning + NTP.
// Повторная настройка: удержание кнопки 3 секунды или удержание при старте.
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "mbedtls/md.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "totp_secrets.h" // твои секреты (в .gitignore)

LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- KY-040 ---
const int ENC_A_PIN = 32;   // CLK (A)
const int ENC_B_PIN = 33;   // DT  (B)
const int ENC_SW_PIN = 25;  // SW  (кнопка), RTC GPIO — может будить из сна

const int ENC_TICKS_PER_DETENT = 2; // под этот KY-040: 2 четверть-шага на один щелчок
const int ENCODER_DIRECTION = 1;    // поставь -1, если прокрутка идёт наоборот

const unsigned long SLEEP_AFTER = 30000;
const unsigned long DEBOUNCE_MS = 40;
const unsigned long LONG_PRESS_MS = 3000;

// --- тайминги Wi-Fi сценария ---
const unsigned long PROV_TIMEOUT = 600000;   // 10 минут на BLE-настройку
const unsigned long CONNECT_TIMEOUT = 15000; // ожидание IP
const unsigned long NTP_TIMEOUT = 12000;     // ожидание времени от NTP
const unsigned long RESULT_TIME = 4000;      // показываем коррекцию
const unsigned long ERROR_TIME = 10000;      // показываем ошибку
const unsigned long SETUP_REBOOT_MS = 900;   // пауза перед reboot в setup

int currentAccount = 0;
int currentNameLen = 0;

// --- энкодер ---
volatile int32_t encoderTicks = 0;
volatile uint8_t encoderPrevState = 0;
int32_t encoderConsumed = 0;

// --- кнопка энкодера ---
bool swLast = false;
bool swReported = false;
unsigned long swDebounce = 0;
unsigned long pressStart = 0;
bool longPressHandled = false;
unsigned long lastActivity = 0;

// --- тайминги обновления ---
unsigned long lastMarquee = 0;
unsigned long lastCode = 0;

// --- режим работы ---
enum AppMode {
  MODE_NORMAL,
  MODE_PROV,
  MODE_CONNECT,
  MODE_NTP,
  MODE_RESULT,
  MODE_ERROR,
  MODE_SETUP
};
AppMode mode = MODE_NORMAL;
unsigned long modeStart = 0;
long resultDelta = 0;

// --- песочные часы ---
uint8_t syncFrame = 0;
unsigned long syncFrameAt = 0;

uint8_t hg0[8] = {0x1F, 0x1F, 0x1F, 0x0E, 0x04, 0x04, 0x0E, 0x1F};
uint8_t hg1[8] = {0x1F, 0x1F, 0x0E, 0x04, 0x04, 0x04, 0x0E, 0x1F};
uint8_t hg2[8] = {0x1F, 0x0E, 0x04, 0x04, 0x04, 0x0A, 0x1F, 0x1F};
uint8_t hg3[8] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x1F, 0x1F};

// --- BLE provisioning ---
char provServiceName[20] = "PROV_TOTP";
char provPop[12] = "00000000";
volatile bool provStarted = false;
volatile bool provCredRecv = false;
volatile bool provSuccess = false;
volatile bool provFail = false;
volatile int provFailReason = 0;
volatile bool gotIP = false;
bool ntpStarted = false;

// --- NVS ---
Preferences prefs;
bool forceProv = false;
bool skipSync = false;
uint64_t lastSync = 0;

// --- ошибки ---
char errLine1[17] = "";
char errLine2[17] = "";

// Квадратурный декодер: индекс = (прошлое состояние << 2) | текущее,
// состояние = (A << 1) | B. Каждый корректный переход даёт ±1.
static const int8_t ENC_TABLE[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

void IRAM_ATTR encoderISR() {
  uint8_t state = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  encoderTicks += ENC_TABLE[(encoderPrevState << 2) | state];
  encoderPrevState = state;
}

// ============ DS3231 → Unix time ============
uint8_t bcdToDec(uint8_t bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

uint8_t decToBcd(uint8_t val) {
  return ((val / 10) << 4) | (val % 10);
}

void readDS3231(int &y, int &mo, int &d, int &h, int &mi, int &s) {
  Wire.beginTransmission(0x68);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(0x68, 7);
  s  = bcdToDec(Wire.read() & 0x7F);
  mi = bcdToDec(Wire.read());
  h  = bcdToDec(Wire.read() & 0x3F);
  Wire.read();
  d  = bcdToDec(Wire.read());
  mo = bcdToDec(Wire.read() & 0x1F);
  y  = bcdToDec(Wire.read()) + 2000;
}

long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

long currentUnix() {
  int y, mo, d, h, mi, s;
  readDS3231(y, mo, d, h, mi, s);
  return daysFromCivil(y, mo, d) * 86400L + h * 3600L + mi * 60L + s;
}

// Записать UTC-время в DS3231 (24-часовой режим, век 2000–2099).
bool writeDS3231Unix(time_t t) {
  struct tm tm;
  if (gmtime_r(&t, &tm) == nullptr) return false;

  Wire.beginTransmission(0x68);
  Wire.write(0x00);
  Wire.write(decToBcd(tm.tm_sec));
  Wire.write(decToBcd(tm.tm_min));
  Wire.write(decToBcd(tm.tm_hour));       // 24h
  Wire.write(decToBcd(tm.tm_wday + 1));   // 1..7
  Wire.write(decToBcd(tm.tm_mday));
  Wire.write(decToBcd(tm.tm_mon + 1));    // 1..12
  Wire.write(decToBcd(tm.tm_year % 100)); // 00..99
  return Wire.endTransmission() == 0;
}

// ============ base32 → bytes ============
int base32Decode(const char *b32, uint8_t *out, int maxLen) {
  int outLen = 0, bits = 0;
  uint32_t buffer = 0;
  for (const char *p = b32; *p; p++) {
    char c = *p;
    if (c == '=') break;
    int val;
    if (c >= 'A' && c <= 'Z') val = c - 'A';
    else if (c >= 'a' && c <= 'z') val = c - 'a';
    else if (c >= '2' && c <= '7') val = c - '2' + 26;
    else continue;
    buffer = (buffer << 5) | val;
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      if (outLen < maxLen) out[outLen++] = (buffer >> bits) & 0xFF;
    }
  }
  return outLen;
}

// ============ TOTP (RFC 6238, HMAC-SHA1) ============
uint32_t totpCode(const char *b32, uint64_t counter) {
  uint8_t secret[64];
  int secretLen = base32Decode(b32, secret, sizeof(secret));

  uint8_t msg[8];
  for (int i = 0; i < 8; i++) {
    msg[7 - i] = (counter >> (i * 8)) & 0xFF; // big-endian
  }

  uint8_t hmac[20];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1),
                  secret, secretLen, msg, 8, hmac);

  int offset = hmac[19] & 0x0F;
  uint32_t code = ((uint32_t)(hmac[offset] & 0x7F) << 24) |
                  ((uint32_t)hmac[offset + 1] << 16) |
                  ((uint32_t)hmac[offset + 2] << 8) |
                  (uint32_t)hmac[offset + 3];
  return code % 1000000;
}

// ============ экран ============
void lcdLine(uint8_t row, const char *s) {
  lcd.setCursor(0, row);
  int n = (int)strlen(s);
  if (n > 16) n = 16;
  for (int i = 0; i < n; i++) lcd.print(s[i]);
  for (int i = n; i < 16; i++) lcd.print(' ');
}

void updateNameLine() {
  const char *name = totpEntries[currentAccount].name;
  int len = strlen(name);
  lcd.setCursor(0, 0);

  if (len <= 16) {
    lcd.print(name);
    for (int i = len; i < 16; i++) lcd.print(" ");
  } else {
    int totalLen = len + 8;
    int pos = (millis() / 300) % totalLen;
    char buf[17];
    for (int i = 0; i < 16; i++) {
      int src = pos + i;
      buf[i] = (src < len) ? name[src] : ' ';
    }
    buf[16] = 0;
    lcd.print(buf);
  }
}

void updateCodeLine() {
  long unix = currentUnix();
  uint64_t counter = (uint64_t)unix / 30;
  uint32_t code = totpCode(totpEntries[currentAccount].secret, counter);

  int remaining = 30 - (int)(unix % 30);
  int segments = (remaining * 8) / 30;
  if (segments > 8) segments = 8;

  char buf[8];
  snprintf(buf, sizeof(buf), "%06lu", (unsigned long)code);
  lcd.setCursor(0, 1);
  lcd.print(buf);
  lcd.print(" ");
  for (int i = 0; i < 8; i++) {
    lcd.write((i < segments) ? (uint8_t)0xFF : (uint8_t)' ');
  }
}

void switchAccount(int steps, unsigned long now) {
  int next = (currentAccount + steps) % TOTP_COUNT;
  if (next < 0) next += TOTP_COUNT;
  currentAccount = next;
  currentNameLen = strlen(totpEntries[currentAccount].name);
  updateNameLine();
  updateCodeLine();
  lastCode = now;
  lastMarquee = now;
}

// ============ экраны синхронизации ============
void drawWait(const char *title, const char *subtitle) {
  lcdLine(0, title);
  lcd.setCursor(0, 1);
  for (int i = 0; i < 16; i++) lcd.print(' ');
  lcd.setCursor(0, 1);
  lcd.write((uint8_t)syncFrame);
  lcd.setCursor(2, 1);
  lcd.print(subtitle);
}

void drawProv(unsigned long now) {
  lcdLine(0, "WIFI SETUP");
  lcd.setCursor(0, 1);
  for (int i = 0; i < 16; i++) lcd.print(' ');
  lcd.setCursor(0, 1);
  lcd.write((uint8_t)syncFrame);
  lcd.setCursor(2, 1);
  if ((now / 2000) % 2 == 0) {
    char buf[17];
    snprintf(buf, sizeof(buf), "POP %s", provPop);
    lcd.print(buf);
  } else {
    lcd.print(provServiceName);
  }
}

void drawResult() {
  lcdLine(0, "RTC SYNCED");
  char buf[17];
  if (labs(resultDelta) > 99999) {
    snprintf(buf, sizeof(buf), "TIME SET");
  } else if (resultDelta == 0) {
    snprintf(buf, sizeof(buf), "CORR 0s");
  } else {
    snprintf(buf, sizeof(buf), "CORR %+lds", resultDelta);
  }
  lcdLine(1, buf);
}

void drawError() {
  lcdLine(0, errLine1);
  lcdLine(1, errLine2);
}

// ============ NVS ============
void loadPrefs() {
  prefs.begin("totp", false);
  forceProv = prefs.getBool("force_prov", false);
  skipSync = prefs.getBool("skip_sync", false);
  lastSync = prefs.getULong64("last_sync", 0);
  if (skipSync) prefs.putBool("skip_sync", false);
  prefs.end();
}

void saveForceProv(bool v) {
  prefs.begin("totp", false);
  prefs.putBool("force_prov", v);
  prefs.end();
  forceProv = v;
}

void saveSkipSync(bool v) {
  prefs.begin("totp", false);
  prefs.putBool("skip_sync", v);
  prefs.end();
  skipSync = v;
}

void saveLastSync(uint64_t v) {
  prefs.begin("totp", false);
  prefs.putULong64("last_sync", v);
  prefs.end();
  lastSync = v;
}

bool needWifiSync() {
  long rtc = currentUnix();
  if (rtc < 1600000000L) return true;        // RTC не установлен
  if (lastSync == 0) return true;
  if ((uint64_t)rtc < lastSync) return true; // RTC сбросили
  return ((uint64_t)rtc - lastSync) >= 86400ULL; // раз в сутки
}

// ============ BLE provisioning ============
void generateProvCredentials() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(provServiceName, sizeof(provServiceName),
           "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);

  uint32_t r = esp_random() % 100000000UL;
  snprintf(provPop, sizeof(provPop), "%08u", (unsigned)r);
}

void onWiFiEvent(arduino_event_t *event) {
  switch (event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      provStarted = true;
      break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
      provCredRecv = true;
      break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      provSuccess = true;
      break;
    case ARDUINO_EVENT_PROV_CRED_FAIL:
      provFail = true;
      provFailReason = event->event_info.prov_fail_reason;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      gotIP = true;
      break;
    default:
      break;
  }
}

void startProvisioning(bool reset) {
  generateProvCredentials();
  provStarted = false;
  provCredRecv = false;
  provSuccess = false;
  provFail = false;
  gotIP = false;

  mode = MODE_PROV;
  modeStart = millis();
  syncFrame = 0;
  syncFrameAt = modeStart;
  drawProv(modeStart);

  Serial.printf("BLE provisioning: %s  POP %s\n", provServiceName, provPop);

  WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE,
                          WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
                          WIFI_PROV_SECURITY_1,
                          provPop, provServiceName,
                          nullptr, nullptr, reset);
}

void startConnect() {
  generateProvCredentials();
  provStarted = false;
  provCredRecv = false;
  provSuccess = false;
  provFail = false;
  gotIP = false;

  mode = MODE_CONNECT;
  modeStart = millis();
  syncFrame = 0;
  syncFrameAt = modeStart;
  drawWait("WIFI CONNECT", "starting...");

  // Если credentials уже есть — подключимся; если нет — начнётся BLE-provisioning.
  WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE,
                          WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
                          WIFI_PROV_SECURITY_1,
                          provPop, provServiceName,
                          nullptr, nullptr, false);
}

void startNtp(unsigned long now) {
  mode = MODE_NTP;
  modeStart = now;
  ntpStarted = false;
  syncFrame = 0;
  syncFrameAt = now;
  drawWait("NTP SYNC", "wait...");
}

void finishNtp(unsigned long now) {
  time_t t = time(nullptr);
  long oldTs = currentUnix();
  resultDelta = (long)t - oldTs;

  if (!writeDS3231Unix(t)) {
    startError("RTC ERROR", "HOLD 3s SETUP");
    return;
  }

  saveLastSync((uint64_t)t);
  saveForceProv(false);
  saveSkipSync(false);

  mode = MODE_RESULT;
  modeStart = now;
  drawResult();
  Serial.printf("NTP OK, delta=%ld s\n", resultDelta);

  for (int i = 0; i < 2; i++) {
    lcd.noBacklight();
    delay(80);
    lcd.backlight();
    delay(80);
  }
}

// ============ режимы ============
void enterNormal(unsigned long now) {
  mode = MODE_NORMAL;
  modeStart = now;
  lastActivity = now;
  lastCode = now;
  lastMarquee = now;

  noInterrupts();
  encoderConsumed = encoderTicks;
  interrupts();

  swLast = (digitalRead(ENC_SW_PIN) == LOW);
  swReported = swLast;
  swDebounce = millis();
  longPressHandled = swLast;

  lcd.clear();
  updateNameLine();
  updateCodeLine();
}

void startSetupMode(unsigned long now) {
  saveForceProv(true);
  saveSkipSync(false);
  mode = MODE_SETUP;
  modeStart = now;
  lcd.clear();
  lcdLine(0, "SETUP MODE");
  lcdLine(1, "REBOOT...");
}

void cancelProvisioning() {
  saveSkipSync(true);
  saveForceProv(false);
  lcd.clear();
  lcdLine(0, "CANCEL SETUP");
  lcdLine(1, "REBOOT...");
  delay(400);
  ESP.restart();
}

void startError(const char *l1, const char *l2) {
  strncpy(errLine1, l1, 16); errLine1[16] = 0;
  strncpy(errLine2, l2, 16); errLine2[16] = 0;

  saveSkipSync(true);
  saveForceProv(false);

  mode = MODE_ERROR;
  modeStart = millis();
  drawError();
  Serial.printf("Sync error: %s / %s\n", errLine1, errLine2);
}

void processMode(unsigned long now) {
  switch (mode) {
    case MODE_PROV:
      if (now - syncFrameAt >= 250) {
        syncFrameAt = now;
        syncFrame = (syncFrame + 1) % 4;
        drawProv(now);
      }

      if (provCredRecv || provSuccess) {
        provCredRecv = false;
        provSuccess = false;
        provStarted = false;
        mode = MODE_CONNECT;
        modeStart = now;
        syncFrame = 0;
        syncFrameAt = now;
        drawWait("WIFI CONNECT", "credentials...");
      } else if (provFail) {
        provFail = false;
        startError(provFailReason == WIFI_PROV_STA_AUTH_ERROR ? "AUTH ERROR" : "WIFI FAILED",
                   "HOLD 3s SETUP");
      } else if (gotIP) {
        gotIP = false;
        startNtp(now);
      } else if (now - modeStart > PROV_TIMEOUT) {
        startError("SETUP TIMEOUT", "HOLD 3s SETUP");
      }
      break;

    case MODE_CONNECT:
      if (now - syncFrameAt >= 250) {
        syncFrameAt = now;
        syncFrame = (syncFrame + 1) % 4;
        drawWait("WIFI CONNECT", "wait...");
      }

      if (provStarted) {
        provStarted = false;
        mode = MODE_PROV;
        modeStart = now;
        syncFrame = 0;
        syncFrameAt = now;
        drawProv(now);
      } else if (provFail) {
        provFail = false;
        startError(provFailReason == WIFI_PROV_STA_AUTH_ERROR ? "AUTH ERROR" : "WIFI FAILED",
                   "HOLD 3s SETUP");
      } else if (gotIP) {
        gotIP = false;
        startNtp(now);
      } else if (now - modeStart > CONNECT_TIMEOUT) {
        startError("WIFI FAILED", "HOLD 3s SETUP");
      }
      break;

    case MODE_NTP:
      if (now - syncFrameAt >= 250) {
        syncFrameAt = now;
        syncFrame = (syncFrame + 1) % 4;
        drawWait("NTP SYNC", "wait...");
      }

      if (!ntpStarted) {
        ntpStarted = true;
        configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
      }
      if (time(nullptr) > 1600000000) {
        finishNtp(now);
      } else if (now - modeStart > NTP_TIMEOUT) {
        startError("NTP FAILED", "HOLD 3s SETUP");
      }
      break;

    case MODE_RESULT:
      if (now - modeStart > RESULT_TIME) {
        ESP.restart();
      }
      break;

    case MODE_ERROR:
      if (now - modeStart > ERROR_TIME) {
        ESP.restart();
      }
      break;

    case MODE_SETUP:
      if (now - modeStart > SETUP_REBOOT_MS) {
        ESP.restart();
      }
      break;

    case MODE_NORMAL:
      break;
  }
}

// ============ энкодер ============
void setupEncoder() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);
  pinMode(ENC_SW_PIN, INPUT_PULLUP);

  encoderPrevState = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B_PIN), encoderISR, CHANGE);

  // если кнопку держат при старте — не считаем это нажатием
  swLast = (digitalRead(ENC_SW_PIN) == LOW);
  swReported = swLast;
  swDebounce = millis();
  pressStart = millis();
  longPressHandled = swLast;
}

void processEncoder(unsigned long now) {
  noInterrupts();
  int32_t ticks = encoderTicks;
  interrupts();

  int32_t diff = ticks - encoderConsumed;
  if (diff >= ENC_TICKS_PER_DETENT || diff <= -ENC_TICKS_PER_DETENT) {
    int32_t rawSteps = diff / ENC_TICKS_PER_DETENT;
    encoderConsumed += rawSteps * ENC_TICKS_PER_DETENT;

    int steps = rawSteps * ENCODER_DIRECTION;
    if (steps != 0) {
      lastActivity = now;
      switchAccount(steps, now);
    }
  }
}

void processButton(unsigned long now) {
  bool sw = (digitalRead(ENC_SW_PIN) == LOW);

  if (sw != swLast) {
    swDebounce = now;
    swLast = sw;
  }

  if (now - swDebounce > DEBOUNCE_MS) {
    if (sw && !swReported) {
      swReported = true;
      pressStart = now;
      longPressHandled = false;
      lastActivity = now;
    }

    if (sw && swReported && !longPressHandled &&
        (now - pressStart >= LONG_PRESS_MS)) {
      longPressHandled = true;

      if (mode == MODE_NORMAL || mode == MODE_ERROR) {
        startSetupMode(now);
      } else if (mode == MODE_PROV) {
        cancelProvisioning();
      }
    }

    if (!sw && swReported) {
      swReported = false;
      if (!longPressHandled && mode == MODE_NORMAL) {
        lastActivity = now;
        switchAccount(1, now);
      }
      longPressHandled = false;
    }
  }
}

// ============ сон ============
void goToSleep() {
  lcd.noBacklight();
  delay(50);

  // SW — единственный источник пробуждения: кнопка замыкает пин на GND.
  rtc_gpio_pullup_en(GPIO_NUM_25);
  rtc_gpio_pulldown_dis(GPIO_NUM_25);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_25, 0);
  esp_light_sleep_start();

  // --- проснулись: вернуть SW в обычный цифровой режим ---
  rtc_gpio_deinit(GPIO_NUM_25);
  pinMode(ENC_SW_PIN, INPUT_PULLUP);

  noInterrupts();
  encoderPrevState = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  interrupts();

  swLast = (digitalRead(ENC_SW_PIN) == LOW);
  swReported = swLast;
  swDebounce = millis();
  pressStart = millis();
  longPressHandled = swLast;
  lastActivity = millis();

  Wire.begin(21, 22);
  lcd.backlight();
  lastCode = 0;
  lastMarquee = 0;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, hg0);
  lcd.createChar(1, hg1);
  lcd.createChar(2, hg2);
  lcd.createChar(3, hg3);

  setupEncoder();
  loadPrefs();
  WiFi.onEvent(onWiFiEvent);

  currentNameLen = strlen(totpEntries[currentAccount].name);
  lastActivity = millis();

  // Удержание кнопки при включении — аварийный вход в настройку.
  bool bootHold = (digitalRead(ENC_SW_PIN) == LOW);
  if (bootHold) {
    lcd.clear();
    lcdLine(0, "SETUP MODE");
    lcdLine(1, "HOLD...");
    unsigned long t0 = millis();
    while (digitalRead(ENC_SW_PIN) == LOW && millis() - t0 < 1200) {
      delay(20);
    }
    bootHold = (digitalRead(ENC_SW_PIN) == LOW);
  }

  if (forceProv || bootHold) {
    saveForceProv(true);
    saveSkipSync(false);
    startProvisioning(true);
  } else if (skipSync) {
    enterNormal(millis());
  } else if (needWifiSync()) {
    startConnect();
  } else {
    enterNormal(millis());
  }
}

void loop() {
  unsigned long now = millis();

  if (mode == MODE_NORMAL) {
    processEncoder(now);
  }
  processButton(now);
  processMode(now);
  now = millis();

  if (mode == MODE_NORMAL) {
    if (currentNameLen > 16 && now - lastMarquee >= 300) {
      lastMarquee = now;
      updateNameLine();
    }

    if (now - lastCode >= 1000) {
      lastCode = now;
      updateCodeLine();
    }

    if (now - lastActivity >= SLEEP_AFTER) {
      if (digitalRead(ENC_SW_PIN) == HIGH) {
        goToSleep();
      } else {
        lastActivity = now;
      }
    }
  } else {
    lastActivity = now;
  }
}
