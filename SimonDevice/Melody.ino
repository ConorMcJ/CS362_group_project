#include <LiquidCrystal.h>

// ---------------- LCD PINS (your wiring) ----------------
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// ---------------- PLAYER I/O (your wiring) ---------------
#define LED_R 6     // Red LED
#define LED_Y 7     // Yellow LED
#define LED_G 8     // Green LED
#define LED_B 9     // Blue LED

#define BTN_R A0    // Red Button
#define BTN_Y A1    // Yellow Button
#define BTN_G A2    // Green Button
#define BTN_B A3    // Blue Button

#define BUZZER 10   // Buzzer

// ---------------- Test Pattern (RYGB) --------------------
uint8_t pattern[4] = {0, 1, 2, 3};
const int toneFreq[4] = {262, 294, 330, 392};
unsigned long toneStart = 0;
bool toneOn = false;

void setup() {

  // ---- LCD ----
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("PLAYER TEST MODE");
  delay(1000);
  lcd.clear();

  // ---- LEDs ----
  pinMode(LED_R, OUTPUT);
  pinMode(LED_Y, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  // ---- Buttons ----
  pinMode(BTN_R, INPUT_PULLUP);
  pinMode(BTN_Y, INPUT_PULLUP);
  pinMode(BTN_G, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);

  // ---- Buzzer ----
  pinMode(BUZZER, OUTPUT);

  // Main screen
  lcd.setCursor(0, 0);
  lcd.print("Press Any Button");
  lcd.setCursor(0, 1);
  lcd.print("To Begin Test");
}

void loop() {

  // ---- BUTTON TEST ----
  if (digitalRead(BTN_R) == LOW) testButton(0);
  if (digitalRead(BTN_Y) == LOW) testButton(1);
  if (digitalRead(BTN_G) == LOW) testButton(2);
  if (digitalRead(BTN_B) == LOW) testButton(3);

  // ---- AUTO PATTERN TEST ----
  static unsigned long last = 0;
  if (millis() - last > 3000) {
    last = millis();
    playPattern();
  }

  // ---- Auto-stop tone ----
  if (toneOn && millis() - toneStart > 250) {
    noTone(BUZZER);
    toneOn = false;
  }
}

// Button 
void testButton(uint8_t c) {
  lcd.clear();
  lcd.print("BUTTON: ");

  switch(c) {
    case 0: lcd.print("RED"); break;
    case 1: lcd.print("YELLOW"); break;
    case 2: lcd.print("GREEN"); break;
    case 3: lcd.print("BLUE"); break;
  }

  lightLED(c, true);
  playTone(c);

  delay(300);

  lightLED(c, false);

  lcd.clear();
  lcd.print("Press Any Button");
  lcd.setCursor(0,1);
  lcd.print("To Begin Test");
}

// Demo Simon test 
void playPattern() {
  lcd.clear();
  lcd.print("Testing LEDs");

  for (int i = 0; i < 4; i++) {

    lcd.setCursor(0,1);
    lcd.print("LED: ");

    switch(pattern[i]) {
      case 0: lcd.print("RED   "); break;
      case 1: lcd.print("YELLOW"); break;
      case 2: lcd.print("GREEN "); break;
      case 3: lcd.print("BLUE  "); break;
    }

    lightLED(pattern[i], true);
    playTone(pattern[i]);
    delay(250);

    lightLED(pattern[i], false);
    noTone(BUZZER);
    delay(150);
  }

  lcd.clear();
  lcd.print("Pattern Done");
  delay(500);
  lcd.clear();
  lcd.print("Press Buttons");
}

// LED blinker
void lightLED(uint8_t c, bool on) {
  switch(c) {
    case 0: digitalWrite(LED_R, on); break;
    case 1: digitalWrite(LED_Y, on); break;
    case 2: digitalWrite(LED_G, on); break;
    case 3: digitalWrite(LED_B, on); break;
  }
}

// Tone
void playTone(uint8_t c) {
  tone(BUZZER, toneFreq[c]);
  toneStart = millis();
  toneOn = true;
}
