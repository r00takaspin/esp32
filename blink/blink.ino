// Blink — мигает встроенным светодиодом 1 раз в секунду
const int ledPin = 2; // GPIO2 — встроенный светодиод на большинстве плат ESP32 Dev Module

void setup() {
  pinMode(ledPin, OUTPUT);
}

void loop() {
  digitalWrite(ledPin, HIGH); // светодиод включён
  delay(1000);                // пауза 1 секунда
  digitalWrite(ledPin, LOW);  // светодиод выключен
  delay(1000);                // пауза 1 секунда
}
