void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}

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

