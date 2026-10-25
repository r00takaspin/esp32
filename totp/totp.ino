// TOTP-генератор (2FA): DS3231 (время) + LCD 1602 (вывод кода)
// Кнопка BOOT: пробуждение + переключение аккаунта. Экран гаснет через 30 сек.
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>
#include "mbedtls/md.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "totp_secrets.h" // твои секреты (в .gitignore)

LiquidCrystal_I2C lcd(0x27, 16, 2);

const int BOOT_PIN = 0;               // GPIO0 — кнопка BOOT
const unsigned long SLEEP_AFTER = 30000; // 30 секунд

int currentAccount = 0;
int currentNameLen = 0;

// --- кнопка ---
bool lastBtn = false;
bool btnReported = false;
unsigned long btnDebounce = 0;
unsigned long lastActivity = 0;

// --- тайминги обновления ---
unsigned long lastMarquee = 0;
unsigned long lastCode = 0;

// ============ DS3231 → Unix time ============
uint8_t bcdToDec(uint8_t bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
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
void updateNameLine() {
  const char *name = totpEntries[currentAccount].name;
  int len = strlen(name);
  lcd.setCursor(0, 0);

  if (len <= 16) {
    lcd.print(name);
    for (int i = len; i < 16; i++) lcd.print(" ");
  } else {
    // бегущая строка: окно 16 символов скользит по имени + пауза 8 позиций
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

  // код 6 цифр + индикатор оставшегося времени (8 сегментов)
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

// ============ сон ============
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
  lcd.backlight();
  lastCode = 0;   // сразу обновить код
  lastMarquee = 0;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  pinMode(BOOT_PIN, INPUT_PULLUP);
  lastActivity = millis();

  currentNameLen = strlen(totpEntries[currentAccount].name);
  lcd.clear();
  updateNameLine();
  updateCodeLine();
}

void loop() {
  unsigned long now = millis();

  // --- кнопка BOOT ---
  bool btn = (digitalRead(BOOT_PIN) == LOW);
  if (btn != lastBtn) {
    btnDebounce = now;
    lastBtn = btn;
  }
  if (now - btnDebounce > 40) {
    if (btn && !btnReported) {
      btnReported = true;
      lastActivity = now;
      currentAccount = (currentAccount + 1) % TOTP_COUNT;
      currentNameLen = strlen(totpEntries[currentAccount].name);
      updateNameLine();
      updateCodeLine();
    }
    if (!btn) btnReported = false;
  }

  // --- бегущая строка (только если имя длинное) ---
  if (currentNameLen > 16 && now - lastMarquee >= 300) {
    lastMarquee = now;
    updateNameLine();
  }

  // --- код + индикатор (раз в секунду) ---
  if (now - lastCode >= 1000) {
    lastCode = now;
    updateCodeLine();
  }

  // --- сон через 30 сек без нажатий ---
  if (now - lastActivity >= SLEEP_AFTER) {
    goToSleep();
  }
}
