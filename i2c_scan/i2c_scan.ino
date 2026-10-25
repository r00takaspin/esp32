// I2C-сканер: показывает все устройства на шине (SDA=21, SCL=22)
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); // SDA = GPIO21, SCL = GPIO22
  delay(100);

  Serial.println("=== I2C Scanner ===");

  int nDevices = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("Найден адрес: 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      // расшифровка известных устройств
      switch (addr) {
        case 0x27: Serial.print("  <- LCD (PCF8574)"); break;
        case 0x3F: Serial.print("  <- LCD (PCF8574)"); break;
        case 0x68: Serial.print("  <- RTC DS3231/DS1307"); break;
        case 0x57: Serial.print("  <- EEPROM AT24C32 (модуль DS3231)"); break;
        case 0x50: Serial.print("  <- EEPROM AT24C32 (модуль DS1307)"); break;
        default: break;
      }
      Serial.println();
      nDevices++;
    }
  }

  if (nDevices == 0) {
    Serial.println("Устройства не найдены! Проверь провода SDA/SCL и питание.");
  } else {
    Serial.print("Всего устройств: ");
    Serial.println(nDevices);
  }
}

void loop() {}
