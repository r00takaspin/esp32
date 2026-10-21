// LCD 1602A + I2C (PCF8574) на ESP32 — «Hello, World!» с подсветкой ~50%
// I2C: SDA = GPIO21, SCL = GPIO22, адрес 0x27
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Wire.begin(21, 22); // SDA = 21, SCL = 22

  lcd.init();

  lcd.setCursor(0, 0);
  lcd.print("Hello, World!");
  lcd.setCursor(0, 1);
  lcd.print("I2C: 0x27");
}

void loop() {
  // Программный ШИМ подсветки ~25% (PCF8574 не умеет аппаратный PWM)
  // Частота ~830 Гц, скважность 25% (вкл 0.3 мс / выкл 0.9 мс)
  lcd.backlight();          // подсветка вкл
  delayMicroseconds(300);   // ~0.3 мс
  lcd.noBacklight();        // подсветка выкл
  delayMicroseconds(900);   // ~0.9 мс
}
