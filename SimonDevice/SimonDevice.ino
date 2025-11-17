#include <LiquidCrystal.h>
#include <SoftwareSerial.h>

// LCD + Serial
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);
SoftwareSerial mySerial(10, 11);   // For now Simon → 1 device

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


// Game globals
int level = 1;
bool gameActive = false;

String memoryNumber = "";
String ledPattern = "";

unsigned long lastAction = 0;


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


// Setup
void setup() {
  lcd.begin(16, 2);
  mySerial.begin(9600);

  pinMode(joySW, INPUT_PULLUP);

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
      // show “Get Ready!” / level for now, we'll discuss the rest later
      showStartScreen();
      break;

    case SEND_MEMORY_NUM:
      sendMemoryNumber();
      simonState = LED_GAME;
      break;

    case LED_GAME:
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


//                  MENU SYSTEM
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
  delay(1000);

  simonState = WAITING_START;
}

void endGame() {
  lcd.clear();
  lcd.print("Ending game...");
  delay(1000);

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
    delay(600);
    simonState = SEND_MEMORY_NUM;
  }
}


// START SEQUENCE (for the LCD)

void showStartScreen() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Get Ready!");
  lcd.setCursor(0,1);
  lcd.print("Level: ");
  lcd.print(level);

  delay(1500);

  simonState = SEND_MEMORY_NUM;
}


// MEMORY NUMBER GAME

void generateMemoryNumber() {
  memoryNumber = "";
  for (int i = 0; i < 3; i++) {
    memoryNumber += String(random(0, 10));
  }
}

void sendMemoryNumber() {
  generateMemoryNumber();

  lcd.clear();
  lcd.print("Mem: ");
  lcd.print(memoryNumber);

  mySerial.println("S_MEM," + memoryNumber);

  delay(2000);
}

// LED GAME

void generateLEDPattern(int length) {
  ledPattern = "";
  char colors[4] = {'R','G','Y','B'};

  for (int i = 0; i < length; i++) {
    ledPattern += colors[random(0,4)];
  }
}

void sendLEDPattern() {
  mySerial.println("S_LED," + ledPattern);
}

void runLEDGame() {
  lcd.clear();
  lcd.print("LED Pattern...");

  generateLEDPattern(level + 2);
  sendLEDPattern();

  delay(2000);

  simonState = BUZ_GAME;   // later this moves to next minigame, but we do have to decide when to 
                           // collect the info and when the next game plays. I'm think a simple delay or millis
}

//   PLACEHOLDER FUNCTIONS, we will prolly use these for later implementation.

void handleBuzzerPattern() {
  simonState = US_GAME;
}

void handleUltrasonic() {
  simonState = RECALL_GAME;
}

void handleMemoryRecall() {
  simonState = PROCESS_RESULTS;
}

void handleResult() {
  simonState = GAME_OVER;
}

void displayGameOver() {
  lcd.clear();
  lcd.print("GAME OVER");
  delay(2000);
  showMenu();
}
