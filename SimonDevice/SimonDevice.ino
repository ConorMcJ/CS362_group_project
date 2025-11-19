// I'm gonna comment all the pins etc so it is easier to adjust it to the hardware setup.

#include <LiquidCrystal.h>
#include <SoftwareSerial.h>

// PINS / HARDWARE

// we can just adjust pins if needed to match wiring:
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);

// Shared serial bus to all three players
SoftwareSerial mySerial(10, 11);

// Joystick for menu control
const int joyX  = A1;   // LEFT / RIGHT
const int joyY  = A2;   // UP / DOWN
const int joySW = 12;   // Joystick button (opens menu)

const int LOW_T  = 300;
const int HIGH_T = 700;

unsigned long lastJoy   = 0;
const unsigned long joyDelay = 200;

// Simon LEDs for LED mini-game (RGYB)
const int ledPins[4] = {
  8,   // R
  9,   // G
  13,  // Y
  A3   // B (analog pin used as digital for now but if space, we shift to digital as needed)
};

// Buzzer pin
const int buzzerPin = 4;


// GAME GLOBALS

int level      = 1;
bool gameActive = false;

String memoryNumber = "";
String ledPattern   = "";
String buzPattern   = "";

unsigned long phaseStart = 0;

// Player alive flags (1..3 used, index 0 unused) makes numbering easy
bool alive[4] = { false, true, true, true };

// Serial buffer for line based protocol
String serialBuffer = "";

// Token engine
int currentPlayerId       = 1;
bool waitingForResponse   = false;
unsigned long tokenStart  = 0;
const unsigned long tokenTimeout = 3000;  // 3s to reply when requested

// LED pattern timing
bool ledPhaseInit       = false;
bool ledShowingPattern  = false;
bool ledResponsePhase   = false;
int  ledIndex           = 0;
unsigned long lastLedStep = 0;
const unsigned long ledStepInterval = 600; // ms between LEDs

// BUZZER pattern timing
bool buzPhaseInit       = false;
bool buzShowingPattern  = false;
bool buzResponsePhase   = false;
int  buzIndex           = 0;
unsigned long lastBuzStep = 0;

// Buzzer durations
const unsigned long shortTone = 200;   // H: high + short
const unsigned long longTone  = 500;   // L: low  + long

// Player answers for possible later use, not really using it rn tho so
String playerAnswers[4];

// Minigame identifier
enum MiniGame { MG_NONE, MG_LED, MG_BUZ, MG_US, MG_MEM };
MiniGame currentMiniGame = MG_NONE;

// Simon main states
enum SimonState {
  MENU,
  WAITING_START,
  SEND_MEMORY_NUM,
  LED_GAME,
  BUZ_GAME,
  US_GAME,
  RECALL_GAME,
  PROCESS_RESULTS,
  GAME_OVER,
  PAUSED
};
SimonState simonState = MENU;


// TIMER / HUD

// Response time per level: round(6 + 1.5 * level)
int computeResponseTime(int lvl) {
  float t = 6.0 + 1.5 * lvl;
  return (int)(t + 0.5);  // round to nearest int
}

// Timer used during response windows
int responseTimeSec          = 0;
unsigned long responseWindow = 0;    // in ms
unsigned long lastCountdownTick = 0;

// Drawing two-line HUD during response window as we discussed a few weeks ago
// Top:  G:LED L:03
// Bottom: T:08s
void showHUD(const char* gameTag) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("G:");
  lcd.print(gameTag);
  lcd.print(" L:");
  if (level < 10) lcd.print("0");
  lcd.print(level);
  lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print("T:");
  if (responseTimeSec < 10) lcd.print("0");
  lcd.print(responseTimeSec);
  lcd.print("s   ");
}

// MENU SYSTEM

bool menuOpen = true;

void showMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Close  Start");
  lcd.setCursor(0, 1);
  lcd.print("End    Pause");
  simonState = MENU;
  menuOpen = true;
}

void playStartMelody() {
  // simple 4-note tune: C5, E5, G5, C6 for the intro like discussed
  int notes[]     = { 523, 659, 784, 1047 };
  int durations[] = { 150, 150, 150, 250 };
  int pause       = 60;

  for (int i = 0; i < 4; i++) {
    tone(buzzerPin, notes[i], durations[i]);
    delay(durations[i] + pause);
  }
  noTone(buzzerPin);
}

bool startShown    = false;
bool memSent       = false;
bool gameOverShown = false;

void startGame() {
  level      = 1;
  gameActive = true;

  // Everyone back in
  alive[1] = true;
  alive[2] = true;
  alive[3] = true;

  // Resetting LED flags
  ledPhaseInit      = false;
  ledShowingPattern = false;
  ledResponsePhase  = false;

  // Resetting BUZ flags
  buzPhaseInit      = false;
  buzShowingPattern = false;
  buzResponsePhase  = false;

  waitingForResponse = false;
  currentPlayerId    = 1;
  serialBuffer       = "";

  startShown    = false;
  memSent       = false;
  gameOverShown = false;
  currentMiniGame = MG_NONE;

  // Let players know current level
  mySerial.println("S_LEVEL," + String(level));

  // Optional short melody at start. we can tak this out later.
  playStartMelody();

  lcd.clear();
  lcd.print("Starting...");
  phaseStart = millis();

  simonState = WAITING_START;
}

void endGame() {
  lcd.clear();
  lcd.print("Ending...");
  phaseStart = millis();
  simonState = GAME_OVER;
}

void togglePause() {
  if (simonState != PAUSED) {
    lcd.clear();
    lcd.print("Paused");
    simonState = PAUSED;
  } else {
    lcd.clear();
    lcd.print("Resuming...");
    // resume into memory number phase for current level
    memSent        = false;
    ledPhaseInit   = false;
    buzPhaseInit   = false;
    simonState     = SEND_MEMORY_NUM;
  }
}

void handleMenuButtons() {
  unsigned long now = millis();

  // Joystick press opens menu if currently closed
  if (digitalRead(joySW) == LOW && now - lastJoy > joyDelay) {
    lastJoy = now;

    if (!menuOpen) {
      menuOpen = true;
      showMenu();
    }
    return;
  }

  if (!menuOpen) return;

  int x = analogRead(joyX);
  int y = analogRead(joyY);

  // UP = Close menu
  if (y < LOW_T && now - lastJoy > joyDelay) {
    menuOpen = false;
    lcd.clear();
    lastJoy = now;
    return;
  }

  // DOWN = Start game
  if (y > HIGH_T && now - lastJoy > joyDelay) {
    startGame();
    menuOpen = false;
    lastJoy = now;
    return;
  }

  // LEFT = End game
  if (x < LOW_T && now - lastJoy > joyDelay) {
    endGame();
    lastJoy = now;
    return;
  }

  // RIGHT = Pause / Resume
  if (x > HIGH_T && now - lastJoy > joyDelay) {
    togglePause();
    lastJoy = now;
    return;
  }
}


// START SCREEN

void showStartScreen() {
  unsigned long now = millis();

  if (!startShown) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Get Ready!");
    lcd.setCursor(0, 1);
    lcd.print("Level ");
    lcd.print(level);

    startShown = true;
    phaseStart = now;
  }

  if (now - phaseStart >= 1500) {
    startShown = false;
    simonState = SEND_MEMORY_NUM;
  }
}


// MEMORY NUMBER GAME

void generateMemoryNumber() {
  memoryNumber = "";
  for (int i = 0; i < 3; i++) {
    memoryNumber += String(random(0, 10));
  }
}

void runMemoryNumberPhase() {
  unsigned long now = millis();

  if (!memSent) {
    generateMemoryNumber();

    lcd.clear();
    lcd.print("Mem: ");
    lcd.print(memoryNumber);

    // here I am basically telling players to show this on their LCD
    mySerial.println("S_MEM," + memoryNumber);

    memSent    = true;
    phaseStart = now;
    return;
  }

  if (now - phaseStart >= 2000) {
    memSent = false;
    simonState = LED_GAME;
  }
}


// LED MINI-GAME

void turnOffAllLEDs() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(ledPins[i], LOW);
  }
}

void setLED(char c) {
  turnOffAllLEDs();
  if (c == 'R') digitalWrite(ledPins[0], HIGH);
  if (c == 'G') digitalWrite(ledPins[1], HIGH);
  if (c == 'Y') digitalWrite(ledPins[2], HIGH);
  if (c == 'B') digitalWrite(ledPins[3], HIGH);
}

void generateLEDPattern(int len) {
  ledPattern = "";
  char colors[4] = { 'R', 'G', 'Y', 'B' };
  for (int i = 0; i < len; i++) {
    ledPattern += colors[random(0, 4)];
  }
}

void runLEDGame() {
  unsigned long now = millis();

  // Initilaizing LED game
  if (!ledPhaseInit) {
    lcd.clear();
    lcd.print("LED Pattern...");

    generateLEDPattern(level + 2);

    ledIndex          = 0;
    lastLedStep       = now;
    ledPhaseInit      = true;
    ledShowingPattern = true;
    ledResponsePhase  = false;

    playerAnswers[1] = "";
    playerAnswers[2] = "";
    playerAnswers[3] = "";

    // Notify players that LED game is starting
    mySerial.println("S_LED_START");

    return;
  }

  // show pattern on Simon LEDs
  if (ledShowingPattern) {
    if (ledIndex < ledPattern.length()) {
      if (now - lastLedStep >= ledStepInterval) {
        lastLedStep = now;
        setLED(ledPattern[ledIndex]);
        ledIndex++;
      }
    } else {
      turnOffAllLEDs();
      ledShowingPattern = false;
      ledResponsePhase  = true;

      // Start response window here
      currentMiniGame   = MG_LED;
      responseTimeSec   = computeResponseTime(level);
      responseWindow    = (unsigned long)responseTimeSec * 1000UL;
      phaseStart        = now;
      lastCountdownTick = now;

      // HUD & broadcast timer to players
      showHUD("LED");
      mySerial.println("S_TIME," + String(responseTimeSec));
    }
    return;
  }

  // players input & countdown runs
  if (ledResponsePhase) {
    // countdown tick
    if (responseTimeSec > 0 && now - lastCountdownTick >= 1000) {
      lastCountdownTick += 1000;
      responseTimeSec--;
      showHUD("LED");
    }

    // window done?
    if (now - phaseStart >= responseWindow || responseTimeSec <= 0) {
      ledResponsePhase   = false;
      currentPlayerId    = 1;
      waitingForResponse = false;
      serialBuffer       = "";
      simonState         = PROCESS_RESULTS;
    }
    return;
  }
}


// BUZZER MINI-GAME
// Pattern uses symbols:
//   'H' = high tone + short duration
//   'L' = low tone  + long duration
// Example: "HLLH" – players hear this and input H/L with joystick.

void generateBuzzerPattern(int len) {
  buzPattern = "";
  char symbols[2] = { 'H', 'L' };
  for (int i = 0; i < len; i++) {
    buzPattern += symbols[random(0, 2)];
  }
}

void playBuzzerSymbol(char s) {
  if (s == 'H') {
    tone(buzzerPin, 900, shortTone);
  } else {
    tone(buzzerPin, 400, longTone);
  }
}

void runBuzzerGame() {
  unsigned long now = millis();

  // Initializing buzzer game
  if (!buzPhaseInit) {
    lcd.clear();
    lcd.print("Buzzer...");

    generateBuzzerPattern(level + 2);

    buzIndex        = 0;
    lastBuzStep     = 0;
    buzPhaseInit      = true;
    buzShowingPattern = true;
    buzResponsePhase  = false;

    playerAnswers[1] = "";
    playerAnswers[2] = "";
    playerAnswers[3] = "";

    // Notify players buzzer game is starting
    mySerial.println("S_BUZ_START");

    return;
  }

  // play buzzer pattern
  if (buzShowingPattern) {
    if (buzIndex < buzPattern.length()) {
      if (now >= lastBuzStep) {
        char symbol = buzPattern[buzIndex++];
        playBuzzerSymbol(symbol);

        unsigned long dur = (symbol == 'H') ? shortTone : longTone;
        lastBuzStep = now + dur + 100; // gap between beeps
      }
    } else {
      buzShowingPattern = false;
      buzResponsePhase  = true;

      // Start response window
      currentMiniGame   = MG_BUZ;
      responseTimeSec   = computeResponseTime(level);
      responseWindow    = (unsigned long)responseTimeSec * 1000UL;
      phaseStart        = now;
      lastCountdownTick = now;

      showHUD("BUZ");
      mySerial.println("S_TIME," + String(responseTimeSec));
    }
    return;
  }

  // players input & countdown runs
  if (buzResponsePhase) {
    if (responseTimeSec > 0 && now - lastCountdownTick >= 1000) {
      lastCountdownTick += 1000;
      responseTimeSec--;
      showHUD("BUZ");
    }

    if (now - phaseStart >= responseWindow || responseTimeSec <= 0) {
      buzResponsePhase   = false;
      currentPlayerId    = 1;
      waitingForResponse = false;
      serialBuffer       = "";
      simonState         = PROCESS_RESULTS;
    }
    return;
  }
}


// TOKEN-BASED RESULT HANDLING

void parseResponse(MiniGame mg);

void advanceOrEnd();

void handleResult() {
  unsigned long now = millis();
  bool finishedAll = false;

  // LED MINI-GAME
  if (currentMiniGame == MG_LED) {
    if (!waitingForResponse) {
      while (currentPlayerId <= 3 && !alive[currentPlayerId]) currentPlayerId++;
      if (currentPlayerId > 3) {
        finishedAll = true;
      } else {
        mySerial.println("S_REQ," + String(currentPlayerId) + ",LED");
        tokenStart         = now;
        waitingForResponse = true;
        serialBuffer       = "";
      }
      if (finishedAll) {
        advanceOrEnd();
      }
      return;
    }

    while (mySerial.available()) {
      char c = mySerial.read();
      if (c == '\n') {
        parseResponse(MG_LED);
        return;
      } else if (c != '\r') {
        serialBuffer += c;
      }
    }

    if (waitingForResponse && now - tokenStart >= tokenTimeout) {
      alive[currentPlayerId] = false;
      mySerial.println("S_RES," + String(currentPlayerId) + ",LED,FAIL");
      waitingForResponse = false;
      currentPlayerId++;
    }
    return;
  }

  // BUZZER MINI-GAME
  if (currentMiniGame == MG_BUZ) {
    if (!waitingForResponse) {
      while (currentPlayerId <= 3 && !alive[currentPlayerId]) currentPlayerId++;
      if (currentPlayerId > 3) {
        finishedAll = true;
      } else {
        mySerial.println("S_REQ," + String(currentPlayerId) + ",BUZ");
        tokenStart         = now;
        waitingForResponse = true;
        serialBuffer       = "";
      }
      if (finishedAll) {
        advanceOrEnd();
      }
      return;
    }

    while (mySerial.available()) {
      char c = mySerial.read();
      if (c == '\n') {
        parseResponse(MG_BUZ);
        return;
      } else if (c != '\r') {
        serialBuffer += c;
      }
    }

    if (waitingForResponse && now - tokenStart >= tokenTimeout) {
      alive[currentPlayerId] = false;
      mySerial.println("S_RES," + String(currentPlayerId) + ",BUZ,FAIL");
      waitingForResponse = false;
      currentPlayerId++;
    }
    return;
  }

  // US + MEM not implemented yet – So for now we hust go to game over but later I will implement this as well I guess
  simonState = GAME_OVER;
}


// PARSE PLAYER RESPONSES

void parseResponse(MiniGame mg) {
  int c1 = serialBuffer.indexOf(',');
  if (c1 < 0) { serialBuffer = ""; waitingForResponse = false; return; }

  int c2 = serialBuffer.indexOf(',', c1 + 1);
  if (c2 < 0) { serialBuffer = ""; waitingForResponse = false; return; }

  int c3 = serialBuffer.indexOf(',', c2 + 1);
  if (c3 < 0) { serialBuffer = ""; waitingForResponse = false; return; }

  String prefix = serialBuffer.substring(0, c1);
  int    id     = serialBuffer.substring(c1 + 1, c2).toInt();
  String type   = serialBuffer.substring(c2 + 1, c3);
  String ans    = serialBuffer.substring(c3 + 1);

  if (prefix != "P_RESP" || id != currentPlayerId) {
    serialBuffer = "";
    return;
  }

  if (mg == MG_LED && type == "LED") {
    if (ans == ledPattern) {
      mySerial.println("S_RES," + String(id) + ",LED,OK");
    } else {
      mySerial.println("S_RES," + String(id) + ",LED,FAIL");
      alive[id] = false;
    }
  }

  if (mg == MG_BUZ && type == "BUZ") {
    if (ans == buzPattern) {
      mySerial.println("S_RES," + String(id) + ",BUZ,OK");
    } else {
      mySerial.println("S_RES," + String(id) + ",BUZ,FAIL");
      alive[id] = false;
    }
  }

  waitingForResponse = false;
  currentPlayerId++;
  serialBuffer = "";
}


// ADVANCE LEVEL OR END GAME

void advanceOrEnd() {
  bool anyoneAlive = false;
  for (int i = 1; i <= 3; i++) {
    if (alive[i]) {
      anyoneAlive = true;
      break;
    }
  }

  if (!anyoneAlive) {
    simonState = GAME_OVER;
    return;
  }

  // Finished LED mini-game, go to BUZ mini-game
  if (currentMiniGame == MG_LED) {
    buzPhaseInit       = false;
    buzShowingPattern  = false;
    buzResponsePhase   = false;
    waitingForResponse = false;
    currentPlayerId    = 1;
    serialBuffer       = "";
    simonState         = BUZ_GAME;
    return;
  }

  // Finished BUZ mini-game, go to next level
  if (currentMiniGame == MG_BUZ) {
    level++;

    // Let players know new level
    mySerial.println("S_LEVEL," + String(level));

    // Reset for next cycle
    startShown    = false;
    memSent       = false;

    ledPhaseInit      = false;
    ledShowingPattern = false;
    ledResponsePhase  = false;

    buzPhaseInit      = false;
    buzShowingPattern = false;
    buzResponsePhase  = false;

    waitingForResponse = false;
    currentPlayerId    = 1;
    serialBuffer       = "";

    simonState = SEND_MEMORY_NUM;  // or later: go to US game first
  }
}


// GAME OVER

void displayGameOver() {
  unsigned long now = millis();

  if (!gameOverShown) {
    lcd.clear();
    lcd.print("GAME OVER");
    gameOverShown = true;
    phaseStart    = now;
  }

  if (now - phaseStart >= 2000) {
    gameOverShown = false;
    showMenu();
  }
}


// SETUP / LOOP

void setup() {
  lcd.begin(16, 2);
  mySerial.begin(9600);

  pinMode(joySW, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);

  for (int i = 0; i < 4; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  randomSeed(analogRead(A0));

  showMenu();
}

void loop() {
  handleMenuButtons();

  switch (simonState) {
    case MENU:
      break;

    case WAITING_START:
      showStartScreen();
      break;

    case SEND_MEMORY_NUM:
      runMemoryNumberPhase();
      break;

    case LED_GAME:
      runLEDGame();
      break;

    case BUZ_GAME:
      runBuzzerGame();
      break;

    case US_GAME:
      // Placeholder for ultrasonic mini-game
      simonState = RECALL_GAME;
      break;

    case RECALL_GAME:
      // Placeholder for memory recall IR mini-game
      simonState = PROCESS_RESULTS;
      break;

    case PROCESS_RESULTS:
      handleResult();
      break;

    case GAME_OVER:
      displayGameOver();
      break;

    case PAUSED:
      // nothing, waits for togglePause()
      break;
  }
}
