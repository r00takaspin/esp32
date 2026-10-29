// TOTP-генератор (2FA): DS3231 + LCD 1602 + KY-040.
// Аккаунты хранятся в NVS и настраиваются через SoftAP-портал.
// Wi-Fi/NTP — опционально; секреты нигде не показываются.
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "mbedtls/md.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_sntp.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- KY-040 ---
const int ENC_A_PIN = 32;
const int ENC_B_PIN = 33;
const int ENC_SW_PIN = 25;
const int ENC_TICKS_PER_DETENT = 2;
const int ENCODER_DIRECTION = 1;

const unsigned long SLEEP_AFTER = 60000;
const unsigned long DEBOUNCE_MS = 40;
const unsigned long LONG_PRESS_MS = 3000;
const unsigned long MENU_TIMEOUT = 15000;
const unsigned long AP_TIMEOUT = 0;
const unsigned long CONNECT_TIMEOUT = 15000;
const unsigned long NTP_TIMEOUT = 12000;
const unsigned long RESULT_TIME = 4000;
const unsigned long ERROR_TIME = 5000;

const int MAX_ACCOUNTS = 16;

struct Account {
  char name[33];
  char secret[65];
};

Account accounts[MAX_ACCOUNTS];
int accountCount = 0;
int currentAccount = 0;
RTC_DATA_ATTR int savedAccount = 0;

char wifiSsid[33] = "";
char wifiPass[65] = "";
char ntpServer[64] = "pool.ntp.org";
bool ntpAuto = true;
uint64_t lastSync = 0;

Preferences prefs;
WebServer server(80);
DNSServer dnsServer;
char apPass[9] = "";
const char *AP_SSID = "TOTP-Setup";

// --- энкодер ---
volatile int32_t encoderTicks = 0;
volatile uint8_t encoderPrevState = 0;
int32_t encoderConsumed = 0;

// --- кнопка ---
bool swLast = false;
bool swReported = false;
unsigned long swDebounce = 0;
unsigned long pressStart = 0;
bool longPressHandled = false;
unsigned long lastActivity = 0;

unsigned long lastMarquee = 0;
int marqueePos = 0;
unsigned long lastCode = 0;

// --- режимы ---
enum AppMode {
  MODE_NORMAL,
  MODE_MENU,
  MODE_ACCOUNTS,
  MODE_CONFIRM_DELETE,
  MODE_CONFIRM_DELETE_ALL,
  MODE_SETUP_AP,
  MODE_SYNC_CONNECT,
  MODE_SYNC_NTP,
  MODE_SYNC_RESULT,
  MODE_SYNC_ERROR,
  MODE_STATUS
};

AppMode mode = MODE_NORMAL;
unsigned long modeStart = 0;
int menuIndex = 0;
int accountIndex = 0;
int statusPage = 0;
bool confirmAll = false;
int confirmIndex = 0;
bool syncManual = false;
bool errorReturnMenu = false;
long resultDelta = 0;
bool ntpStarted = false;
volatile bool ntpSynced = false;
uint8_t syncFrame = 0;
unsigned long syncFrameAt = 0;

bool apReturnMenu = false;
unsigned long apLastActivity = 0;
unsigned long apLastDraw = 0;
bool apPhase = false;

char errLine1[17] = "";
char errLine2[17] = "";

uint8_t hg0[8] = {0x1F, 0x1F, 0x1F, 0x0E, 0x04, 0x04, 0x0E, 0x1F};
uint8_t hg1[8] = {0x1F, 0x1F, 0x0E, 0x04, 0x04, 0x04, 0x0E, 0x1F};
uint8_t hg2[8] = {0x1F, 0x0E, 0x04, 0x04, 0x04, 0x0A, 0x1F, 0x1F};
uint8_t hg3[8] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x1F, 0x1F};

const char *MENU_ITEMS[] = {"Sync time", "Accounts", "Setup", "Status", "Back"};
const int MENU_COUNT = 5;

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

// прототипы
void enterNormal(unsigned long now);
void enterMenu(unsigned long now);
void enterAccounts(unsigned long now);
void startSetupAP(bool returnMenu);
void startSync(bool manual);
void startSyncError(const char *l1, const char *l2, bool returnMenu);

// ============ DS3231 ============
uint8_t bcdToDec(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }
uint8_t decToBcd(uint8_t val) { return ((val / 10) << 4) | (val % 10); }

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

bool writeDS3231Unix(time_t t) {
  struct tm tm;
  if (gmtime_r(&t, &tm) == nullptr) return false;
  Wire.beginTransmission(0x68);
  Wire.write(0x00);
  Wire.write(decToBcd(tm.tm_sec));
  Wire.write(decToBcd(tm.tm_min));
  Wire.write(decToBcd(tm.tm_hour));
  Wire.write(decToBcd(tm.tm_wday + 1));
  Wire.write(decToBcd(tm.tm_mday));
  Wire.write(decToBcd(tm.tm_mon + 1));
  Wire.write(decToBcd(tm.tm_year % 100));
  return Wire.endTransmission() == 0;
}

// ============ base32 / TOTP ============
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

uint32_t totpCode(const char *b32, uint64_t counter) {
  uint8_t secret[64];
  int secretLen = base32Decode(b32, secret, sizeof(secret));
  uint8_t msg[8];
  for (int i = 0; i < 8; i++) msg[7 - i] = (counter >> (i * 8)) & 0xFF;
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

// ============ LCD ============
void lcdLine(uint8_t row, const char *s) {
  lcd.setCursor(0, row);
  int n = (int)strlen(s);
  if (n > 16) n = 16;
  for (int i = 0; i < n; i++) lcd.print(s[i]);
  for (int i = n; i < 16; i++) lcd.print(' ');
}

void updateNameLine() {
  if (accountCount == 0) {
    lcdLine(0, "NO ACCOUNTS");
    return;
  }
  if (currentAccount < 0 || currentAccount >= accountCount) currentAccount = 0;

  const char *name = accounts[currentAccount].name;
  int len = strlen(name);
  lcd.setCursor(0, 0);
  if (len <= 16) {
    lcd.print(name);
    for (int i = len; i < 16; i++) lcd.print(" ");
  } else {
    int totalLen = len + 1;
    int pos = marqueePos % totalLen;
    char buf[17];
    for (int i = 0; i < 16; i++) {
      int src = (pos + i) % totalLen;
      buf[i] = (src < len) ? name[src] : ' ';
    }
    buf[16] = 0;
    lcd.print(buf);
  }
}

void updateCodeLine() {
  if (accountCount == 0) {
    lcdLine(1, "HOLD 3s MENU");
    return;
  }
  long unix = currentUnix();
  if (unix < 1600000000L) {
    lcdLine(1, "NO TIME  MENU");
    return;
  }

  uint64_t counter = (uint64_t)unix / 30;
  uint32_t code = totpCode(accounts[currentAccount].secret, counter);
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
  if (accountCount == 0) return;
  int next = (currentAccount + steps) % accountCount;
  if (next < 0) next += accountCount;
  currentAccount = next;
  savedAccount = currentAccount;
  marqueePos = 0;
  lastMarquee = now;
  updateNameLine();
  updateCodeLine();
  lastCode = now;
}

// ============ NVS ============
void onNtpSync(struct timeval *tv) {
  (void)tv;
  ntpSynced = true;
}

bool normalizeBase32(const char *in, char *out, size_t outLen) {
  if (!in || !out || outLen < 2) return false;

  char tmp[256];
  strncpy(tmp, in, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;

  const char *src = tmp;
  const char *p = strstr(tmp, "secret=");
  if (p) {
    src = p + 7;
    char *amp = strchr((char *)src, '&');
    if (amp) *amp = 0;
  }

  size_t n = 0;
  for (const char *q = src; *q; q++) {
    char c = *q;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-') continue;
    if (c == '=') break;
    if (c >= 'a' && c <= 'z') c -= 32;
    if (!((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'))) return false;
    if (n + 1 >= outLen) return false;
    out[n++] = c;
  }
  out[n] = 0;
  return n >= 16 && n <= 64;
}

bool validAccountName(const char *name) {
  if (!name) return false;
  int n = strlen(name);
  if (n < 1 || n > 32) return false;
  for (int i = 0; i < n; i++) {
    if ((unsigned char)name[i] < 0x20) return false;
  }
  return true;
}

void saveAccounts() {
  prefs.begin("totp", false);
  prefs.putUChar("acc_count", (uint8_t)accountCount);
  if (accountCount > 0) {
    prefs.putBytes("acc_data", accounts, accountCount * sizeof(Account));
  } else {
    prefs.remove("acc_data");
  }
  prefs.end();
}

void saveSettings() {
  prefs.begin("totp", false);
  prefs.putString("wifi_ssid", wifiSsid);
  prefs.putString("wifi_pass", wifiPass);
  prefs.putString("ntp_server", ntpServer);
  prefs.putBool("ntp_auto", ntpAuto);
  prefs.end();
}

void saveLastSync(uint64_t v) {
  prefs.begin("totp", false);
  prefs.putULong64("last_sync", v);
  prefs.end();
  lastSync = v;
}

void loadSettings() {
  wifiSsid[0] = 0;
  wifiPass[0] = 0;
  ntpServer[0] = 0;

  prefs.begin("totp", true);
  accountCount = prefs.getUChar("acc_count", 0);
  if (accountCount < 0 || accountCount > MAX_ACCOUNTS) accountCount = 0;

  size_t len = prefs.getBytesLength("acc_data");
  if (accountCount > 0 && len == accountCount * sizeof(Account)) {
    prefs.getBytes("acc_data", accounts, len);
  } else {
    accountCount = 0;
  }

  prefs.getString("wifi_ssid", wifiSsid, sizeof(wifiSsid));
  prefs.getString("wifi_pass", wifiPass, sizeof(wifiPass));
  prefs.getString("ntp_server", ntpServer, sizeof(ntpServer));
  ntpAuto = prefs.getBool("ntp_auto", true);
  lastSync = prefs.getULong64("last_sync", 0);
  prefs.end();

  if (ntpServer[0] == 0) snprintf(ntpServer, sizeof(ntpServer), "pool.ntp.org");

  int n = 0;
  for (int i = 0; i < accountCount; i++) {
    accounts[i].name[32] = 0;
    accounts[i].secret[64] = 0;
    char norm[65];
    if (validAccountName(accounts[i].name) &&
        normalizeBase32(accounts[i].secret, norm, sizeof(norm))) {
      if (n != i) accounts[n] = accounts[i];
      strncpy(accounts[n].secret, norm, sizeof(accounts[n].secret) - 1);
      accounts[n].secret[sizeof(accounts[n].secret) - 1] = 0;
      n++;
    }
  }
  if (n != accountCount) {
    accountCount = n;
    saveAccounts();
  }
  if (currentAccount >= accountCount) currentAccount = 0;
}

bool addAccount(const char *name, const char *secret) {
  if (accountCount >= MAX_ACCOUNTS) return false;
  if (!validAccountName(name)) return false;

  char norm[65];
  if (!normalizeBase32(secret, norm, sizeof(norm))) return false;

  strncpy(accounts[accountCount].name, name, sizeof(accounts[accountCount].name) - 1);
  accounts[accountCount].name[sizeof(accounts[accountCount].name) - 1] = 0;
  strncpy(accounts[accountCount].secret, norm, sizeof(accounts[accountCount].secret) - 1);
  accounts[accountCount].secret[sizeof(accounts[accountCount].secret) - 1] = 0;
  accountCount++;
  if (accountCount == 1) currentAccount = 0;
  savedAccount = currentAccount;
  saveAccounts();
  return true;
}

void deleteAccountAt(int idx) {
  if (idx < 0 || idx >= accountCount) return;
  for (int i = idx; i < accountCount - 1; i++) accounts[i] = accounts[i + 1];
  accountCount--;
  if (currentAccount >= accountCount) currentAccount = 0;
  savedAccount = currentAccount;
  saveAccounts();
}

void deleteAllAccounts() {
  accountCount = 0;
  currentAccount = 0;
  savedAccount = 0;
  saveAccounts();
}

bool needSync() {
  long rtc = currentUnix();
  if (rtc < 1600000000L) return true;
  if (lastSync == 0) return true;
  if ((uint64_t)rtc < lastSync) return true;
  return ((uint64_t)rtc - lastSync) >= (30ULL * 86400ULL);
}

void flushEncoder() {
  noInterrupts();
  encoderConsumed = encoderTicks;
  interrupts();
}

// ============ меню / аккаунты ============
int wrapIndex(int v, int n) {
  if (n <= 0) return 0;
  v %= n;
  if (v < 0) v += n;
  return v;
}

void drawMenu() {
  char line[17];
  snprintf(line, sizeof(line), "MENU %d/%d", menuIndex + 1, MENU_COUNT);
  lcdLine(0, line);
  snprintf(line, sizeof(line), "> %s", MENU_ITEMS[menuIndex]);
  lcdLine(1, line);
}

void enterMenu(unsigned long now) {
  mode = MODE_MENU;
  menuIndex = 0;
  lastActivity = now;
  flushEncoder();
  drawMenu();
}

void drawAccounts() {
  if (accountCount == 0) {
    lcdLine(0, "NO ACCOUNTS");
    lcdLine(1, "HOLD 3s MENU");
    return;
  }
  int total = accountCount + 1;
  char line[17];
  snprintf(line, sizeof(line), "ACC %d/%d", accountIndex + 1, total);
  lcdLine(0, line);
  if (accountIndex < accountCount) {
    snprintf(line, sizeof(line), "> %s", accounts[accountIndex].name);
    lcdLine(1, line);
  } else {
    lcdLine(1, "> DELETE ALL");
  }
}

void enterAccounts(unsigned long now) {
  mode = MODE_ACCOUNTS;
  accountIndex = 0;
  lastActivity = now;
  flushEncoder();
  drawAccounts();
}

void drawConfirm() {
  lcdLine(0, confirmAll ? "DELETE ALL?" : "DELETE?");
  lcdLine(1, confirmIndex == 0 ? "> NO" : "> YES");
}

void enterConfirm(bool all, unsigned long now) {
  confirmAll = all;
  confirmIndex = 0;
  mode = all ? MODE_CONFIRM_DELETE_ALL : MODE_CONFIRM_DELETE;
  lastActivity = now;
  flushEncoder();
  drawConfirm();
}

void confirmDelete() {
  if (confirmIndex == 0) {
    enterAccounts(millis());
    return;
  }
  if (confirmAll) {
    deleteAllAccounts();
  } else {
    deleteAccountAt(accountIndex);
  }
  enterAccounts(millis());
}

void drawStatus() {
  char l1[17], l2[17];
  long rtc = currentUnix();
  if (statusPage == 0) {
    if (rtc < 1600000000L) {
      lcdLine(0, "RTC NOT SET");
      lcdLine(1, "USE MENU SYNC");
    } else {
      time_t t = (time_t)rtc;
      struct tm tm;
      gmtime_r(&t, &tm);
      char date[12], tim[16];
      strftime(date, sizeof(date), "%Y-%m-%d", &tm);
      strftime(tim, sizeof(tim), "%H:%M:%S UTC", &tm);
      snprintf(l1, sizeof(l1), "RTC %s", date);
      lcdLine(0, l1);
      lcdLine(1, tim);
    }
  } else {
    char age[10];
    if (lastSync == 0) {
      snprintf(age, sizeof(age), "never");
    } else {
      long d = rtc - (long)lastSync;
      if (d < 0) d = 0;
      if (d < 3600) snprintf(age, sizeof(age), "%ldm", d / 60);
      else if (d < 86400) snprintf(age, sizeof(age), "%ldh", d / 3600);
      else snprintf(age, sizeof(age), "%ldd", d / 86400);
    }
    snprintf(l1, sizeof(l1), "SYNC %s", age);
    snprintf(l2, sizeof(l2), "ACC %d  %s", accountCount, wifiSsid[0] ? "WIFI OK" : "NO WIFI");
    lcdLine(0, l1);
    lcdLine(1, l2);
  }
}

void enterStatus(unsigned long now) {
  mode = MODE_STATUS;
  statusPage = 0;
  lastActivity = now;
  flushEncoder();
  drawStatus();
}

void selectMenu() {
  switch (menuIndex) {
    case 0: startSync(true); break;
    case 1: enterAccounts(millis()); break;
    case 2: startSetupAP(true); break;
    case 3: enterStatus(millis()); break;
    case 4: enterNormal(millis()); break;
  }
}

void selectAccount() {
  if (accountCount == 0) {
    startSetupAP(true);
    return;
  }
  if (accountIndex < accountCount) {
    enterConfirm(false, millis());
  } else {
    enterConfirm(true, millis());
  }
}

void enterNormal(unsigned long now) {
  mode = MODE_NORMAL;
  lastActivity = now;
  lastCode = now;
  lastMarquee = now;
  marqueePos = 0;
  savedAccount = currentAccount;
  flushEncoder();

  swLast = (digitalRead(ENC_SW_PIN) == LOW);
  swReported = swLast;
  swDebounce = millis();
  longPressHandled = swLast;

  lcd.clear();
  updateNameLine();
  updateCodeLine();
}

// ============ NTP ============
void drawSyncWait(const char *phase, unsigned long now) {
  lcdLine(0, "NTP SYNC");
  lcd.setCursor(0, 1);
  for (int i = 0; i < 16; i++) lcd.print(' ');
  lcd.setCursor(0, 1);
  lcd.write((uint8_t)syncFrame);
  lcd.setCursor(2, 1);
  if ((now / 2000) % 2 == 0) lcd.print(phase);
  else lcd.print("PRESS=SKIP");
}

void updateSyncAnimation(const char *phase, unsigned long now) {
  if (now - syncFrameAt >= 250) {
    syncFrameAt = now;
    syncFrame = (syncFrame + 1) % 4;
    drawSyncWait(phase, now);
  }
}

void drawResult() {
  lcdLine(0, "RTC SYNCED");
  char buf[17];
  if (labs(resultDelta) > 99999) snprintf(buf, sizeof(buf), "TIME SET");
  else if (resultDelta == 0) snprintf(buf, sizeof(buf), "CORR 0s");
  else snprintf(buf, sizeof(buf), "CORR %+lds", resultDelta);
  lcdLine(1, buf);
}

void drawSyncError() {
  lcdLine(0, errLine1);
  lcdLine(1, errLine2);
}

void startSync(bool manual) {
  syncManual = manual;
  ntpSynced = false;

  if (wifiSsid[0] == 0) {
    startSyncError("NO WIFI", "OPEN SETUP", manual);
    return;
  }

  ntpStarted = false;
  mode = MODE_SYNC_CONNECT;
  modeStart = millis();
  syncFrame = 0;
  syncFrameAt = modeStart;
  flushEncoder();

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPass);
  drawSyncWait("connecting...", modeStart);
}

void finishSync(unsigned long now) {
  time_t t = time(nullptr);
  if (t < 1600000000) {
    startSyncError("NTP BAD", "RETRY", syncManual);
    return;
  }
  long oldTs = currentUnix();
  resultDelta = (long)t - oldTs;

  if (!writeDS3231Unix(t)) {
    startSyncError("RTC ERROR", "CHECK RTC", syncManual);
    return;
  }

  saveLastSync((uint64_t)t);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);

  mode = MODE_SYNC_RESULT;
  modeStart = now;
  drawResult();

  for (int i = 0; i < 2; i++) {
    lcd.noBacklight();
    delay(80);
    lcd.backlight();
    delay(80);
  }
}

void skipSync() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  startSyncError("SKIPPED", "OFFLINE", syncManual);
}

void startSyncError(const char *l1, const char *l2, bool returnMenu) {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);

  strncpy(errLine1, l1, 16); errLine1[16] = 0;
  strncpy(errLine2, l2, 16); errLine2[16] = 0;

  errorReturnMenu = returnMenu;
  mode = MODE_SYNC_ERROR;
  modeStart = millis();
  drawSyncError();
}

void processSync(unsigned long now) {
  switch (mode) {
    case MODE_SYNC_CONNECT:
      updateSyncAnimation("connecting...", now);
      if (WiFi.status() == WL_CONNECTED) {
        mode = MODE_SYNC_NTP;
        modeStart = now;
        syncFrame = 0;
        syncFrameAt = now;
        drawSyncWait("waiting...", now);
      } else if (now - modeStart > CONNECT_TIMEOUT) {
        startSyncError("WIFI FAILED", "CHECK SETUP", syncManual);
      }
      break;

    case MODE_SYNC_NTP:
      updateSyncAnimation("waiting...", now);
      if (!ntpStarted) {
        ntpStarted = true;
        configTime(0, 0, ntpServer, "time.cloudflare.com");
        sntp_restart();
      }
      if (ntpSynced) {
        finishSync(now);
      } else if (now - modeStart > NTP_TIMEOUT) {
        startSyncError("NTP FAILED", "BACK...", syncManual);
      }
      break;

    case MODE_SYNC_RESULT:
      if (now - modeStart > RESULT_TIME) enterNormal(now);
      break;

    case MODE_SYNC_ERROR:
      if (now - modeStart > ERROR_TIME) {
        if (errorReturnMenu) enterMenu(now);
        else enterNormal(now);
      }
      break;

    default:
      break;
  }
}

// ============ SoftAP portal ============
String htmlEscape(const char *s) {
  String out;
  if (!s) return out;
  for (const char *p = s; *p; p++) {
    switch (*p) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += *p; break;
    }
  }
  return out;
}

String rtcText() {
  long now = currentUnix();
  if (now < 1600000000L) return String("не установлено");
  time_t t = (time_t)now;
  struct tm tm;
  gmtime_r(&t, &tm);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm);
  return String(buf);
}

String lastSyncText() {
  if (lastSync == 0) return String("никогда");
  time_t t = (time_t)lastSync;
  struct tm tm;
  gmtime_r(&t, &tm);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm);
  return String(buf);
}

String buildPage() {
  String page;
  page.reserve(10000);

  page += F("<!doctype html><html lang=ru><head><meta charset=utf-8>");
  page += F("<meta name=viewport content='width=device-width,initial-scale=1'>");
  page += F("<title>TOTP Setup</title><style>");
  page += F("body{font-family:system-ui,Arial;max-width:640px;margin:20px auto;padding:0 14px;background:#f8fafc;color:#0f172a}");
  page += F("h1{font-size:24px}h2{font-size:18px;margin-top:24px}");
  page += F("label{display:block;margin:10px 0}input{width:100%;padding:10px;border:1px solid #cbd5e1;border-radius:8px;box-sizing:border-box}");
  page += F("button{padding:10px 16px;border:0;border-radius:8px;background:#2563eb;color:#fff;font-size:15px;margin-top:8px}");
  page += F(".danger{background:#dc2626}.primary{background:#0d9488}");
  page += F(".ok{background:#dcfce7;color:#166534;padding:10px;border-radius:8px}");
  page += F(".err{background:#fee2e2;color:#991b1b;padding:10px;border-radius:8px}");
  page += F(".status{background:#e2e8f0;padding:10px;border-radius:8px;font-size:14px}");
  page += F("ul{list-style:none;padding:0}li{display:flex;justify-content:space-between;align-items:center;gap:8px;border-bottom:1px solid #e2e8f0;padding:8px 0}");
  page += F("li form{margin:0}.muted{color:#64748b}</style></head><body>");
  page += F("<h1>Настройка TOTP</h1>");

  String ok = server.arg("ok");
  String err = server.arg("err");
  if (ok == "saved") page += F("<div class=ok>Настройки сохранены. Нажми «Применить и перезагрузить».</div>");
  else if (ok == "added") page += F("<div class=ok>Аккаунт добавлен.</div>");
  else if (ok == "deleted") page += F("<div class=ok>Аккаунт удалён.</div>");
  else if (ok == "deleted_all") page += F("<div class=ok>Все аккаунты удалены.</div>");
  else if (ok == "imported") { page += F("<div class=ok>Импортировано аккаунтов: "); page += server.arg("n"); page += F("</div>"); }
  if (err == "account") page += F("<div class=err>Неверный аккаунт: имя 1–32 символа, секрет base32 16–64 символа.</div>");
  else if (err == "ssid") page += F("<div class=err>Нужен SSID, если включена автосинхронизация NTP.</div>");
  else if (err == "pass") page += F("<div class=err>Для новой сети нужен пароль.</div>");
  else if (err == "ntp") page += F("<div class=err>Слишком длинное имя NTP-сервера.</div>");
  else if (err == "index") page += F("<div class=err>Неверный индекс аккаунта.</div>");

  page += F("<p class=muted>Секреты нигде не отображаются. Пароль Wi-Fi можно оставить пустым, чтобы не менять. Аккаунты сохраняются сразу; Wi-Fi/NTP — кнопкой «Сохранить и перезагрузить».</p>");

  page += F("<h2>Wi-Fi и NTP</h2><form method=post action=/save>");
  page += F("<label>SSID<input name=ssid maxlength=32 value='");
  page += htmlEscape(wifiSsid);
  page += F("'></label>");
  page += F("<label>Пароль Wi-Fi<input type=password name=pass maxlength=64 autocomplete=off placeholder='оставь пустым, чтобы не менять'></label>");
  page += F("<label>NTP-сервер<input name=ntp_server maxlength=63 value='");
  page += htmlEscape(ntpServer);
  page += F("'></label>");
  page += F("<label><input type=checkbox name=ntp_auto value=1");
  if (ntpAuto) page += F(" checked");
  page += F("> Автосинхронизация NTP</label><button name=save value=1>Сохранить</button><button name=reboot value=1>Сохранить и перезагрузить</button></form>");

  page += F("<p class=status>RTC: ");
  page += rtcText();
  page += F("<br>Последняя синхронизация: ");
  page += lastSyncText();
  page += F("</p>");

  page += F("<h2>Аккаунты</h2><ul>");
  for (int i = 0; i < accountCount; i++) {
    page += F("<li><span>");
    page += htmlEscape(accounts[i].name);
    page += F("</span><form method=post action=/delete><input type=hidden name=index value=");
    page += String(i);
    page += F("><button class=danger>Удалить</button></form></li>");
  }
  if (accountCount == 0) page += F("<li class=muted>Пока нет аккаунтов</li>");
  page += F("</ul>");

  page += F("<form method=post action=/add>");
  page += F("<label>Имя<input name=name maxlength=32 required></label>");
  page += F("<label>Секрет (base32 или otpauth://)<input type=password name=secret maxlength=4096 required autocomplete=off></label>");
  page += F("<button>Добавить</button></form>");

  page += F("<form method=post action=/delete_all onsubmit=\"return confirm('Удалить все аккаунты?')\">");
  page += F("<button class=danger>Удалить все</button></form>");

  page += F("<h2>Импорт из Google Authenticator</h2>");
  page += F("<p class=muted>Вставь otpauth-migration://... — все поддерживаемые аккаунты добавятся сразу. Поле маскируется.</p>");
  page += F("<input type=password id=mig maxlength=4096 autocomplete=off placeholder='otpauth-migration://...'>");
  page += F("<button type=button id=importBtn>Импортировать</button>");

  page += F("<form method=post action=/reboot><button class=primary>Применить и перезагрузить</button></form>");

  page += F(R"JS(<script>
function b64d(s){s=s.replace(/-/g,'+').replace(/_/g,'/');while(s.length%4)s+='=';const bin=atob(s);const out=new Uint8Array(bin.length);for(let i=0;i<bin.length;i++)out[i]=bin.charCodeAt(i);return out;}
function vr(b,p){let r=0,sh=1;while(p<b.length){const x=b[p++];r+=(x&127)*sh;if(!(x&128))return [r,p];sh*=128;}return [r,p];}
function parseOtp(b){let secret=null,name='',issuer='',alg=0,digits=0,type=0,counter=0,p=0;while(p<b.length){let t;[t,p]=vr(b,p);const f=t>>3,w=t&7;if(w===0){let v;[v,p]=vr(b,p);if(f===4)alg=v;else if(f===5)digits=v;else if(f===6)type=v;else if(f===7)counter=v;}else if(w===2){let l;[l,p]=vr(b,p);const d=b.slice(p,p+l);p+=l;if(f===1)secret=d;else if(f===2)name=new TextDecoder().decode(d);else if(f===3)issuer=new TextDecoder().decode(d);}else if(w===1)p+=8;else if(w===5)p+=4;else break;}let otpType=type,digitCount=digits;if(!(type===1||type===2)&&(digits===1||digits===2)){otpType=digits;digitCount=0;}return {secret,name,issuer,alg,type:otpType,digits:digitCount};}
function parseMigration(b){const out=[];let p=0;while(p<b.length){let t;[t,p]=vr(b,p);const f=t>>3,w=t&7;if(f===1&&w===2){let l;[l,p]=vr(b,p);out.push(parseOtp(b.slice(p,p+l)));p+=l;}else if(w===0){let v;[v,p]=vr(b,p);}else if(w===2){let l;[l,p]=vr(b,p);p+=l;}else if(w===5)p+=4;else if(w===1)p+=8;else break;}return out;}
function b32e(b){const A='ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';let bits=0,v=0,o='';for(const x of b){v=(v<<8)|x;bits+=8;while(bits>=5){o+=A[(v>>>(bits-5))&31];bits-=5;}}if(bits>0)o+=A[(v<<(5-bits))&31];return o;}
async function importMigration(raw){
raw=(raw||'').trim();if(!raw)return;
let data=raw;const m=raw.match(/[?&]data=([^&]+)/);if(m)data=decodeURIComponent(m[1]);
let bytes,list;try{bytes=b64d(data);list=parseMigration(bytes);}catch(e){alert('Не удалось разобрать migration URL');return;}
const lines=[];const skipped=[];
list.forEach((a,i)=>{const label=(a.issuer?a.issuer+': ':'')+a.name;
if(a.type!==2){skipped.push(label||('#'+(i+1)));return;}
if(!(a.alg===0||a.alg===1)){skipped.push((label||('#'+(i+1)))+' (algorithm)');return;}
if(a.digits===2){skipped.push((label||('#'+(i+1)))+' (8 digits)');return;}
if(!a.secret||!a.secret.length)return;
let name=label.trim()||('Account '+(i+1));name=name.replace(/[\t\r\n]/g,' ').slice(0,32);
lines.push(name+'\t'+b32e(a.secret));});
if(!lines.length){alert('Нет поддерживаемых TOTP/SHA1 аккаунтов'+(skipped.length?': '+skipped.join(', '):''));return;}
const body='accounts='+encodeURIComponent(lines.join('\n'));
const r=await fetch('/import',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
if(r.ok)location.href='/?ok=imported';else alert('Ошибка импорта');}
const importBtn=document.getElementById('importBtn');
if(importBtn)importBtn.onclick=()=>importMigration(document.getElementById('mig').value);
const af=document.querySelector('form[action="/add"]');
if(af){const s=af.querySelector('input[name=secret]'),n=af.querySelector('input[name=name]');
s.addEventListener('input',()=>{if(!s.value.startsWith('otpauth://'))return;try{const u=new URL(s.value);const x=u.searchParams.get('secret');if(x)s.value=x.replace(/\s+/g,'').toUpperCase();if(!n.value){const i=u.searchParams.get('issuer')||'';const l=decodeURIComponent(u.pathname.replace(/^\/+/,''));n.value=i?i+': '+l:l;}}catch(e){}});
af.addEventListener('submit',e=>{if(s.value.startsWith('otpauth-migration://')){e.preventDefault();importMigration(s.value);}});}
</script>)JS");

  page += F("</body></html>");
  return page;
}

void redirectTo(const String &url) {
  server.sendHeader("Location", url);
  server.send(303, "text/plain", "");
}

void handleRoot() {
  apLastActivity = millis();
  Serial.println("GET /");
  server.send(200, "text/html", buildPage());
  Serial.println("GET / done");
}

void handleSave() {
  apLastActivity = millis();
  String ssid = server.arg("ssid"); ssid.trim();
  String pass = server.arg("pass");
  String ntp = server.arg("ntp_server"); ntp.trim();
  bool autoNtp = server.hasArg("ntp_auto");

  if (ssid.length() > 32) { redirectTo("/?err=ssid"); return; }
  if (pass.length() > 64) { redirectTo("/?err=pass"); return; }
  if (ntp.length() > 63) { redirectTo("/?err=ntp"); return; }
  if (ssid.length() == 0 && autoNtp) { redirectTo("/?err=ssid"); return; }
  if (ssid.length() > 0 && pass.length() == 0 && ssid != String(wifiSsid)) {
    redirectTo("/?err=pass"); return;
  }

  if (ssid.length() == 0) {
    wifiSsid[0] = 0;
    wifiPass[0] = 0;
  } else {
    strncpy(wifiSsid, ssid.c_str(), sizeof(wifiSsid) - 1);
    wifiSsid[sizeof(wifiSsid) - 1] = 0;
    if (pass.length() > 0) {
      strncpy(wifiPass, pass.c_str(), sizeof(wifiPass) - 1);
      wifiPass[sizeof(wifiPass) - 1] = 0;
    }
  }

  if (ntp.length() == 0) snprintf(ntpServer, sizeof(ntpServer), "pool.ntp.org");
  else {
    strncpy(ntpServer, ntp.c_str(), sizeof(ntpServer) - 1);
    ntpServer[sizeof(ntpServer) - 1] = 0;
  }
  ntpAuto = autoNtp;
  saveSettings();
  Serial.println("POST /save");

  if (server.hasArg("reboot")) {
    server.send(200, "text/html", F("<!doctype html><meta charset=utf-8><p>Сохранено. Перезагрузка...</p>"));
    delay(600);
    ESP.restart();
    return;
  }
  redirectTo("/?ok=saved");
}

void handleAdd() {
  apLastActivity = millis();
  String name = server.arg("name"); name.trim();
  String secret = server.arg("secret");
  Serial.println("POST /add");
  if (addAccount(name.c_str(), secret.c_str())) redirectTo("/?ok=added");
  else redirectTo("/?err=account");
}

void handleDelete() {
  apLastActivity = millis();
  int idx = server.arg("index").toInt();
  if (idx >= 0 && idx < accountCount) {
    deleteAccountAt(idx);
    redirectTo("/?ok=deleted");
  } else {
    redirectTo("/?err=index");
  }
}

void handleDeleteAll() {
  apLastActivity = millis();
  deleteAllAccounts();
  redirectTo("/?ok=deleted_all");
}

void handleImport() {
  apLastActivity = millis();
  String data = server.arg("accounts");
  int added = 0;
  int pos = 0;
  while (pos < (int)data.length()) {
    int nl = data.indexOf('\n', pos);
    if (nl < 0) nl = data.length();
    String line = data.substring(pos, nl);
    pos = nl + 1;
    int tab = line.indexOf('\t');
    if (tab > 0) {
      String name = line.substring(0, tab); name.trim();
      String sec = line.substring(tab + 1);
      if (addAccount(name.c_str(), sec.c_str())) added++;
    }
  }
  redirectTo("/?ok=imported&n=" + String(added));
}

void handleReboot() {
  apLastActivity = millis();
  server.send(200, "text/html", F("<!doctype html><meta charset=utf-8><p>Перезагрузка...</p>"));
  delay(500);
  ESP.restart();
}

void drawSetupAP() {
  // Пароль всегда на второй строке, чтобы не исчезал.
  char line[17];
  snprintf(line, sizeof(line), "PASS: %s", apPass);
  lcdLine(1, line);

  if (!apPhase) lcdLine(0, "AP: TOTP-Setup");
  else lcdLine(0, "192.168.4.1");
}

void startSetupAP(bool returnMenu) {
  apReturnMenu = returnMenu;
  apLastActivity = millis();
  apLastDraw = 0;
  apPhase = false;

  unsigned long apNum = 10000000UL + (esp_random() % 90000000UL);
  snprintf(apPass, sizeof(apPass), "%08lu", apNum);

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.softAP(AP_SSID, apPass);
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.begin();
  Serial.printf("AP %s started, IP %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  mode = MODE_SETUP_AP;
  flushEncoder();
  lcd.clear();
  drawSetupAP();
}

void stopSetupAP() {
  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  if (apReturnMenu) enterMenu(millis());
  else enterNormal(millis());
}

void processSetupAP(unsigned long now) {
  dnsServer.processNextRequest();
  server.handleClient();

  if (AP_TIMEOUT > 0 && now - apLastActivity > AP_TIMEOUT) {
    stopSetupAP();
    return;
  }
  if (now - apLastDraw >= 3000) {
    apLastDraw = now;
    apPhase = !apPhase;
    drawSetupAP();
  }
}

// ============ энкодер / кнопка ============
void setupEncoder() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);
  pinMode(ENC_SW_PIN, INPUT_PULLUP);

  encoderPrevState = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B_PIN), encoderISR, CHANGE);

  swLast = (digitalRead(ENC_SW_PIN) == LOW);
  swReported = swLast;
  swDebounce = millis();
  pressStart = millis();
  longPressHandled = swLast;
}

void processEncoder(unsigned long now) {
  int32_t diff = encoderTicks - encoderConsumed;
  if (diff < ENC_TICKS_PER_DETENT && diff > -ENC_TICKS_PER_DETENT) return;

  int32_t rawSteps = diff / ENC_TICKS_PER_DETENT;
  encoderConsumed += rawSteps * ENC_TICKS_PER_DETENT;
  int steps = rawSteps * ENCODER_DIRECTION;
  if (steps == 0) return;

  if (mode == MODE_NORMAL) {
    lastActivity = now;
    switchAccount(steps, now);
  } else if (mode == MODE_MENU) {
    menuIndex = wrapIndex(menuIndex + steps, MENU_COUNT);
    lastActivity = now;
    drawMenu();
  } else if (mode == MODE_ACCOUNTS) {
    if (accountCount > 0) {
      accountIndex = wrapIndex(accountIndex + steps, accountCount + 1);
      lastActivity = now;
      drawAccounts();
    }
  } else if (mode == MODE_CONFIRM_DELETE || mode == MODE_CONFIRM_DELETE_ALL) {
    confirmIndex = (confirmIndex + 2 + (steps % 2)) % 2;
    lastActivity = now;
    drawConfirm();
  } else if (mode == MODE_STATUS) {
    statusPage = wrapIndex(statusPage + steps, 2);
    lastActivity = now;
    drawStatus();
  }
}

void onShortPress(unsigned long now) {
  switch (mode) {
    case MODE_NORMAL:
      switchAccount(1, now);
      break;
    case MODE_MENU:
      selectMenu();
      break;
    case MODE_ACCOUNTS:
      selectAccount();
      break;
    case MODE_CONFIRM_DELETE:
    case MODE_CONFIRM_DELETE_ALL:
      confirmDelete();
      break;
    case MODE_SYNC_CONNECT:
    case MODE_SYNC_NTP:
      skipSync();
      break;
    case MODE_STATUS:
      statusPage = (statusPage + 1) % 2;
      lastActivity = now;
      drawStatus();
      break;
    default:
      break;
  }
}

void onLongPress(unsigned long now) {
  switch (mode) {
    case MODE_NORMAL:
      enterMenu(now);
      break;
    case MODE_MENU:
      enterNormal(now);
      break;
    case MODE_ACCOUNTS:
      enterMenu(now);
      break;
    case MODE_CONFIRM_DELETE:
    case MODE_CONFIRM_DELETE_ALL:
      enterAccounts(now);
      break;
    case MODE_SETUP_AP:
      stopSetupAP();
      break;
    case MODE_SYNC_CONNECT:
    case MODE_SYNC_NTP:
      skipSync();
      break;
    case MODE_STATUS:
      enterMenu(now);
      break;
    default:
      break;
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
      onLongPress(now);
    }

    if (!sw && swReported) {
      swReported = false;
      if (!longPressHandled) onShortPress(now);
      longPressHandled = false;
    }
  }
}

// ============ сон ============
void goToSleep() {
  savedAccount = currentAccount;

  lcd.noBacklight();
  delay(50);

  rtc_gpio_pullup_en(GPIO_NUM_25);
  rtc_gpio_pulldown_dis(GPIO_NUM_25);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_25, 0);
  esp_deep_sleep_start();
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/add", HTTP_POST, handleAdd);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/delete_all", HTTP_POST, handleDeleteAll);
  server.on("/import", HTTP_POST, handleImport);
  server.on("/reboot", HTTP_POST, handleReboot);
}

void setup() {
  Serial.begin(115200);
  Serial.printf("BOOT reset_reason=%d\n", (int)esp_reset_reason());
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, hg0);
  lcd.createChar(1, hg1);
  lcd.createChar(2, hg2);
  lcd.createChar(3, hg3);

  setupEncoder();
  loadSettings();
  setupWebServer();
  sntp_set_time_sync_notification_cb(onNtpSync);

  lastActivity = millis();
  currentAccount = 0;
  if (accountCount > 0 && savedAccount >= 0 && savedAccount < accountCount) {
    currentAccount = savedAccount;
  }

  bool wakeFromSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
  bool bootHold = !wakeFromSleep && (digitalRead(ENC_SW_PIN) == LOW);
  if (bootHold) {
    lcd.clear();
    lcdLine(0, "SETUP MODE");
    lcdLine(1, "HOLD...");
    unsigned long t0 = millis();
    while (digitalRead(ENC_SW_PIN) == LOW && millis() - t0 < 1200) delay(20);
    bootHold = (digitalRead(ENC_SW_PIN) == LOW);
  }

  if (bootHold) {
    startSetupAP(false);
    return;
  }

  // Пока нет аккаунтов — всегда держим портал настройки.
  if (accountCount == 0) {
    startSetupAP(false);
    return;
  }

  if (ntpAuto && wifiSsid[0] != 0 && needSync()) {
    startSync(false);
    return;
  }

  enterNormal(millis());
}

void processMode(unsigned long now) {
  switch (mode) {
    case MODE_MENU:
    case MODE_ACCOUNTS:
    case MODE_CONFIRM_DELETE:
    case MODE_CONFIRM_DELETE_ALL:
    case MODE_STATUS:
      if (now - lastActivity > MENU_TIMEOUT) enterNormal(now);
      break;
    case MODE_SETUP_AP:
      processSetupAP(now);
      break;
    case MODE_SYNC_CONNECT:
    case MODE_SYNC_NTP:
    case MODE_SYNC_RESULT:
    case MODE_SYNC_ERROR:
      processSync(now);
      break;
    default:
      break;
  }
}

void loop() {
  unsigned long now = millis();

  processEncoder(now);
  processButton(now);
  processMode(now);
  now = millis();

  if (mode == MODE_NORMAL) {
    if (accountCount > 0) {
      int nameLen = strlen(accounts[currentAccount].name);
      if (nameLen > 16 && now - lastMarquee >= 600) {
        lastMarquee = now;
        marqueePos = (marqueePos + 1) % (nameLen + 1);
        updateNameLine();
      }
    }
    if (now - lastCode >= 1000) {
      lastCode = now;
      updateCodeLine();
    }
    if (now - lastActivity >= SLEEP_AFTER) {
      if (digitalRead(ENC_SW_PIN) == HIGH) goToSleep();
      else lastActivity = now;
    }
  }
}
