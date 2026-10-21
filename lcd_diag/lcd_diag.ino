// Диагностика LCD: мигаем подсветкой, потом заливаем экран блоками
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C *lcd = nullptr;

byte findAddress() {
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return a;
  }
  return 0;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  byte addr = findAddress();
  Serial.print("I2C addr: 0x");
  Serial.println(addr, HEX);

  lcd = new LiquidCrystal_I2C(addr, 16, 2);
  lcd->init();

  // 1) мигаем подсветкой 3 раза — проверка, что I2C-запись доходит до PCF8574
  for (int i = 0; i < 3; i++) {
    lcd->noBacklight();
    delay(250);
    lcd->backlight();
    delay(250);
  }

  // 2) заливаем весь экран сплошными блоками (все пиксели включены)
  for (int r = 0; r < 2; r++) {
    lcd->setCursor(0, r);
    for (int c = 0; c < 16; c++) {
      lcd->write((uint8_t)0xFF); // 0xFF = полный блок в ROM HD44780
    }
  }
}

void loop() {
  // ничего не делаем
}
