// I'm gonna comment all the pins etc so it is easier to adjust it to the hardware setup.

#include <LiquidCrystal.h>
#include <SoftwareSerial.h>

// LCD + Serial ---- as conner mentioned, this could be done in less pins using i2c.
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);
SoftwareSerial mySerial(10, 11);   // Simon - shared RX/TX bus to all 3 players like we discussed

// Joystick setup
const int joyX = A1;   // LEFT/RIGHT
const int joyY = A2;   // UP/DOWN
const int joySW = 12;  // Joystick button --- added this cuz we might have space. will have to readjust code in case there is no space.

const int LOW_T  = 300;
const int HIGH_T = 700;

unsigned long lastJoy = 0;
const unsigned long joyDelay = 200;

// Simon LEDs for LED mini-game
const int ledPins[4] = {
  8,   // R
  9,   // G
  13,  // Y
  A3   // B ---- i chose A3 but depending on pin availibility we can choose didital.
};

// Simon buzzer
const int buzzerPin = 4;

// Game globals
int level = 1;
bool gameActive = false;

String memoryNumber = "";
String ledPattern   = "";
String buzPattern   = "";

unsigned long phaseStart = 0;

// Per player alive flag. I'm only using 1-3. 0 is unsused, makes intuitive coding.
bool alive[4] = { false, true, true, true };

// Serial buffer for line based protocol like we discussed
String serialBuffer = "";

// Token engine for collecting answers ---- Conner look over this, I tried implementing it.
int currentPlayerId = 1;
bool waitingForResponse = false;
unsigned long tokenStart = 0;
const unsigned long tokenTimeout = 3000;

// LED pattern timing
bool ledPhaseInit       = false;
bool ledShowingPattern  = false;
bool ledResponsePhase   = false;
int  ledIndex           = 0;
unsigned long lastLedStep = 0;
const unsigned long ledStepInterval   = 600;
const unsigned long ledResponseWindow = 12000;

// BUZZER pattern timing
bool buzPhaseInit       = false;
bool buzShowingPattern  = false;
bool buzResponsePhase   = false;
int  buzIndex           = 0;
unsigned long lastBuzStep = 0;

// buzzer short/long durations ---- so i was thinking of making buzzer pitch and duration vary. we can make this into a 4-bit but I just did it for audio assist. game is still 2 bit.
const unsigned long shortTone        = 200;
const unsigned long longTone         = 500;
const unsigned long buzResponseWindow = 12000;

// Player answers for current minigame. might end up needing this but idk, not really at this stage.
String playerAnswers[4];

// Minigame identifier.
enum MiniGame { MG_NONE, MG_LED, MG_BUZ, MG_US, MG_MEM };
MiniGame currentMiniGame = MG_NONE;

// Main Simon states
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


// SETUP
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


// LOOP

// didn't implement the rest so it easy to test. I will however, put those implementation in a separate file.
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
      currentMiniGame = MG_LED;
      runLEDGame();
      break;

    case BUZ_GAME:
      currentMiniGame = MG_BUZ;
      runBuzzerGame();
      break;

    case US_GAME:
      // will use it later for ultrasonic game
      simonState = RECALL_GAME;
      break;

    case RECALL_GAME:
      // will use it later for memory recall game
      simonState = PROCESS_RESULTS;
      break;

    case PROCESS_RESULTS:
      handleResult();
      break;

    case GAME_OVER:
      displayGameOver();
      break;

    case PAUSED:
      break;
  }
}


// MENU SYSTEM

bool menuOpen = true;

void showMenu() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Close  Start");
  lcd.setCursor(0,1);
  lcd.print("End    Pause");
  simonState = MENU;
  menuOpen = true;
}

void handleMenuButtons() {
  unsigned long now = millis();

  if (digitalRead(joySW) == LOW && now - lastJoy > joyDelay) {
    lastJoy = now;

    if (!menuOpen) {
      menuOpen = true;
      showMenu();
    }
    return;
  }

  if (!menuOpen) return;   // this is so when the menu closed, we ignore directions

  int x = analogRead(joyX);
  int y = analogRead(joyY);

  // UP = Close menu
  if (y < LOW_T && now - lastJoy > joyDelay) {
    menuOpen = false;
    lcd.clear();
    lastJoy = now;
    return;
  }

  // DOWN = START
  if (y > HIGH_T && now - lastJoy > joyDelay) {
    startGame();
    menuOpen = false;
    lastJoy = now;
    return;
  }

  // LEFT = END game
  if (x < LOW_T && now - lastJoy > joyDelay) {
    endGame();
    lastJoy = now;
    return;
  }

  // RIGHT = PAUSE / RESUME
  if (x > HIGH_T && now - lastJoy > joyDelay) {
    togglePause();
    lastJoy = now;
    return;
  }
}

void startGame() {
  level = 1;
  gameActive = true;

  // resetting all players to alive at start of game
  alive[1] = true;
  alive[2] = true;
  alive[3] = true;

  // resetting flags for a fresh run
  ledPhaseInit      = false;
  ledShowingPattern = false;
  ledResponsePhase  = false;

  buzPhaseInit      = false;
  buzShowingPattern = false;
  buzResponsePhase  = false;

  waitingForResponse = false;
  currentPlayerId    = 1;
  serialBuffer       = "";

  startShown   = false;
  memSent      = false;
  gameOverShown = false;
  currentMiniGame = MG_NONE;

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
    // resume into memory number phase of current level
    memSent      = false;
    ledPhaseInit = false;
    buzPhaseInit = false;
    simonState   = SEND_MEMORY_NUM;
  }
}


// START SCREEN
bool startShown = false;

void showStartScreen() {
  unsigned long now = millis();

  if (!startShown) {
    lcd.clear();
    lcd.print("Get Ready!");
    lcd.setCursor(0,1);
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
bool memSent = false;

void generateMemoryNumber() {
  memoryNumber = "";
  for (int i = 0; i < 3; i++) {
    memoryNumber += String(random(0,10));
  }
}

void runMemoryNumberPhase() {
  unsigned long now = millis();

  if (!memSent) {
    generateMemoryNumber();

    lcd.clear();
    lcd.print("Mem: ");
    lcd.print(memoryNumber);

    // broadcast to all players. so players show this on their LCDs using the broadcast.
    mySerial.println("S_MEM," + memoryNumber);

    memSent   = true;
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
  for (int i = 0; i < 4; i++) digitalWrite(ledPins[i], LOW);
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
  char colors[4] = {'R','G','Y','B'};
  for (int i = 0; i < len; i++) {
    ledPattern += colors[random(0,4)];
  }
}

void runLEDGame() {
  unsigned long now = millis();

  // Initializing the LED game
  if (!ledPhaseInit) {
    lcd.clear();
    lcd.print("LED Pattern...");

    generateLEDPattern(level + 2);

    ledIndex     = 0;
    lastLedStep  = now;

    ledPhaseInit      = true;
    ledShowingPattern = true;
    ledResponsePhase  = false;

    playerAnswers[1] = "";
    playerAnswers[2] = "";
    playerAnswers[3] = "";

    return;
  }

  // showing pattern
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
      lcd.clear();
      lcd.print("Your turn...");
      phaseStart = now;
    }
    return;
  }

  //  12 sec window for response validation etc
  if (ledResponsePhase) {
    if (now - phaseStart >= ledResponseWindow) {
      ledResponsePhase   = false;
      currentPlayerId    = 1;
      waitingForResponse = false;
      serialBuffer       = "";
      currentMiniGame    = MG_LED;
      simonState         = PROCESS_RESULTS;
    }
    return;
  }
}



//  BUZZER MINI-GAME
// Pattern is now only 2 symbols:
// 'H' = high tone + short duration
// 'L' = low tone + long duration
// Example pattern: "HLLH"
void generateBuzzerPattern(int len) {
  buzPattern = "";
  char symbols[2] = {'H','L'};
  for (int i = 0; i < len; i++) {
    buzPattern += symbols[random(0,2)];
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

  // Initializing
  if (!buzPhaseInit) {
    lcd.clear();
    lcd.print("Buzzer...");

    generateBuzzerPattern(level + 2);

    buzIndex        = 0;
    lastBuzStep     = 0;  // so first step triggers immediately

    buzPhaseInit      = true;
    buzShowingPattern = true;
    buzResponsePhase  = false;

    playerAnswers[1] = "";
    playerAnswers[2] = "";
    playerAnswers[3] = "";

    return;
  }

  // showing pattern
  if (buzShowingPattern) {
    if (buzIndex < buzPattern.length()) {
      if (now >= lastBuzStep) {
        char symbol = buzPattern[buzIndex++];
        playBuzzerSymbol(symbol);

        unsigned long dur = (symbol == 'H') ? shortTone : longTone;
        lastBuzStep = now + dur + 100;  // small gap after each beep
      }
    }
    else {
      buzShowingPattern = false;
      buzResponsePhase  = true;
      lcd.clear();
      lcd.print("Your turn...");
      phaseStart = now;
    }
    return;
  }

  // 12s response window again
  if (buzResponsePhase) {
    if (now - phaseStart >= buzResponseWindow) {
      buzResponsePhase   = false;
      currentPlayerId    = 1;
      waitingForResponse = false;
      serialBuffer       = "";
      currentMiniGame    = MG_BUZ;
      simonState         = PROCESS_RESULTS;
    }
    return;
  }
}



// TOKEN-BASED RESULT HANDLING

void handleResult() {
  unsigned long now = millis();
  bool finishedAll = false;

  //    LED MINI-GAME
  if (currentMiniGame == MG_LED) {

    if (!waitingForResponse) {
      while (currentPlayerId <= 3 && !alive[currentPlayerId]) currentPlayerId++;
      if (currentPlayerId > 3) {
        finishedAll = true;
      } else {
        mySerial.println("S_REQ," + String(currentPlayerId) + ",LED");
        tokenStart        = now;
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


  //    BUZZER MINI-GAME
  if (currentMiniGame == MG_BUZ) {

    if (!waitingForResponse) {
      while (currentPlayerId <= 3 && !alive[currentPlayerId]) currentPlayerId++;
      if (currentPlayerId > 3) {
        finishedAll = true;
      } else {
        mySerial.println("S_REQ," + String(currentPlayerId) + ",BUZ");
        tokenStart        = now;
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

  // US + MEM not implemented yet so we default to game over. I will try to upload that soon as well but not today for testing.
  simonState = GAME_OVER;
}



// PARSE PLAYER RESPONSES
void parseResponse(MiniGame mg) {
  int c1 = serialBuffer.indexOf(',');
  if (c1 < 0) { serialBuffer = ""; waitingForResponse = false; return; }

  int c2 = serialBuffer.indexOf(',', c1+1);
  if (c2 < 0) { serialBuffer = ""; waitingForResponse = false; return; }

  int c3 = serialBuffer.indexOf(',', c2+1);
  if (c3 < 0) { serialBuffer = ""; waitingForResponse = false; return; }

  String prefix = serialBuffer.substring(0, c1);
  int    id     = serialBuffer.substring(c1+1, c2).toInt();
  String type   = serialBuffer.substring(c2+1, c3);
  String ans    = serialBuffer.substring(c3+1);

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

  // If we just finished LED game and players still alive → go to BUZ game
  if (currentMiniGame == MG_LED) {
    buzPhaseInit      = false;
    buzShowingPattern = false;
    buzResponsePhase  = false;
    waitingForResponse = false;
    currentPlayerId    = 1;
    serialBuffer       = "";
    simonState         = BUZ_GAME;
    return;
  }

  // If we just finished BUZ game and players still alive → next level
  if (currentMiniGame == MG_BUZ) {
    level++;

    // reset for next level cycle
    startShown   = false;
    memSent      = false;

    ledPhaseInit      = false;
    ledShowingPattern = false;
    ledResponsePhase  = false;

    buzPhaseInit      = false;
    buzShowingPattern = false;
    buzResponsePhase  = false;

    waitingForResponse = false;
    currentPlayerId    = 1;
    serialBuffer       = "";

    simonState = SEND_MEMORY_NUM; // BUt this can be US after implemenation and etcetc
  }
}


// GAME OVER
bool gameOverShown = false;

void displayGameOver() {
  unsigned long now = millis();

  if (!gameOverShown) {
    lcd.clear();
    lcd.print("GAME OVER");
    gameOverShown = true;
    phaseStart = now;
  }

  if (now - phaseStart >= 2000) {
    gameOverShown = false;
    showMenu();
  }
}
