#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>


LiquidCrystal_I2C lcd(0x27, 16, 2);

// Dedicated serial channels for each player
// Player 1: RX=2, TX=3
// Player 2: RX=4, TX=5
// Player 3: RX=6, TX=7

SoftwareSerial ss1(2, 3);   // RX, TX for Player 1
SoftwareSerial ss2(4, 5);   // RX, TX for Player 2
SoftwareSerial ss3(6, 7);   // RX, TX for Player 3

// Helper array to index correct channel
SoftwareSerial* playerSS[4] = { nullptr, &ss1, &ss2, &ss3 };

// Joystick (for the menu)

const int joyX  = A1;
const int joyY  = A2;
const int joySW = 12;

const int LOW_T  = 300;
const int HIGH_T = 700;

unsigned long lastJoy       = 0;
const unsigned long joyDelay = 200;

// LEDs + Buzzer

// Simon LEDs for LED mini-game (R,G,Y,B)
const int ledPins[4] = {
  8,   // R
  9,   // G
  13,  // Y
  A3   // B
};

const int buzzerPin = A0;

// Game Globals

int  level       = 1;
bool gameActive  = false;

String memoryNumber = "";
String ledPattern   = "";
String buzPattern   = "";
String recallAnswer = "";

unsigned long phaseStart = 0;

bool alive[4] = { false, true, true, true };

// Token response system
int currentPlayerId    = 1;
bool waitingForResponse = false;
unsigned long tokenStart = 0;
const unsigned long tokenTimeout = 3000;

String serialBuffer = "";

// LED Timing

bool ledPhaseInit       = false;
bool ledShowingPattern  = false;
bool ledResponsePhase   = false;

int ledIndex = 0;
unsigned long lastLedStep = 0;
const unsigned long ledStepInterval = 600;

// Buzzer Timing

bool buzPhaseInit       = false;
bool buzShowingPattern  = false;
bool buzResponsePhase   = false;

int buzIndex = 0;
unsigned long lastBuzStep = 0;

const unsigned long shortTone = 200;
const unsigned long longTone  = 500;

// Ultrasonic + Recall Timing

bool usPhaseInit       = false;
bool usResponsePhase   = false;

int targetDistanceCm   = 0;
const int usTolerance  = 5;
const int usFixedTime  = 8;

bool recallPhaseInit     = false;
bool recallResponsePhase = false;

// Start Melody (non-blocking)

const int melodyNotes[4]     = {523, 659, 784, 1047};
const int melodyDurations[4] = {150, 150, 150, 250};
const int melodyPause        = 60;

int melodyIndex = 0;
bool melodyPlaying = false;
unsigned long melodyNextEvent = 0;

// HUD Timing

int  responseTimeSec        = 0;
unsigned long responseWindow = 0;
unsigned long lastCountdownTick = 0;

// Mini-Game enumeration

enum MiniGame { MG_NONE, MG_LED, MG_BUZ, MG_US, MG_MEM };
MiniGame currentMiniGame = MG_NONE;

// State Machine

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

// Utilities


int computeResponseTime(int lvl) {
  float t = 6.0 + 1.5 * lvl;
  return (int)(t + 0.5);
}

void showHUD(const char *gameTag) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("G:");
  lcd.print(gameTag);
  lcd.print(" L:");
  if (level < 10) lcd.print("0");
  lcd.print(level);

  lcd.setCursor(0, 1);
  lcd.print("T:");
  if (responseTimeSec < 10) lcd.print("0");
  lcd.print(responseTimeSec);
  lcd.print("s");
}

// Menu + Start Melody

bool menuOpen      = true;
bool startShown    = false;
bool memSent       = false;
bool gameOverShown = false;

void startMelody() {
  melodyIndex = 0;
  melodyPlaying = true;
  melodyNextEvent = millis();
}

void updateMelody() {
  if (!melodyPlaying) return;

  unsigned long now = millis();
  if (now < melodyNextEvent) return;

  if (melodyIndex >= 4) {
    noTone(buzzerPin);
    melodyPlaying = false;
    return;
  }

  tone(buzzerPin, melodyNotes[melodyIndex], melodyDurations[melodyIndex]);
  melodyNextEvent = now + melodyDurations[melodyIndex] + melodyPause;

  melodyIndex++;
}

void showMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Close  Start");
  lcd.setCursor(0, 1);
  lcd.print("End    Pause");
  simonState = MENU;
  menuOpen   = true;
}

// Start / End

void startGame() {
  level      = 1;
  gameActive = true;

  alive[1] = true;
  alive[2] = true;
  alive[3] = true;

  // Reset all phases
  ledPhaseInit = buzPhaseInit = usPhaseInit = recallPhaseInit = false;
  ledShowingPattern = buzShowingPattern = false;
  ledResponsePhase  = buzResponsePhase  = false;
  usResponsePhase   = recallResponsePhase = false;

  waitingForResponse = false;
  currentPlayerId    = 1;
  serialBuffer       = "";

  startShown = memSent = gameOverShown = false;
  currentMiniGame = MG_NONE;

  // Broadcast level
  for (int i=1;i<=3;i++)
    playerSS[i]->println("S_LEVEL," + String(level));

  startMelody();

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

// Pause

void togglePause() {
  if (simonState != PAUSED) {
    lcd.clear();
    lcd.print("Paused");
    simonState = PAUSED;
  } else {
    lcd.clear();
    lcd.print("Resuming...");
    memSent = false;
    ledPhaseInit = buzPhaseInit = usPhaseInit = recallPhaseInit = false;
    simonState = SEND_MEMORY_NUM;
  }
}

// Handle Menu Buttons

void handleMenuButtons() {
  unsigned long now = millis();

  if (digitalRead(joySW) == LOW && now - lastJoy > joyDelay) {
    lastJoy = now;
    if (!menuOpen) { menuOpen = true; showMenu(); }
    return;
  }

  if (!menuOpen) return;

  int x = analogRead(joyX);
  int y = analogRead(joyY);

  if (y < LOW_T && now - lastJoy > joyDelay) {
    menuOpen = false;
    lcd.clear();
    lastJoy = now;
    return;
  }

  if (y > HIGH_T && now - lastJoy > joyDelay) {
    startGame();
    menuOpen = false;
    lastJoy = now;
    return;
  }

  if (x < LOW_T && now - lastJoy > joyDelay) {
    endGame();
    lastJoy = now;
    return;
  }

  if (x > HIGH_T && now - lastJoy > joyDelay) {
    togglePause();
    lastJoy = now;
    return;
  }
}

// Start Screen

void showStartScreen() {
  unsigned long now = millis();

  if (!startShown) {
    lcd.clear();
    lcd.print("Get Ready!");
    lcd.setCursor(0, 1);
    lcd.print("Level ");
    lcd.print(level);

    startShown = true;
    phaseStart = now;
    return;
  }

  if (now - phaseStart >= 1500) {
    startShown = false;
    simonState = SEND_MEMORY_NUM;
  }
}

// Memory Digits

void generateMemoryNumber() {
  int digits = 3 + ((level - 1) / 3);
  memoryNumber = "";
  for (int i=0;i<digits;i++)
    memoryNumber += String(random(0,10));
}

void runMemoryNumberPhase() {
  unsigned long now = millis();

  if (!memSent) {
    generateMemoryNumber();

    lcd.clear();
    lcd.print("Mem: ");
    lcd.print(memoryNumber);

    for (int i=1;i<=3;i++)
      playerSS[i]->println("S_MEM," + memoryNumber);

    memSent = true;
    phaseStart = now;
    return;
  }

  if (now - phaseStart >= 2000) {
    memSent = false;
    simonState = LED_GAME;
  }
}

// LED GAME

void turnOffAllLEDs() { for (int i=0;i<4;i++) digitalWrite(ledPins[i],LOW); }

void setLED(char c) {
  turnOffAllLEDs();
  if (c=='R') digitalWrite(ledPins[0],HIGH);
  if (c=='G') digitalWrite(ledPins[1],HIGH);
  if (c=='Y') digitalWrite(ledPins[2],HIGH);
  if (c=='B') digitalWrite(ledPins[3],HIGH);
}

void generateLEDPattern(int len) {
  ledPattern = "";
  char c[4]={'R','G','Y','B'};
  for (int i=0;i<len;i++) ledPattern += c[random(0,4)];
}

void runLEDGame() {
  unsigned long now = millis();

  if (!ledPhaseInit) {
    lcd.clear();
    lcd.print("LED Pattern...");

    generateLEDPattern(level + 2);

    ledIndex = 0;
    lastLedStep = now;

    ledPhaseInit = true;
    ledShowingPattern = true;
    ledResponsePhase = false;

    currentMiniGame = MG_LED;

    for (int i=1;i<=3;i++)
      playerSS[i]->println("S_LED_START");

    return;
  }

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

      responseTimeSec = computeResponseTime(level);
      responseWindow = (unsigned long)responseTimeSec * 1000UL;

      phaseStart = now;
      lastCountdownTick = now;

      showHUD("LED");

      for (int i=1;i<=3;i++)
        playerSS[i]->println("S_TIME," + String(responseTimeSec));
    }
    return;
  }

  if (ledResponsePhase) {
    if (responseTimeSec>0 && now-lastCountdownTick>=1000) {
      lastCountdownTick += 1000;
      responseTimeSec--;
      if (responseTimeSec<0) responseTimeSec=0;
      showHUD("LED");
    }

    if (now - phaseStart >= responseWindow || responseTimeSec<=0) {
      ledResponsePhase=false;
      waitingForResponse=false;
      currentPlayerId=1;
      serialBuffer="";
      simonState=PROCESS_RESULTS;
    }
  }
}

// BUZZER GAME

void generateBuzzerPattern(int len) {
  buzPattern="";
  char s[2]={'H','L'};
  for (int i=0;i<len;i++) buzPattern+=s[random(0,2)];
}

void playBuzzerSymbol(char s) {
  if (s=='H') tone(buzzerPin,900,shortTone);
  else        tone(buzzerPin,400,longTone);
}

void runBuzzerGame() {
  unsigned long now = millis();

  if (!buzPhaseInit) {
    lcd.clear();
    lcd.print("Buzzer...");

    generateBuzzerPattern(level + 2);

    buzIndex=0;
    lastBuzStep=0;

    buzPhaseInit=true;
    buzShowingPattern=true;
    buzResponsePhase=false;

    currentMiniGame=MG_BUZ;

    for (int i=1;i<=3;i++)
      playerSS[i]->println("S_BUZ_START");

    return;
  }

  if (buzShowingPattern) {
    if (buzIndex < buzPattern.length()) {
      if (now >= lastBuzStep) {
        char s=buzPattern[buzIndex++];
        playBuzzerSymbol(s);
        unsigned long dur=(s=='H'?shortTone:longTone);
        lastBuzStep = now + dur + 100;
      }
    } else {
      buzShowingPattern=false;
      buzResponsePhase=true;

      responseTimeSec = computeResponseTime(level);
      responseWindow = (unsigned long)responseTimeSec *1000UL;

      phaseStart = now;
      lastCountdownTick = now;

      showHUD("BUZ");

      for (int i=1;i<=3;i++)
        playerSS[i]->println("S_TIME," + String(responseTimeSec));
    }
    return;
  }

  if (buzResponsePhase) {
    if (responseTimeSec>0 && now-lastCountdownTick>=1000) {
      lastCountdownTick+=1000;
      responseTimeSec--;
      if (responseTimeSec<0) responseTimeSec=0;
      showHUD("BUZ");
    }

    if (now-phaseStart>=responseWindow || responseTimeSec<=0) {
      buzResponsePhase=false;
      waitingForResponse=false;
      currentPlayerId=1;
      serialBuffer="";
      simonState=PROCESS_RESULTS;
    }
  }
}

// ULTRASONIC GAME

void runUSGame() {
  unsigned long now = millis();

  if (!usPhaseInit) {
    lcd.clear();
    lcd.print("US Distance...");

    currentMiniGame = MG_US;

    targetDistanceCm = random(15,41);

    responseTimeSec = usFixedTime;
    responseWindow = (unsigned long)usFixedTime * 1000UL;

    phaseStart = now;
    lastCountdownTick = now;

    showHUD("US");

    for (int i=1;i<=3;i++) {
      playerSS[i]->print("S_US_START,");
      playerSS[i]->println(targetDistanceCm);
      playerSS[i]->println("S_TIME," + String(usFixedTime));
    }

    usPhaseInit=true;
    usResponsePhase=true;
    return;
  }

  if (usResponsePhase) {
    if (responseTimeSec>0 && now-lastCountdownTick>=1000) {
      lastCountdownTick+=1000;
      responseTimeSec--;
      if (responseTimeSec<0) responseTimeSec=0;
      showHUD("US");
    }

    if (now-phaseStart>=responseWindow || responseTimeSec<=0) {
      usResponsePhase=false;
      waitingForResponse=false;
      currentPlayerId=1;
      serialBuffer="";
      simonState=PROCESS_RESULTS;
    }
  }
}

// RECALL GAME

void runRecallGame() {
  unsigned long now = millis();

  if (!recallPhaseInit) {
    lcd.clear();
    lcd.print("Recall Mem...");

    currentMiniGame = MG_MEM;

    responseTimeSec = usFixedTime;
    responseWindow = (unsigned long)usFixedTime * 1000UL;

    phaseStart = now;
    lastCountdownTick = now;

    showHUD("MEM");

    for (int i=1;i<=3;i++) {
      playerSS[i]->println("S_RECALL_START");
      playerSS[i]->println("S_TIME," + String(usFixedTime));
    }

    recallPhaseInit=true;
    recallResponsePhase=true;
    return;
  }

  if (recallResponsePhase) {
    if (responseTimeSec>0 && now-lastCountdownTick>=1000) {
      lastCountdownTick+=1000;
      responseTimeSec--;
      if (responseTimeSec<0) responseTimeSec=0;
      showHUD("MEM");
    }

    if (now-phaseStart>=responseWindow || responseTimeSec<=0) {
      recallResponsePhase=false;
      waitingForResponse=false;
      currentPlayerId=1;
      serialBuffer="";
      simonState=PROCESS_RESULTS;
    }
  }
}

// TOKEN SYSTEM

String readOneLine(SoftwareSerial &ss) {
  while (ss.available()) {
    char c = ss.read();
    if (c=='\n') {
      String out = serialBuffer;
      serialBuffer = "";
      return out;
    }
    if (c!='\r') serialBuffer+=c;
  }
  return "";
}

void parseResponse(MiniGame mg, int pid, const String &line) {
  int c1=line.indexOf(',');
  int c2=line.indexOf(',',c1+1);
  int c3=line.indexOf(',',c2+1);
  if (c1<0||c2<0||c3<0) return;

  String prefix=line.substring(0,c1);
  int id=line.substring(c1+1,c2).toInt();
  String type=line.substring(c2+1,c3);
  String ans=line.substring(c3+1);

  if (prefix!="P_RESP" || id!=pid) return;

  if (mg==MG_LED && type=="LED") {
    if (ans==ledPattern) playerSS[pid]->println("S_RES,"+String(pid)+",LED,OK");
    else { alive[pid]=false; playerSS[pid]->println("S_RES,"+String(pid)+",LED,FAIL"); }
  }

  else if (mg==MG_BUZ && type=="BUZ") {
    if (ans==buzPattern) playerSS[pid]->println("S_RES,"+String(pid)+",BUZ,OK");
    else { alive[pid]=false; playerSS[pid]->println("S_RES,"+String(pid)+",BUZ,FAIL"); }
  }

  else if (mg==MG_US && type=="US") {
    int measured=ans.toInt();
    if (abs(measured-targetDistanceCm)<=usTolerance)
      playerSS[pid]->println("S_RES,"+String(pid)+",US,OK");
    else { alive[pid]=false; playerSS[pid]->println("S_RES,"+String(pid)+",US,FAIL"); }
  }

  else if (mg==MG_MEM && type=="MEM") {
    if (ans==memoryNumber) playerSS[pid]->println("S_RES,"+String(pid)+",MEM,OK");
    else { alive[pid]=false; playerSS[pid]->println("S_RES,"+String(pid)+",MEM,FAIL"); }
  }

  waitingForResponse=false;
  currentPlayerId++;
}

void handleResult() {
  unsigned long now=millis();

  if (!waitingForResponse) {
    while (currentPlayerId<=3 && !alive[currentPlayerId]) currentPlayerId++;
    if (currentPlayerId>3) { advanceOrEnd(); return; }

    playerSS[currentPlayerId]->print("S_REQ,");
    playerSS[currentPlayerId]->print(currentPlayerId);
    playerSS[currentPlayerId]->print(",");

    if (currentMiniGame==MG_LED) playerSS[currentPlayerId]->println("LED");
    if (currentMiniGame==MG_BUZ) playerSS[currentPlayerId]->println("BUZ");
    if (currentMiniGame==MG_US)  playerSS[currentPlayerId]->println("US");
    if (currentMiniGame==MG_MEM) playerSS[currentPlayerId]->println("MEM");

    waitingForResponse=true;
    tokenStart=now;
    serialBuffer="";
    return;
  }

  // Read from correct channel
  String line = readOneLine(*playerSS[currentPlayerId]);
  if (line!="") {
    parseResponse(currentMiniGame,currentPlayerId,line);
    return;
  }

  // Timeout
  if (now-tokenStart>=tokenTimeout) {
    alive[currentPlayerId]=false;
    playerSS[currentPlayerId]->println("S_RES,"+String(currentPlayerId)+",FAIL");
    waitingForResponse=false;
    currentPlayerId++;
  }
}

// Advance or End

void advanceOrEnd() {
  bool anyone=false;
  for (int i=1;i<=3;i++) if (alive[i]) anyone=true;

  if (!anyone) { simonState=GAME_OVER; return; }

  if (currentMiniGame==MG_LED) {
    buzPhaseInit=false;
    simonState=BUZ_GAME;
    return;
  }

  if (currentMiniGame==MG_BUZ) {
    usPhaseInit=false;
    simonState=US_GAME;
    return;
  }

  if (currentMiniGame==MG_US) {
    recallPhaseInit=false;
    simonState=RECALL_GAME;
    return;
  }

  if (currentMiniGame==MG_MEM) {
    level++;

    if (level > 18) {
      lcd.clear();
      lcd.print("Max Level!");
      simonState=GAME_OVER;
      return;
    }

    for (int i=1;i<=3;i++)
      playerSS[i]->println("S_LEVEL,"+String(level));

    // reset phases
    ledPhaseInit=buzPhaseInit=usPhaseInit=recallPhaseInit=false;

    simonState=SEND_MEMORY_NUM;
  }
}

// Game Over

void displayGameOver() {
  unsigned long now=millis();
  if (!gameOverShown) {
    lcd.clear();
    lcd.print("GAME OVER");
    gameOverShown=true;
    phaseStart=now;
  }
  if (now-phaseStart>=2000) {
    gameOverShown=false;
    showMenu();
  }
}

// Setup

void setup() {
  lcd.init();
  lcd.backlight();

  ss1.begin(9600);
  ss2.begin(9600);
  ss3.begin(9600);

  pinMode(joySW, INPUT_PULLUP);
  pinMode(buzzerPin,OUTPUT);

  for (int i=0;i<4;i++) {
    pinMode(ledPins[i],OUTPUT);
    digitalWrite(ledPins[i],LOW);
  }

  randomSeed(analogRead(A0));

  showMenu();
}

// Loop

void loop() {
  handleMenuButtons();
  updateMelody();

  switch (simonState) {
    case MENU: break;
    case WAITING_START: showStartScreen(); break;
    case SEND_MEMORY_NUM: runMemoryNumberPhase(); break;
    case LED_GAME: runLEDGame(); break;
    case BUZ_GAME: runBuzzerGame(); break;
    case US_GAME: runUSGame(); break;
    case RECALL_GAME: runRecallGame(); break;
    case PROCESS_RESULTS: handleResult(); break;
    case GAME_OVER: displayGameOver(); break;
    case PAUSED: break;
  }
}
