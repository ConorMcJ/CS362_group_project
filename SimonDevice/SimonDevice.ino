#include <LiquidCrystal.h>
#include <SoftwareSerial.h>

// LCD + Serial
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);
SoftwareSerial mySerial(10, 11);

// Joystick setup
// Directions
const int joyX = A1;   // LEFT/RIGHT
const int joyY = A2;   // UP/DOWN
const int joySW = 12;  // Joystick button

// Thresholds to detect direction
const int LOW_T = 300;
const int HIGH_T = 700;

// Menu navigation debounce
unsigned long lastJoy = 0;
const unsigned long joyDelay = 200;

// LED pins
const int ledPins[4] = {8, 9, 10, 11}; // R, G, Y, B in some order, we can tweak later

// Game globals
int level = 1;
bool gameActive = false;

String memoryNumber = "";
String ledPattern = "";

unsigned long lastAction = 0;

// Timing / phase control
unsigned long phaseStart = 0;

// Start screen flags
bool startScreenShown = false;

// Memory number flags
bool memSent = false;

// LED game flags/state
bool ledPhaseInit = false;
bool ledShowingPattern = false;
bool ledResponsePhase = false;
int ledIndex = 0;
unsigned long lastLedStep = 0;
const unsigned long ledStepInterval = 600;     // how fast we step through pattern
const unsigned long ledResponseWindow = 12000; // 12 second gap for responses

// Player response (for now one player)
bool player1Responded = false;
String player1Input = "";
String serialBuffer = "";

// Game over flags
bool gameOverShown = false;

// Main States
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

// just to know which mini-game's result we're processing later
enum MiniGame {
  MG_NONE,
  MG_LED,
  MG_BUZ,
  MG_US,
  MG_MEM
};

MiniGame currentMiniGame = MG_NONE;


// Setup
void setup() {
  lcd.begin(16, 2);
  mySerial.begin(9600);

  pinMode(joySW, INPUT_PULLUP);

  for (int i = 0; i < 4; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  randomSeed(analogRead(A0));

  showMenu();
}


// LOOP
void loop() {

  handleMenuButtons();

  switch (simonState) {

    case MENU:
      // idle until user chooses START
      break;

    case WAITING_START:
      // show “Get Ready!” / level, then move on
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
      handleBuzzerPattern();
      break;

    case US_GAME:
      handleUltrasonic();
      break;

    case RECALL_GAME:
      handleMemoryRecall();
      break;

    case PROCESS_RESULTS:
      handleResult();
      break;

    case GAME_OVER:
      displayGameOver();
      break;

    case PAUSED:
      // stay paused until menu-resume
      break;
  }
}


//         MENU SYSTEM
// cuz we had more pins now, I added the joystick switch cuz it's convenient.
// so now 4 options always available:
//
//   UP: CLOSE menu
//   DOWN: START
//   LEFT: END game
//   RIGHT: PAUSE / RESUME
//
// Joystick button = open menu
//
// LCD shows: CLOSE | START
//            END   | PAUSE

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

// joystick button opens/closes menu
void handleMenuButtons() {

  unsigned long now = millis();

  // joystick button = toggle menu
  if (digitalRead(joySW) == LOW && now - lastJoy > joyDelay) {
    menuOpen = !menuOpen;
    lastJoy = now;

    if (menuOpen) showMenu();
    else lcd.clear();

    return;
  }

  if (!menuOpen) return;  // when menu not visible we ignore directions

  int x = analogRead(joyX);
  int y = analogRead(joyY);

  // UP = Close
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


// MENU ACTIONS

void startGame() {
  level = 1;
  gameActive = true;

  lcd.clear();
  lcd.print("Starting...");

  // I immediately go into the start screen state
  startScreenShown = false;
  simonState = WAITING_START;
}

void endGame() {
  lcd.clear();
  lcd.print("Ending game...");
  // after this I just treat it as game over
  gameOverShown = false;
  simonState = GAME_OVER;
}

void togglePause() {
  if (simonState != PAUSED) {
    lcd.clear();
    lcd.print("Paused");
    simonState = PAUSED;
  }
  else {
    lcd.clear();
    lcd.print("Resuming...");
    // for now I just resume into the memory number phase
    startScreenShown = false;
    memSent = false;
    simonState = SEND_MEMORY_NUM;
  }
}


// START SEQUENCE (for the LCD)

void showStartScreen() {
  unsigned long now = millis();

  if (!startScreenShown) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Get Ready!");
    lcd.setCursor(0,1);
    lcd.print("Level: ");
    lcd.print(level);

    phaseStart = now;
    startScreenShown = true;
  }

  if (now - phaseStart >= 1500) {
    startScreenShown = false;
    memSent = false;
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

    // its supposed to go to LCD but will implement that after discussing serialization protocols.
    mySerial.println("S_MEM," + memoryNumber);

    phaseStart = now;
    memSent = true;
    return;
  }

  // give the memory number about 2 seconds of screen time, then move on
  if (now - phaseStart >= 2000) {
    memSent = false;
    ledPhaseInit = false;
    simonState = LED_GAME;
  }
}


// LED GAME

void generateLEDPattern(int length) {
  ledPattern = "";
  char colors[4] = {'R','G','Y','B'};

  for (int i = 0; i < length; i++) {
    ledPattern += colors[random(0,4)];
  }
}

void turnOffAllLEDs() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(ledPins[i], LOW);
  }
}

void setLEDForChar(char c) {
  turnOffAllLEDs();
  if (c == 'R') digitalWrite(ledPins[0], HIGH);
  else if (c == 'G') digitalWrite(ledPins[1], HIGH);
  else if (c == 'Y') digitalWrite(ledPins[2], HIGH);
  else if (c == 'B') digitalWrite(ledPins[3], HIGH);
}

// This is where we do the whole LED pattern mini-game you goes can fix it later but 
// i had to make it to like design the communications:
void runLEDGame() {
  unsigned long now = millis();
  
  if (!ledPhaseInit) {
    lcd.clear();
    lcd.print("LED Pattern...");

    generateLEDPattern(level + 2);  // pattern grows with level as Connor mentioned

    ledIndex = 0;
    lastLedStep = now;
    ledPhaseInit = true;
    ledShowingPattern = true;
    ledResponsePhase = false;

    player1Responded = false;
    player1Input = "";
    serialBuffer = "";

    turnOffAllLEDs();
    return;
  }

  // showing the pattern via LEDs
  if (ledShowingPattern) {
    if (ledIndex < ledPattern.length()) {
      if (now - lastLedStep >= ledStepInterval) {
        lastLedStep = now;

        char c = ledPattern[ledIndex];
        setLEDForChar(c);
        ledIndex++;
      }
    }
    else {
      // pattern is done, LEDs off, now it's players' turn
      turnOffAllLEDs();
      ledShowingPattern = false;
      ledResponsePhase = true;
      phaseStart = now;

      lcd.clear();
      lcd.print("Your turn...");
    }
    return;
  }

  // response window (12 seconds so like with time to receive, validate input and output results)
  if (ledResponsePhase) {
    // read serial non-blocking and build up a line
    while (mySerial.available() > 0) {
      char c = mySerial.read();
      if (c == '\n') {
        // got a full line, process it
        if (serialBuffer.startsWith("P_LED,")) {
          player1Input = serialBuffer.substring(6);
          player1Responded = true;
        }
        serialBuffer = "";
      } else if (c != '\r') {
        serialBuffer += c;
      }
    }

    // if 12 seconds have passed, I close the window and go process results
    if (now - phaseStart >= ledResponseWindow) {
      ledResponsePhase = false;
      ledPhaseInit = false;  // ready for next level next time we enter
      simonState = PROCESS_RESULTS;
    }

    return;
  }
}


// we will prolly use these for later implementations

void handleBuzzerPattern() {
  // eventually we’ll set up a similar millis-based pattern + response window here
  simonState = US_GAME;
}

void handleUltrasonic() {
  // here we’ll broadcast the distance target and wait for US responses
  simonState = RECALL_GAME;
}

void handleMemoryRecall() {
  // here we’ll handle the IR remote numbers at the end of all mini-games
  simonState = PROCESS_RESULTS;
}


// =========================
//        RESULT PHASE
// =========================

void handleResult() {
  // for now I only care about the LED mini-game result

  if (currentMiniGame == MG_LED) {
    lcd.clear();
    lcd.print("Checking LED...");

    bool passed = (player1Responded && player1Input == ledPattern);

    if (passed) {
      lcd.clear();
      lcd.print("Player OK");
      // we can bump level here
      level++;
      // move on to next mini-game later; right now I just go to GAME_OVER or BUZ_GAME
      // simonState = BUZ_GAME; // when we implement it
      simonState = GAME_OVER;   // for now I just end after LED for testing
    }
    else {
      lcd.clear();
      lcd.print("Player OUT");
      simonState = GAME_OVER;
    }
  }
  else {
    simonState = GAME_OVER;
  }
}


// GAME OVER

void displayGameOver() {
  unsigned long now = millis();

  if (!gameOverShown) {
    lcd.clear();
    lcd.print("GAME OVER");
    phaseStart = now;
    gameOverShown = true;
  }

  // show GAME OVER for 2 seconds, then go back to menu
  if (now - phaseStart >= 2000) {
    gameOverShown = false;
    showMenu();
  }
}
