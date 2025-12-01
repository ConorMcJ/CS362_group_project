#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <IRremote.h>

#define PLAYER_ID 1

// ======================================================================
// LCD & SERIAL
// ======================================================================

// I2C LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Dedicated line to SIMON (must match Simon)
const int rxPin = 2;    // from Simon TX=3
const int txPin = 3;    // to Simon RX=2
SoftwareSerial mySerial(rxPin, txPin);

// ======================================================================
// INPUT HARDWARE
// ======================================================================

// 4 LED-game buttons (R,G,Y,B in that order)
const int btnPins[4] = {4, 5, 6, 7};

// Joystick for BUZ game (Y-axis only)
const int joyYPin  = A0;
const int JOY_LOW_T  = 300;
const int JOY_HIGH_T = 700;

// Ultrasonic sensor (unique per player)
const int trigPin = 8;
const int echoPin = 9;

// IR Receiver
const int irPin = 10;

// ======================================================================
// STATE & GAME VARS
// ======================================================================

bool alive = true;
int currentLevel = 1;

String memoryNumber = "";
String recallAnswer = "";
String ledAnswer = "";
String buzAnswer = "";

// countdown state
int timeLeftSec = 0;
unsigned long responseEndTime   = 0;
unsigned long lastCountdownTick = 0;

// For reading serial messages
String serialInBuffer = "";

// Button debouncing
int lastBtnState[4];
unsigned long lastBtnChange[4];
const unsigned long btnDebounce = 50;

// BuzzGame joystick debouncing
unsigned long lastJoyInputTime = 0;
const unsigned long joyInputDelay = 250;

// mini-games
enum PlayerMiniGame {
  PG_NONE,
  PG_MEM,
  PG_LED,
  PG_BUZ,
  PG_US,
  PG_RECALL
};
PlayerMiniGame currentGame = PG_NONE;

// player high-level state
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

// ======================================================================
// IR Remote Codes (UPDATE LATER WITH REAL READINGS)
// ======================================================================

#define IR_POWER   0xFF45BA
#define IR_0       0xFF16E9
#define IR_1       0xFF0CF3
#define IR_2       0xFF18E7
#define IR_3       0xFF5EA1
#define IR_4       0xFF08F7
#define IR_5       0xFF1CE3
#define IR_6       0xFF5AA5
#define IR_7       0xFF42BD
#define IR_8       0xFF52AD
#define IR_9       0xFF4AB5

// ======================================================================
// LCD HELPERS
// ======================================================================

char gameCharFromMiniGame(PlayerMiniGame g) {
  switch (g) {
    case PG_MEM:    return 'A';
    case PG_LED:    return 'B';
    case PG_BUZ:    return 'C';
    case PG_US:     return 'D';
    case PG_RECALL: return 'A';
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

void showTopMessage(const String& msg) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(msg);
  drawBottomHUD();
}

void showTimedHUD(const char* tag) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(tag);
  lcd.print(" T:");
  if (timeLeftSec < 10) lcd.print("0");
  lcd.print(timeLeftSec);
  lcd.print("s");
  drawBottomHUD();
}

void startCountdown(int seconds) {
  timeLeftSec = seconds;
  responseEndTime = millis() + (unsigned long)seconds * 1000UL;
  lastCountdownTick = millis();
}

void updateCountdownUI() {
  if (timeLeftSec <= 0) return;

  unsigned long now = millis();
  if (now - lastCountdownTick >= 1000) {
    lastCountdownTick += 1000;
    timeLeftSec--;
    if (timeLeftSec < 0) timeLeftSec = 0;

    if (currentGame == PG_LED) showTimedHUD("LED");
    else if (currentGame == PG_BUZ) showTimedHUD("BUZ");
    else if (currentGame == PG_US)  showTimedHUD("US ");
    else if (currentGame == PG_RECALL) showTimedHUD("MEM");
  }
}

// ======================================================================
// SERIAL HANDLERS
// ======================================================================

void handleLine(const String& line);

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

void handleS_LEVEL(const String& rest) {
  currentLevel = rest.toInt();

  if (currentLevel > 18) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Max Level!");
    lcd.setCursor(0,1);
    lcd.print("Game Complete");
    pState = P_IDLE;
    alive = false;
  }
}

void handleS_MEM(const String& rest) {
  int comma = rest.indexOf(',');
  String num = (comma < 0 ? rest : rest.substring(comma+1));

  memoryNumber = num;
  currentGame = PG_MEM;
  pState = P_MEM_SHOW;

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Mem: ");
  lcd.print(memoryNumber);
  drawBottomHUD();
}

void handleS_TIME(const String& rest) {
  int comma = rest.indexOf(',');
  int t = (comma < 0 ? rest.toInt() : rest.substring(comma+1).toInt());

  startCountdown(t);

  if      (currentGame == PG_LED)    showTimedHUD("LED");
  else if (currentGame == PG_BUZ)    showTimedHUD("BUZ");
  else if (currentGame == PG_US)     showTimedHUD("US ");
  else if (currentGame == PG_RECALL) showTimedHUD("MEM");
}

void handleS_LED_START() {
  if (!alive) return;
  currentGame = PG_LED;
  pState = P_LED_INPUT;
  ledAnswer = "";
  showTopMessage("LED Ready...");
}

void handleS_BUZ_START() {
  if (!alive) return;
  currentGame = PG_BUZ;
  pState = P_BUZ_INPUT;
  buzAnswer = "";
  showTopMessage("BUZ Ready...");
}

void handleS_US_START(const String& rest) {
  if (!alive) return;

  currentGame = PG_US;
  pState = P_US_INPUT;

  int comma = rest.indexOf(',');
  int target = rest.substring(comma+1).toInt();

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Aim ");
  lcd.print(target);
  lcd.print("cm");
  drawBottomHUD();
}

void handleS_RECALL_START() {
  if (!alive) return;

  currentGame = PG_RECALL;
  pState = P_MEM_RECALL_INPUT;
  recallAnswer = "";

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Recall:");
  drawBottomHUD();
}

void handleS_REQ(const String& rest) {
  int c1 = rest.indexOf(',');
  int c2 = rest.indexOf(',',c1+1);

  if (c1<0 || c2<0) return;

  int id = rest.substring(c1+1,c2).toInt();
  String type = rest.substring(c2+1);

  if (id != PLAYER_ID || !alive) return;

  if (type == "LED") {
    mySerial.print("P_RESP,1,LED,");
    mySerial.print(ledAnswer);
    mySerial.print("\n");
    pState = P_WAIT_RESULT;
  }
  else if (type == "BUZ") {
    mySerial.print("P_RESP,1,BUZ,");
    mySerial.print(buzAnswer);
    mySerial.print("\n");
    pState = P_WAIT_RESULT;
  }
  else if (type == "US") {
    long duration;
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    duration = pulseIn(echoPin, HIGH);

    int dist = duration * 0.0343 / 2;

    mySerial.print("P_RESP,1,US,");
    mySerial.print(dist);
    mySerial.print("\n");

    pState = P_WAIT_RESULT;
  }
  else if (type == "MEM") {
    mySerial.print("P_RESP,1,MEM,");
    mySerial.print(recallAnswer);
    mySerial.print("\n");
    pState = P_WAIT_RESULT;
  }
}

void handleS_RES(const String& rest) {
  int c1 = rest.indexOf(',');
  int c2 = rest.indexOf(',',c1+1);
  int c3 = rest.indexOf(',',c2+1);

  if (c1<0 || c2<0 || c3<0) return;

  int id = rest.substring(c1+1,c2).toInt();
  String result = rest.substring(c3+1);

  if (id != PLAYER_ID) return;

  if (result == "OK") {
    alive = true;
    showTopMessage("OK!");
    pState = P_IDLE;
  }
  else {
    alive = false;
    showTopMessage("ELIMINATED!");
    pState = P_ELIMINATED;
  }
}

void handleLine(const String& line) {
  int c = line.indexOf(',');
  String cmd = (c<0 ? line : line.substring(0,c));
  String rest = (c<0 ? "" : line.substring(c+1));

  if      (cmd == "S_LEVEL")        handleS_LEVEL(rest);
  else if (cmd == "S_MEM")          handleS_MEM(rest);
  else if (cmd == "S_TIME")         handleS_TIME(rest);
  else if (cmd == "S_LED_START")    handleS_LED_START();
  else if (cmd == "S_BUZ_START")    handleS_BUZ_START();
  else if (cmd == "S_US_START")     handleS_US_START(rest);
  else if (cmd == "S_RECALL_START") handleS_RECALL_START();
  else if (cmd == "S_REQ")          handleS_REQ(rest);
  else if (cmd == "S_RES")          handleS_RES(rest);
}

// ======================================================================
// INPUT HANDLERS
// ======================================================================

void initButtons() {
  for (int i=0;i<4;i++) {
    pinMode(btnPins[i], INPUT_PULLUP);
    lastBtnState[i]  = digitalRead(btnPins[i]);
    lastBtnChange[i] = millis();
  }
}

char charFromButtonIndex(int i) {
  if (i==0) return 'R';
  if (i==1) return 'G';
  if (i==2) return 'Y';
  if (i==3) return 'B';
  return '?';
}

void handleLEDInput() {
  if (!alive) return;
  unsigned long now = millis();
  if (now > responseEndTime) return;

  for (int i=0;i<4;i++) {
    int reading = digitalRead(btnPins[i]);

    if (reading != lastBtnState[i]) {
      if (now - lastBtnChange[i] > btnDebounce) {
        lastBtnChange[i] = now;
        lastBtnState[i] = reading;

        if (reading == LOW)
          ledAnswer += charFromButtonIndex(i);
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
    }
    else if (y > JOY_HIGH_T) {
      buzAnswer += 'L';
      lastJoyInputTime = now;
    }
  }
}

// IR input for recall game
void handleMemRecallInput() {
  if (millis() > responseEndTime) return;

  if (IrReceiver.decode()) {
    unsigned long code = IrReceiver.decodedIRData.decodedRawData;

    if (code == IR_POWER) {
      responseEndTime = millis();
      IrReceiver.resume();
      return;
    }

    if (recallAnswer.length() < memoryNumber.length()) {
      if      (code==IR_0) recallAnswer+='0';
      else if (code==IR_1) recallAnswer+='1';
      else if (code==IR_2) recallAnswer+='2';
      else if (code==IR_3) recallAnswer+='3';
      else if (code==IR_4) recallAnswer+='4';
      else if (code==IR_5) recallAnswer+='5';
      else if (code==IR_6) recallAnswer+='6';
      else if (code==IR_7) recallAnswer+='7';
      else if (code==IR_8) recallAnswer+='8';
      else if (code==IR_9) recallAnswer+='9';

      lcd.setCursor(7,0);
      lcd.print(recallAnswer);
    }

    IrReceiver.resume();
  }
}

// ======================================================================
// SETUP
// ======================================================================

void setup() {
  lcd.init();
  lcd.backlight();

  mySerial.begin(9600);
  IrReceiver.begin(irPin, ENABLE_LED_FEEDBACK);

  initButtons();
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  alive = true;
  currentLevel = 1;
  currentGame = PG_NONE;
  pState = P_IDLE;

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Player ");
  lcd.print(PLAYER_ID);

  lcd.setCursor(0,1);
  lcd.print("Waiting Simon...");
}

// ======================================================================
// LOOP
// ======================================================================

void loop() {

  pollSerial();

  if (pState == P_LED_INPUT ||
      pState == P_BUZ_INPUT ||
      pState == P_US_INPUT ||
      pState == P_MEM_RECALL_INPUT) {
    updateCountdownUI();
  }

  switch (pState) {
    case P_IDLE: break;

    case P_MEM_SHOW: break;

    case P_LED_INPUT:
      handleLEDInput();
      break;

    case P_BUZ_INPUT:
      handleBuzzerInput();
      break;

    case P_US_INPUT:
      // no loop input needed
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
