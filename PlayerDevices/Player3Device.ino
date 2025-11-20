#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

#define PLAYER_ID 3

// LCD & SERIAL

// I2C LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Shared bus with Simon
const int rxPin = 10;
const int txPin = 11;
SoftwareSerial mySerial(rxPin, txPin);

// INPUT HARDWARE

// 4 LED-game buttons, left-to-right aligned with Simon LEDs (R,G,Y,B)
const int btnPins[4] = {2, 3, 4, 5};

// Joystick for BUZ game (Y axis only)
const int joyYPin = A0;
const int JOY_LOW_T  = 300;
const int JOY_HIGH_T = 700;

// Ultrasonic + IR pins
const int trigPin = 6;   // HC-SR04 trig
const int echoPin = 7;   // HC-SR04 echo
const int irPin   = 8;   // IR receiver pin

// STATE & GAME VARS

bool alive = true;      // player alive or eliminated
int currentLevel = 1;

String memoryNumber = "";  // from S_MEM
String ledAnswer    = "";  // player's LED input as R/G/Y/B
String buzAnswer    = "";  // player's BUZ input as H/L
String usAnswer     = "";  // final US distance as string
String recallAnswer = "";  // IR digits locked into exact memoryNumber.length()

// Time / countdown
int timeLeftSec = 0;
unsigned long responseEndTime   = 0;
unsigned long lastCountdownTick = 0;

// For reading serial line based
String serialInBuffer = "";

// Button debouncing
int lastBtnState[4];
unsigned long lastBtnChange[4];
const unsigned long btnDebounce = 50;

// Joystick input debouncing for buzzer game
unsigned long lastJoyInputTime = 0;
const unsigned long joyInputDelay = 250;

// Player side view of which minigame is active for HUD
enum PlayerMiniGame {
  PG_NONE,
  PG_MEM,
  PG_LED,
  PG_BUZ,
  PG_US,
  PG_RECALL
};
PlayerMiniGame currentGame = PG_NONE;

// High level player state
enum PlayerState {
  P_IDLE,
  P_MEM_SHOW,
  P_LED_INPUT,
  P_BUZ_INPUT,
  P_US_INPUT,
  P_MEM_RECALL_INPUT,
  P_WAIT_RESULT,
  P_ELIMINATED
};
PlayerState pState = P_IDLE;

// HELPERS

char gameCharFromMiniGame(PlayerMiniGame g) {
  switch (g) {
    case PG_MEM:    return 'A';  // memory number game
    case PG_LED:    return 'B';  // LED pattern
    case PG_BUZ:    return 'C';  // buzzer pattern
    case PG_US:     return 'D';  // ultrasonic
    case PG_RECALL: return 'A';  // recall same category
    default:        return '-';
  }
}

void drawBottomHUD() {
  lcd.setCursor(0, 1);
  lcd.print("G:");
  lcd.print(gameCharFromMiniGame(currentGame));
  lcd.print(" P");
  lcd.print(PLAYER_ID);
  lcd.print(" L:");
  if (currentLevel < 10) lcd.print("0");
  lcd.print(currentLevel);
  lcd.print(" ");
}

void showTopMessage(const String &msg) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(msg);
  drawBottomHUD();
}

void showTimedHUD(const char* gameTag) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(gameTag);
  lcd.print(" T:");
  if (timeLeftSec < 10) lcd.print("0");
  lcd.print(timeLeftSec);
  lcd.print("s");
  drawBottomHUD();
}

void startCountdown(int seconds) {
  timeLeftSec       = seconds;
  responseEndTime   = millis() + (unsigned long)seconds * 1000UL;
  lastCountdownTick = millis();
}

void updateCountdownUI() {
  if (timeLeftSec <= 0) return;
  unsigned long now = millis();

  if (now - lastCountdownTick >= 1000) {
    lastCountdownTick += 1000;
    timeLeftSec--;
    if (timeLeftSec < 0) timeLeftSec = 0;

    if (currentGame == PG_LED)   showTimedHUD("LED");
    else if (currentGame == PG_BUZ) showTimedHUD("BUZ");
    else if (currentGame == PG_US)  showTimedHUD("Aim");
    else if (currentGame == PG_RECALL) showTimedHUD("MEM");
  }
}

// SERIAL PARSING

void handleLine(const String &line);

void pollSerial() {
  while (mySerial.available()) {
    char c = mySerial.read();
    if (c == '\n') {
      if (serialInBuffer.length() > 0) {
        handleLine(serialInBuffer);
        serialInBuffer = "";
      }
    } else if (c != '\r') {
      serialInBuffer += c;
    }
  }
}

void handleS_LEVEL(const String &rest) {
  currentLevel = rest.toInt();
}

void handleS_MEM(const String &rest) {
  int comma = rest.indexOf(',');
  String num = (comma < 0) ? rest : rest.substring(comma + 1);
  memoryNumber = num;
  currentGame  = PG_MEM;
  pState       = P_MEM_SHOW;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mem: ");
  lcd.print(memoryNumber);
  drawBottomHUD();
}

void handleS_TIME(const String &rest) {
  int comma = rest.indexOf(',');
  String val = (comma < 0) ? rest : rest.substring(comma + 1);

  int t = val.toInt();
  startCountdown(t);

  if (currentGame == PG_LED)   showTimedHUD("LED");
  else if (currentGame == PG_BUZ) showTimedHUD("BUZ");
  else if (currentGame == PG_US)  showTimedHUD("Aim");
  else if (currentGame == PG_RECALL) showTimedHUD("MEM");
}

void handleS_LED_START() {
  if (!alive) return;
  currentGame = PG_LED;
  pState      = P_LED_INPUT;
  ledAnswer   = "";
  showTopMessage("LED Ready...");
}

void handleS_BUZ_START() {
  if (!alive) return;
  currentGame = PG_BUZ;
  pState      = P_BUZ_INPUT;
  buzAnswer   = "";
  showTopMessage("BUZ Ready...");
}

void handleS_US_START(const String &rest) {
  if (!alive) return;

  int c = rest.indexOf(',');
  String val = (c < 0) ? rest : rest.substring(c + 1);
  int tgt = val.toInt();

  usAnswer = "";

  currentGame = PG_US;
  pState      = P_US_INPUT;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Aim ");
  lcd.print(tgt);
  lcd.print("cm");

  drawBottomHUD();
}

void handleS_RECALL_START() {
  if (!alive) return;
  currentGame = PG_RECALL;
  pState      = P_MEM_RECALL_INPUT;
  recallAnswer = "";

  showTopMessage("Recall NOW");
}

void handleS_REQ(const String &rest) {
  int c1 = rest.indexOf(',');
  if (c1 < 0) return;
  int c2 = rest.indexOf(',', c1 + 1);
  if (c2 < 0) return;

  int id = rest.substring(c1 + 1, c2).toInt();
  String type = rest.substring(c2 + 1);

  if (id != PLAYER_ID || !alive) return;

  if (type == "LED") {
    mySerial.print("P_RESP,"); mySerial.print(PLAYER_ID);
    mySerial.print(",LED,"); mySerial.print(ledAnswer); mySerial.print("\n");
    pState = P_WAIT_RESULT;
  }
  else if (type == "BUZ") {
    mySerial.print("P_RESP,"); mySerial.print(PLAYER_ID);
    mySerial.print(",BUZ,"); mySerial.print(buzAnswer); mySerial.print("\n");
    pState = P_WAIT_RESULT;
  }
  else if (type == "US") {
    mySerial.print("P_RESP,"); mySerial.print(PLAYER_ID);
    mySerial.print(",US,"); mySerial.print(usAnswer); mySerial.print("\n");
    pState = P_WAIT_RESULT;
  }
  else if (type == "MEM") {
    mySerial.print("P_RESP,"); mySerial.print(PLAYER_ID);
    mySerial.print(",MEM,"); mySerial.print(recallAnswer); mySerial.print("\n");
    pState = P_WAIT_RESULT;
  }
}

void handleS_RES(const String &rest) {
  int c1 = rest.indexOf(',');
  if (c1 < 0) return;
  int c2 = rest.indexOf(',', c1 + 1);
  if (c2 < 0) return;
  int c3 = rest.indexOf(',', c2 + 1);
  if (c3 < 0) return;

  int id = rest.substring(c1 + 1, c2).toInt();
  String result = rest.substring(c3 + 1);

  if (id != PLAYER_ID) return;

  if (result == "OK") {
    alive = true;
    showTopMessage("OK!");
    pState = P_IDLE;
  } else {
    alive = false;
    pState = P_ELIMINATED;
    showTopMessage("ELIMINATED!");
  }
}

void handleLine(const String &line) {
  int c = line.indexOf(',');
  String cmd = (c < 0) ? line : line.substring(0, c);
  String rest = (c < 0) ? ""   : line.substring(c + 1);

  if (cmd == "S_LEVEL")         handleS_LEVEL(rest);
  else if (cmd == "S_MEM")      handleS_MEM(line);
  else if (cmd == "S_TIME")     handleS_TIME(line);
  else if (cmd == "S_LED_START") handleS_LED_START();
  else if (cmd == "S_BUZ_START") handleS_BUZ_START();
  else if (cmd == "S_US_START")  handleS_US_START(line);
  else if (cmd == "S_RECALL_START") handleS_RECALL_START();
  else if (cmd == "S_REQ")       handleS_REQ(line);
  else if (cmd == "S_RES")       handleS_RES(line);
}

// INPUT HANDLERS

void initButtons() {
  for (int i = 0; i < 4; i++) {
    pinMode(btnPins[i], INPUT_PULLUP);
    lastBtnState[i]   = digitalRead(btnPins[i]);
    lastBtnChange[i]  = millis();
  }
}

char charFromButtonIndex(int i) {
  if (i == 0) return 'R';
  if (i == 1) return 'G';
  if (i == 2) return 'Y';
  if (i == 3) return 'B';
  return '?';
}

void handleLEDInput() {
  if (!alive) return;
  unsigned long now = millis();
  if (now > responseEndTime) return;

  for (int i = 0; i < 4; i++) {
    int reading = digitalRead(btnPins[i]);

    if (reading != lastBtnState[i]) {
      if (now - lastBtnChange[i] > btnDebounce) {
        lastBtnChange[i] = now;
        lastBtnState[i]  = reading;

        if (reading == LOW) {
          ledAnswer += charFromButtonIndex(i);
        }
      }
    }
  }
}

void handleBuzzerInput() {
  if (!alive) return;
  unsigned long now = millis();
  if (now > responseEndTime) return;

  int y = analogRead(joyYPin);

  if (now - lastJoyInputTime > joyInputDelay) {
    if (y < JOY_LOW_T) {
      buzAnswer += 'H';
      lastJoyInputTime = now;
    } else if (y > JOY_HIGH_T) {
      buzAnswer += 'L';
      lastJoyInputTime = now;
    }
  }
}

// US = A option: take one distance when time ends
long measureDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(3);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 18000);
  long cm = duration / 58;
  return cm;
}

void handleUSInput() {
  if (!alive) return;

  unsigned long now = millis();
  if (now >= responseEndTime) {
    if (usAnswer.length() == 0) {
      long d = measureDistance();
      usAnswer = String(d);
    }
    return;
  }
}

// MEMORY RECALL = B (lock exact #digits)
void handleMemRecallInput() {
  if (!alive) return;

  unsigned long now = millis();
  if (now > responseEndTime) return;

  int reading = digitalRead(irPin);
  static bool lastState = HIGH;

  if (reading != lastState) {
    lastState = reading;

    if (reading == LOW) {
      delay(180);

      if (recallAnswer.length() < memoryNumber.length()) {
        int digit = random(0,10);
        recallAnswer += String(digit);
      }
    }
  }
}

// SETUP & LOOP

void setup() {
  lcd.init();
  lcd.backlight();

  mySerial.begin(9600);

  initButtons();
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(irPin, INPUT_PULLUP);

  alive = true;
  currentLevel = 1;
  currentGame  = PG_NONE;
  pState       = P_IDLE;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Player ");
  lcd.print(PLAYER_ID);
  lcd.setCursor(0, 1);
  lcd.print("Waiting Simon...");
}

void loop() {
  pollSerial();

  if (pState == P_LED_INPUT || pState == P_BUZ_INPUT ||
      pState == P_US_INPUT  || pState == P_MEM_RECALL_INPUT) {
    updateCountdownUI();
  }

  switch (pState) {
    case P_IDLE:
      break;

    case P_MEM_SHOW:
      break;

    case P_LED_INPUT:
      handleLEDInput();
      break;

    case P_BUZ_INPUT:
      handleBuzzerInput();
      break;

    case P_US_INPUT:
      handleUSInput();
      break;

    case P_MEM_RECALL_INPUT:
      handleMemRecallInput();
      break;

    case P_WAIT_RESULT:
      break;

    case P_ELIMINATED:
      break;
  }
}
