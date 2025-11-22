#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

// LCD + Serial

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Shared software-serial bus to all 3 players
SoftwareSerial mySerial(10, 11); // RX, TX on Simon

//Joystick (menu)

const int joyX  = A1;   // LEFT / RIGHT
const int joyY  = A2;   // UP / DOWN
const int joySW = 12;   // joystick button – opens menu

const int LOW_T  = 300;
const int HIGH_T = 700;

unsigned long lastJoy       = 0;
const unsigned long joyDelay = 200;

// LEDs + Buzzer

// Simon LEDs for LED mini-game (R, G, Y, B)
const int ledPins[4] = {
  8,   // R
  9,   // G
  13,  // Y
  A3   // B (analog pin used as digital)
};

const int buzzerPin = 4;

// Game globals

int  level       = 1;
bool gameActive  = false;

String memoryNumber = "";   // digits shown at start of level
String ledPattern   = "";   // R/G/Y/B sequence
String buzPattern   = "";   // H/L sequence (high+short / low+long)
String recallAnswer = "";   // IR recall answers per player (sent back)

unsigned long phaseStart = 0;

// Player alive flags (1..3 used)
bool alive[4] = { false, true, true, true };

// Serial line buffer
String serialBuffer = "";

// Token system
int  currentPlayerId      = 1;
bool waitingForResponse   = false;
unsigned long tokenStart  = 0;
const unsigned long tokenTimeout = 3000;  // 3 s

// LED timing
bool ledPhaseInit       = false;
bool ledShowingPattern  = false;
bool ledResponsePhase   = false;
int  ledIndex           = 0;
unsigned long lastLedStep = 0;
const unsigned long ledStepInterval = 600;

// Buzzer timing

bool buzPhaseInit       = false;
bool buzShowingPattern  = false;
bool buzResponsePhase   = false;
int  buzIndex           = 0;
unsigned long lastBuzStep = 0;

// buzzer tones
const unsigned long shortTone = 200;
const unsigned long longTone  = 500;

// Ultrasonic + Recall timing

bool usPhaseInit       = false;
bool usResponsePhase   = false;
int  targetDistanceCm  = 0;         // 15–40 cm
const int usTolerance  = 5;         // ± 5 cm
const int usFixedTime  = 8;         // fixed 8 s

bool recallPhaseInit     = false;
bool recallResponsePhase = false;

// starting melody millis delay
const int melodyNotes[4]     = {523, 659, 784, 1047};
const int melodyDurations[4] = {150, 150, 150, 250};
const int melodyPause        = 60;

int  melodyIndex      = 0;
bool melodyPlaying    = false;
unsigned long melodyNextEvent = 0;

// Timer / HUD
int  responseTimeSec        = 0;
unsigned long responseWindow = 0;
unsigned long lastCountdownTick = 0;

// Mini-game ID
enum MiniGame { MG_NONE, MG_LED, MG_BUZ, MG_US, MG_MEM };
MiniGame currentMiniGame = MG_NONE;

// Simon states
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

// Player answer storage (not strictly needed)
String playerAnswers[4];

// Prototypes
int  computeResponseTime(int lvl);
void showHUD(const char *gameTag);
void showMenu();
void handleMenuButtons();
void startGame();
void endGame();
void togglePause();
void showStartScreen();
void runMemoryNumberPhase();
void runLEDGame();
void runBuzzerGame();
void runUSGame();
void runRecallGame();
void handleResult();
void parseResponse(MiniGame mg);
void advanceOrEnd();
void displayGameOver();

// response time rule: 6 + 1.5*level
int computeResponseTime(int lvl) {
  float t = 6.0 + 1.5 * lvl;
  return (int)(t + 0.5);
}

// HUD: Simon side only
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

// Menu / start melody

bool menuOpen      = true;
bool startShown    = false;
bool memSent       = false;
bool gameOverShown = false;


void startMelody() {
  melodyIndex      = 0;
  melodyPlaying    = true;
  melodyNextEvent  = millis();
}

void updateMelody() {
  if (!melodyPlaying) return;

  unsigned long now = millis();
  if (now >= melodyNextEvent) {

    if (melodyIndex >= 4) {
      noTone(buzzerPin);
      melodyPlaying = false;
      return;
    }

    int freq = melodyNotes[melodyIndex];
    int dur  = melodyDurations[melodyIndex];

    tone(buzzerPin, freq, dur);

    melodyNextEvent = now + dur + melodyPause;
    melodyIndex++;
  }
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

void startGame() {
  level      = 1;
  gameActive = true;

  alive[1] = true;
  alive[2] = true;
  alive[3] = true;

  ledPhaseInit       = false;
  ledShowingPattern  = false;
  ledResponsePhase   = false;

  buzPhaseInit       = false;
  buzShowingPattern  = false;
  buzResponsePhase   = false;

  usPhaseInit        = false;
  usResponsePhase    = false;

  recallPhaseInit     = false;
  recallResponsePhase = false;

  waitingForResponse = false;
  currentPlayerId    = 1;
  serialBuffer       = "";

  startShown    = false;
  memSent       = false;
  gameOverShown = false;
  currentMiniGame = MG_NONE;

  mySerial.println("S_LEVEL," + String(level));
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

void togglePause() {
  if (simonState != PAUSED) {
    lcd.clear();
    lcd.print("Paused");
    simonState = PAUSED;
  } else {
    lcd.clear();
    lcd.print("Resuming...");
    memSent        = false;
    ledPhaseInit   = false;
    buzPhaseInit   = false;
    usPhaseInit    = false;
    recallPhaseInit = false;
    simonState     = SEND_MEMORY_NUM;
  }
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

  if (!menuOpen) return;

  int x = analogRead(joyX);
  int y = analogRead(joyY);

  if (y < LOW_T && now - lastJoy > joyDelay) {
    menuOpen = false;
    lcd.clear();
    lastJoy  = now;
    return;
  }

  if (y > HIGH_T && now - lastJoy > joyDelay) {
    startGame();
    menuOpen = false;
    lastJoy  = now;
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

// Memory: digits = 3 + floor((level-1)/3)
void generateMemoryNumber() {
  int digits = 3 + ((level - 1) / 3);
  memoryNumber = "";
  for (int i = 0; i < digits; i++) {
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

// LED GAME --------------------------------------------------------

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
  for (int i=0;i<len;i++) ledPattern += colors[random(0,4)];
}

void runLEDGame() {
  unsigned long now = millis();

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

    currentMiniGame = MG_LED;
    mySerial.println("S_LED_START");
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

      responseTimeSec   = computeResponseTime(level);
      responseWindow    = (unsigned long)responseTimeSec * 1000UL;
      phaseStart        = now;
      lastCountdownTick = now;

      showHUD("LED");
      mySerial.println("S_TIME," + String(responseTimeSec));
    }
    return;
  }

  if (ledResponsePhase) {
    if (responseTimeSec > 0 && now - lastCountdownTick >= 1000) {
      lastCountdownTick += 1000;
      responseTimeSec--;
      if (responseTimeSec < 0) responseTimeSec = 0;
      showHUD("LED");
    }
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

// BUZZER GAME ------------------------------------------------------

void generateBuzzerPattern(int len) {
  buzPattern="";
  char s[2]={'H','L'};
  for (int i=0;i<len;i++) buzPattern += s[random(0,2)];
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

    buzIndex          = 0;
    lastBuzStep       = 0;
    buzPhaseInit      = true;
    buzShowingPattern = true;
    buzResponsePhase  = false;

    playerAnswers[1]="";
    playerAnswers[2]="";
    playerAnswers[3]="";

    currentMiniGame = MG_BUZ;
    mySerial.println("S_BUZ_START");
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

      responseTimeSec   = computeResponseTime(level);
      responseWindow    = (unsigned long)responseTimeSec *1000UL;
      phaseStart        = now;
      lastCountdownTick = now;

      showHUD("BUZ");
      mySerial.println("S_TIME," + String(responseTimeSec));
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
      currentPlayerId=1;
      waitingForResponse=false;
      serialBuffer="";
      simonState=PROCESS_RESULTS;
    }
    return;
  }
}

// ULTRASONIC GAME ---------------------------------------------------

void runUSGame() {
  unsigned long now = millis();

  if (!usPhaseInit) {
    lcd.clear();
    lcd.print("US Distance...");

    currentMiniGame = MG_US;
    targetDistanceCm = random(15,41);

    responseTimeSec   = usFixedTime;
    responseWindow    = (unsigned long)usFixedTime*1000UL;
    phaseStart        = now;
    lastCountdownTick = now;

    showHUD("US");

    mySerial.println("S_US_START," + String(targetDistanceCm));
    mySerial.println("S_TIME," + String(usFixedTime));

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
      currentPlayerId=1;
      waitingForResponse=false;
      serialBuffer="";
      simonState=PROCESS_RESULTS;
    }
    return;
  }
}

// RECALL GAME (A again) ----------------------------------------------

void runRecallGame() {
  unsigned long now=millis();

  if (!recallPhaseInit) {
    lcd.clear();
    lcd.print("Recall Mem...");

    currentMiniGame = MG_MEM;

    responseTimeSec   = usFixedTime;  // fixed 8 seconds too
    responseWindow    = (unsigned long)usFixedTime *1000UL;
    phaseStart        = now;
    lastCountdownTick = now;

    showHUD("MEM");

    mySerial.println("S_RECALL_START");
    mySerial.println("S_TIME," + String(usFixedTime));

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
      currentPlayerId=1;
      waitingForResponse=false;
      serialBuffer="";
      simonState=PROCESS_RESULTS;
    }
    return;
  }
}

// TOKEN SYSTEM --------------------------------------------------------

void handleResult() {
  unsigned long now=millis();
  bool finishedAll=false;

  // template macro
  auto requestPlayer = [&](String tag){
    if (!waitingForResponse) {
      while (currentPlayerId<=3 && !alive[currentPlayerId]) currentPlayerId++;
      if (currentPlayerId>3) finishedAll=true;
      else {
        mySerial.println("S_REQ," + String(currentPlayerId) + "," + tag);
        tokenStart=now;
        waitingForResponse=true;
        serialBuffer="";
      }
      if (finishedAll) advanceOrEnd();
      return true;
    }
    return false;
  };

  // read serial lines
  auto readSerial = [&](){
    while (mySerial.available()) {
      char c = mySerial.read();
      if (c=='\n') {
        parseResponse(currentMiniGame);
        return;
      } else if (c!='\r') serialBuffer+=c;
    }
  };

  // timeout
  auto timeoutCheck = [&](String tag){
    if (waitingForResponse && now-tokenStart>=tokenTimeout) {
      alive[currentPlayerId]=false;
      mySerial.println("S_RES," + String(currentPlayerId) + "," + tag + ",FAIL");
      waitingForResponse=false;
      currentPlayerId++;
    }
  };

  if (currentMiniGame==MG_LED) {
    if (requestPlayer("LED")) return;
    readSerial();
    timeoutCheck("LED");
    return;
  }

  if (currentMiniGame==MG_BUZ) {
    if (requestPlayer("BUZ")) return;
    readSerial();
    timeoutCheck("BUZ");
    return;
  }

  if (currentMiniGame==MG_US) {
    if (requestPlayer("US")) return;
    readSerial();
    timeoutCheck("US");
    return;
  }

  if (currentMiniGame==MG_MEM) {
    if (requestPlayer("MEM")) return;
    readSerial();
    timeoutCheck("MEM");
    return;
  }

  simonState=GAME_OVER;
}

void parseResponse(MiniGame mg) {
  int c1=serialBuffer.indexOf(',');
  if (c1<0) {serialBuffer="";waitingForResponse=false;return;}
  int c2=serialBuffer.indexOf(',',c1+1);
  if (c2<0) {serialBuffer="";waitingForResponse=false;return;}
  int c3=serialBuffer.indexOf(',',c2+1);
  if (c3<0) {serialBuffer="";waitingForResponse=false;return;}

  String prefix=serialBuffer.substring(0,c1);
  int    id    =serialBuffer.substring(c1+1,c2).toInt();
  String type  =serialBuffer.substring(c2+1,c3);
  String ans   =serialBuffer.substring(c3+1);

  if (prefix!="P_RESP" || id!=currentPlayerId) {
    serialBuffer="";
    return;
  }

  if (mg==MG_LED && type=="LED") {
    if (ans==ledPattern) mySerial.println("S_RES,"+String(id)+",LED,OK");
    else {
      mySerial.println("S_RES,"+String(id)+",LED,FAIL");
      alive[id]=false;
    }
  }

  if (mg==MG_BUZ && type=="BUZ") {
    if (ans==buzPattern) mySerial.println("S_RES,"+String(id)+",BUZ,OK");
    else {
      mySerial.println("S_RES,"+String(id)+",BUZ,FAIL");
      alive[id]=false;
    }
  }

  if (mg==MG_US && type=="US") {
    int measured = ans.toInt();
    if (abs(measured-targetDistanceCm)<=usTolerance)
      mySerial.println("S_RES,"+String(id)+",US,OK");
    else {
      mySerial.println("S_RES,"+String(id)+",US,FAIL");
      alive[id]=false;
    }
  }

  if (mg==MG_MEM && type=="MEM") {
    if (ans==memoryNumber) mySerial.println("S_RES,"+String(id)+",MEM,OK");
    else {
      mySerial.println("S_RES,"+String(id)+",MEM,FAIL");
      alive[id]=false;
    }
  }

  waitingForResponse=false;
  currentPlayerId++;
  serialBuffer="";
}

void advanceOrEnd() {
  bool anyoneAlive=false;
  for (int i=1;i<=3;i++) if (alive[i]) anyoneAlive=true;

  if (!anyoneAlive) {
    simonState=GAME_OVER;
    return;
  }

  if (currentMiniGame==MG_LED) {
    buzPhaseInit=false;
    buzShowingPattern=false;
    buzResponsePhase=false;
    waitingForResponse=false;
    currentPlayerId=1;
    serialBuffer="";
    simonState=BUZ_GAME;
    return;
  }

  if (currentMiniGame==MG_BUZ) {
    usPhaseInit=false;
    usResponsePhase=false;
    waitingForResponse=false;
    currentPlayerId=1;
    serialBuffer="";
    simonState=US_GAME;
    return;
  }

  if (currentMiniGame==MG_US) {
    recallPhaseInit=false;
    recallResponsePhase=false;
    waitingForResponse=false;
    currentPlayerId=1;
    serialBuffer="";
    simonState=RECALL_GAME;
    return;
  }

  if (currentMiniGame==MG_MEM) {
    level++;

    if (level > 18) {
        lcd.clear();
        lcd.print("Max Level!");
        simonState = GAME_OVER;
        return;
    }
    
    mySerial.println("S_LEVEL," + String(level));

    startShown=false;
    memSent=false;

    ledPhaseInit=false;
    ledShowingPattern=false;
    ledResponsePhase=false;

    buzPhaseInit=false;
    buzShowingPattern=false;
    buzResponsePhase=false;

    usPhaseInit=false;
    usResponsePhase=false;

    recallPhaseInit=false;
    recallResponsePhase=false;

    waitingForResponse=false;
    currentPlayerId=1;
    serialBuffer="";

    simonState=SEND_MEMORY_NUM;
  }
}

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

void setup() {
  lcd.init();
  lcd.backlight();

  mySerial.begin(9600);

  pinMode(joySW, INPUT_PULLUP);
  pinMode(buzzerPin,OUTPUT);

  for (int i=0;i<4;i++) {
    pinMode(ledPins[i],OUTPUT);
    digitalWrite(ledPins[i],LOW);
  }

  randomSeed(analogRead(A0));
  showMenu();
}

void loop() {
  handleMenuButtons();
  updateMelody();

  switch(simonState) {
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
      runUSGame();
      break;

    case RECALL_GAME:
      runRecallGame();
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
