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

void loop() {

  // we can add or remove these later
  handleMenuButtons();

  switch (simonState) {

    case MENU:
      // so we are just waiting for it to start
      break;

    case WAITING_START:
      // maybe show sth like get ready
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
      // Wait for resume
      break;
  }
}

void handleStart(String message) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Get Ready!");
  lcd.setCursor(0,1);
  lcd.print("Level: " + String(level));
  gameActive = true;
}

handleMemory()
handleLEDPattern()
handleBuzzerPattern()
handleUltrasonic()
handleMemoryRecall()
handleElimination()
handleResult()

int level = 1;
bool gameActive = false;

String memoryNumber = "";
String ledPattern = "";

unsigned long lastAction = 0;




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

  broadcast("S_MEM," + memoryNumber);

  delay(2000);
}

void generateLEDPattern(int length) {
  ledPattern = "";

  char colors[4] = {'R', 'G', 'Y', 'B'};

  for (int i = 0; i < length; i++) {
    int r = random(0, 4);
    ledPattern += colors[r];
  }
}

void sendLEDPattern() {
  broadcast("S_LED," + ledPattern);
}

String ledPattern = "";
void generateLEDPattern(int length) {
  ledPattern = "";

  char colors[4] = {'R', 'G', 'Y', 'B'};

  for (int i = 0; i < length; i++) {
    int r = random(0, 4);
    ledPattern += colors[r];
  }
}

void sendLEDPattern() {
  mySerial.println("S_LED," + ledPattern);
}


void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}
