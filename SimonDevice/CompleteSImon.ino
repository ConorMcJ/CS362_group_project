#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

// =========================================================
//  I2C LCD
// =========================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =========================================================
//  SERIAL LINKS TO EACH PLAYER
// =========================================================
// P1: RX=2, TX=3
// P2: RX=4, TX=5
// P3: RX=6, TX=7

SoftwareSerial ss1(2, 3);
SoftwareSerial ss2(4, 5);
SoftwareSerial ss3(6, 7);

// Helper arrays for indexing players easily
SoftwareSerial* channels[4] = { NULL, &ss1, &ss2, &ss3 };

// incoming line buffers
String serialBuf[4] = { "", "", "", "" };

// =========================================================
//  MENU INPUT (JOYSTICK)
// =========================================================
const int joyX  = A1;
const int joyY  = A2;
const int joySW = 12;

const int LOW_T  = 300;
const int HIGH_T = 700;
unsigned long lastJoy = 0;
const unsigned long joyDelay = 200;

// =========================================================
//  SIMON LEDS + BUZZER
// =========================================================
const int ledPins[4] = {8, 9, 13, A3};
const int buzzerPin = 4;

// =========================================================
//  GAME STATE STORAGE
// =========================================================
int level = 1;
bool alive[4] = {false, true, true, true};
bool gameActive = false;

String memoryNumber = "";
String ledPattern   = "";
String buzPattern   = "";
int    targetDistanceCm = 0;

// Tolerance + timing
const int usTolerance = 5;
const int usFixedTime = 8;

unsigned long phaseStart = 0;

// =========================================================
//  TIMING + HUD
// =========================================================
int responseTimeSec = 0;
unsigned long responseWindow = 0;
unsigned long lastCountdownTick = 0;

// =========================================================
//  TOKEN SYSTEM — NOW USING SEPARATE SERIALS
// =========================================================
bool waitingForResponse = false;
int  currentPlayerId    = 1;
unsigned long tokenStart = 0;
const unsigned long tokenTimeout = 3000;

// =========================================================
//  GAME MODE ENUMS
// =========================================================
enum MiniGame { MG_NONE, MG_LED, MG_BUZ, MG_US, MG_MEM };
MiniGame currentMiniGame = MG_NONE;

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

// =========================================================
//  START MELODY
// =========================================================
const int melodyNotes[4]     = {523, 659, 784, 1047};
const int melodyDurations[4] = {150, 150, 150, 250};
const int melodyPause        = 60;

int melodyIndex = 0;
bool melodyPlaying = false;
unsigned long melodyNextEvent = 0;

void startMelody() {
  melodyIndex = 0;
  melodyPlaying = true;
  melodyNextEvent = millis();
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
    tone(buzzerPin, melodyNotes[melodyIndex], melodyDurations[melodyIndex]);
    melodyNextEvent = now + melodyDurations[melodyIndex] + melodyPause;
    melodyIndex++;
  }
}

// =========================================================
//  HELPER FUNCTIONS
// =========================================================
int computeResponseTime(int lvl) {
  float t = 6.0 + 1.5 * lvl;
  return (int)(t + 0.5);
}

void showHUD(const char *tag) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("G:");
  lcd.print(tag);
  lcd.print(" L:");
  if (level<10) lcd.print("0");
  lcd.print(level);

  lcd.setCursor(0,1);
  lcd.print("T:");
  if (responseTimeSec<10) lcd.print("0");
  lcd.print(responseTimeSec);
  lcd.print("s");
}

void showMenu() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Close  Start");
  lcd.setCursor(0,1);
  lcd.print("End    Pause");
  simonState = MENU;
}

void handleMenuButtons() {
  unsigned long now = millis();
  if (digitalRead(joySW)==LOW && now-lastJoy>joyDelay) {
    lastJoy = now;
    showMenu();
    return;
  }

  int x = analogRead(joyX);
  int y = analogRead(joyY);

  if (y < LOW_T && now-lastJoy>joyDelay) {
    lcd.clear();
    lastJoy = now;
    return;
  }
  if (y > HIGH_T && now-lastJoy>joyDelay) {
    startGame();
    lastJoy = now;
    return;
  }
  if (x < LOW_T && now-lastJoy>joyDelay) {
    endGame();
    lastJoy = now;
    return;
  }
  if (x > HIGH_T && now-lastJoy>joyDelay) {
    togglePause();
    lastJoy = now;
    return;
  }
}

void startGame() {
  level = 1;
  gameActive = true;

  alive[1]=alive[2]=alive[3]=true;

  memoryNumber="";
  ledPattern="";
  buzPattern="";

  waitingForResponse = false;
  currentPlayerId = 1;

  for (int i=1;i<=3;i++) serialBuf[i]="";

  ss1.println("S_LEVEL,1");
  ss2.println("S_LEVEL,1");
  ss3.println("S_LEVEL,1");

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
    simonState = SEND_MEMORY_NUM;
  }
}

// =========================================================
//  MEMORY NUMBER
// =========================================================
void generateMemoryNumber() {
  int digits = 3 + ((level-1)/3);
  memoryNumber = "";
  for (int i=0;i<digits;i++)
    memoryNumber += String(random(0,10));
}

void runMemoryNumberPhase() {
  unsigned long now = millis();
  static bool sent = false;

  if (!sent) {
    generateMemoryNumber();
    lcd.clear();
    lcd.print("Mem:");
    lcd.print(memoryNumber);

    ss1.println("S_MEM," + memoryNumber);
    ss2.println("S_MEM," + memoryNumber);
    ss3.println("S_MEM," + memoryNumber);

    sent = true;
    phaseStart = now;
    return;
  }

  if (now-phaseStart >= 2000) {
    sent = false;
    simonState = LED_GAME;
  }
}

// =========================================================
//  LED GAME
// =========================================================
void turnOffAllLEDs() {
  for (int i=0;i<4;i++) digitalWrite(ledPins[i], LOW);
}

void setLED(char c) {
  turnOffAllLEDs();
  if (c=='R') digitalWrite(ledPins[0], HIGH);
  if (c=='G') digitalWrite(ledPins[1], HIGH);
  if (c=='Y') digitalWrite(ledPins[2], HIGH);
  if (c=='B') digitalWrite(ledPins[3], HIGH);
}

void generateLEDPattern(int len) {
  ledPattern="";
  char c[4]={'R','G','Y','B'};
  for (int i=0;i<len;i++)
    ledPattern+=c[random(0,4)];
}

void runLEDGame() {
  unsigned long now = millis();
  static bool init=false, showing=false;
  static int idx=0;
  static unsigned long lastStep=0;
  static const unsigned long interval=600;

  if (!init) {
    lcd.clear();
    lcd.print("LED Pattern...");
    generateLEDPattern(level+2);

    idx=0;
    showing=true;
    lastStep=now;
    init=true;

    ss1.println("S_LED_START");
    ss2.println("S_LED_START");
    ss3.println("S_LED_START");
    return;
  }

  if (showing) {
    if (idx < ledPattern.length()) {
      if (now-lastStep >= interval) {
        lastStep = now;
        setLED(ledPattern[idx]);
        idx++;
      }
    } else {
      turnOffAllLEDs();
      showing=false;

      responseTimeSec = computeResponseTime(level);
      responseWindow  = responseTimeSec * 1000UL;
      phaseStart = now;
      lastCountdownTick = now;

      showHUD("LED");

      ss1.println("S_TIME," + String(responseTimeSec));
      ss2.println("S_TIME," + String(responseTimeSec));
      ss3.println("S_TIME," + String(responseTimeSec));
    }
    return;
  }

  if (now-phaseStart >= responseWindow) {
    init=false;
    showing=false;
    idx=0;
    currentMiniGame = MG_LED;
    currentPlayerId = 1;
    waitingForResponse=false;

    for (int i=1;i<=3;i++) serialBuf[i]="";

    simonState = PROCESS_RESULTS;
  }

  if (responseTimeSec>0 && now-lastCountdownTick>=1000) {
    lastCountdownTick+=1000;
    responseTimeSec--;
    showHUD("LED");
  }
}

// =========================================================
//  BUZZER GAME
// =========================================================
void generateBuzzerPattern(int len) {
  buzPattern="";
  char s[2]={'H','L'};
  for (int i=0;i<len;i++)
    buzPattern+=s[random(0,2)];
}

void playBuzzerSymbol(char s) {
  if (s=='H') tone(buzzerPin,900,200);
  else        tone(buzzerPin,400,500);
}

void runBuzzerGame() {
  unsigned long now=millis();
  static bool init=false, showing=false;
  static int idx=0;
  static unsigned long nextTime=0;

  if (!init) {
    lcd.clear();
    lcd.print("Buzzer...");
    generateBuzzerPattern(level+2);

    idx=0;
    showing=true;
    nextTime=now;
    init=true;

    ss1.println("S_BUZ_START");
    ss2.println("S_BUZ_START");
    ss3.println("S_BUZ_START");
    return;
  }

  if (showing) {
    if (idx < buzPattern.length()) {
      if (now >= nextTime) {
        char s=buzPattern[idx++];
        unsigned long d=(s=='H'?200:500);
        playBuzzerSymbol(s);
        nextTime = now + d + 120;
      }
    } else {
      showing=false;
      responseTimeSec = computeResponseTime(level);
      responseWindow  = responseTimeSec*1000UL;
      phaseStart      = now;
      lastCountdownTick=now;

      showHUD("BUZ");

      ss1.println("S_TIME,"+String(responseTimeSec));
      ss2.println("S_TIME,"+String(responseTimeSec));
      ss3.println("S_TIME,"+String(responseTimeSec));
    }
    return;
  }

  if (now-phaseStart >= responseWindow) {
    init=false;
    showing=false;
    idx=0;
    currentMiniGame = MG_BUZ;
    currentPlayerId = 1;
    waitingForResponse=false;
    for(int i=1;i<=3;i++) serialBuf[i]="";
    simonState = PROCESS_RESULTS;
  }

  if (responseTimeSec>0 && now-lastCountdownTick>=1000) {
    lastCountdownTick+=1000;
    responseTimeSec--;
    showHUD("BUZ");
  }
}

// =========================================================
//  ULTRASONIC GAME
// =========================================================
void runUSGame() {
  unsigned long now = millis();
  static bool init=false;

  if (!init) {
    lcd.clear();
    lcd.print("US Dist...");
    targetDistanceCm = random(15,41);

    responseTimeSec = usFixedTime;
    responseWindow  = usFixedTime*1000UL;
    phaseStart      = now;
    lastCountdownTick=now;

    showHUD("US");

    ss1.println("S_US_START," + String(targetDistanceCm));
    ss2.println("S_US_START," + String(targetDistanceCm));
    ss3.println("S_US_START," + String(targetDistanceCm));

    ss1.println("S_TIME,8");
    ss2.println("S_TIME,8");
    ss3.println("S_TIME,8");

    init=true;
    return;
  }

  if (now-phaseStart >= responseWindow) {
    init=false;
    currentMiniGame = MG_US;
    currentPlayerId = 1;
    waitingForResponse=false;
    for (int i=1;i<=3;i++) serialBuf[i]="";
    simonState = PROCESS_RESULTS;
  }

  if (responseTimeSec>0 && now-lastCountdownTick>=1000) {
    lastCountdownTick+=1000;
    responseTimeSec--;
    showHUD("US");
  }
}

// =========================================================
//  RECALL GAME (IR)
// =========================================================
void runRecallGame() {
  unsigned long now = millis();
  static bool init=false;

  if (!init) {
    lcd.clear();
    lcd.print("Recall...");

    responseTimeSec = usFixedTime;
    responseWindow  = usFixedTime*1000UL;
    phaseStart      = now;
    lastCountdownTick=now;

    showHUD("MEM");

    ss1.println("S_RECALL_START");
    ss2.println("S_RECALL_START");
    ss3.println("S_RECALL_START");

    ss1.println("S_TIME,8");
    ss2.println("S_TIME,8");
    ss3.println("S_TIME,8");

    init=true;
    return;
  }

  if (now-phaseStart >= responseWindow) {
    init=false;
    currentMiniGame = MG_MEM;
    currentPlayerId = 1;
    waitingForResponse=false;
    for (int i=1;i<=3;i++) serialBuf[i]="";
    simonState = PROCESS_RESULTS;
  }

  if (responseTimeSec>0 && now-lastCountdownTick>=1000) {
    lastCountdownTick+=1000;
    responseTimeSec--;
    showHUD("MEM");
  }
}

// =========================================================
//  TOKEN SYSTEM — UPDATED FOR 3 SEPARATE SERIAL LINKS
// =========================================================
void requestPlayer(int id, const String &tag) {
  channels[id]->println("S_REQ," + String(id) + "," + tag);
  tokenStart = millis();
  waitingForResponse = true;
  serialBuf[id] = "";
}

void handleResult() {
  unsigned long now = millis();

  if (!waitingForResponse) {
    while (currentPlayerId <= 3 && !alive[currentPlayerId]) currentPlayerId++;
    if (currentPlayerId > 3) {
      advanceOrEnd();
      return;
    }
    // ask current player
    switch(currentMiniGame) {
      case MG_LED: requestPlayer(currentPlayerId, "LED"); break;
      case MG_BUZ: requestPlayer(currentPlayerId, "BUZ"); break;
      case MG_US:  requestPlayer(currentPlayerId, "US");  break;
      case MG_MEM: requestPlayer(currentPlayerId, "MEM"); break;
    }
    return;
  }

  // read incoming
  SoftwareSerial* ss = channels[currentPlayerId];
  while (ss->available()) {
    char c = ss->read();
    if (c == '\n') {
      parseResponse(currentPlayerId);
      return;
    } else if (c != '\r') {
      serialBuf[currentPlayerId] += c;
    }
  }

  // timeout
  if (now - tokenStart >= tokenTimeout) {
    alive[currentPlayerId] = false;
    ss->println("S_RES," + String(currentPlayerId) + ",FAIL");
    waitingForResponse = false;
    currentPlayerId++;
  }
}

void parseResponse(int id) {
  String &line = serialBuf[id];

  int c1=line.indexOf(',');
  int c2=line.indexOf(',',c1+1);
  int c3=line.indexOf(',',c2+1);
  if (c1<0||c2<0||c3<0) { waitingForResponse=false; currentPlayerId++; return; }

  String prefix=line.substring(0,c1);
  int pid=line.substring(c1+1,c2).toInt();
  String type=line.substring(c2+1,c3);
  String ans=line.substring(c3+1);

  if (prefix!="P_RESP" || pid!=id) {
    waitingForResponse=false;
    currentPlayerId++;
    return;
  }

  SoftwareSerial* ss = channels[id];

  if (currentMiniGame == MG_LED) {
    if (ans == ledPattern) ss->println("S_RES,"+String(id)+",OK");
    else { ss->println("S_RES,"+String(id)+",FAIL"); alive[id]=false; }
  }

  if (currentMiniGame == MG_BUZ) {
    if (ans == buzPattern) ss->println("S_RES,"+String(id)+",OK");
    else { ss->println("S_RES,"+String(id)+",FAIL"); alive[id]=false; }
  }

  if (currentMiniGame == MG_US) {
    int d = ans.toInt();
    if (abs(d - targetDistanceCm) <= usTolerance)
      ss->println("S_RES,"+String(id)+",OK");
    else {
      ss->println("S_RES,"+String(id)+",FAIL");
      alive[id]=false;
    }
  }

  if (currentMiniGame == MG_MEM) {
    if (ans == memoryNumber) ss->println("S_RES,"+String(id)+",OK");
    else { ss->println("S_RES,"+String(id)+",FAIL"); alive[id]=false; }
  }

  waitingForResponse=false;
  currentPlayerId++;
}

void advanceOrEnd() {
  bool anyAlive=false;
  for (int i=1;i<=3;i++) if (alive[i]) anyAlive=true;
  if (!anyAlive) { simonState=GAME_OVER; return; }

  if (currentMiniGame == MG_LED) { simonState = BUZ_GAME; return; }
  if (currentMiniGame == MG_BUZ) { simonState = US_GAME;  return; }
  if (currentMiniGame == MG_US)  { simonState = RECALL_GAME; return; }

  if (currentMiniGame == MG_MEM) {
    level++;
    if (level > 18) { simonState = GAME_OVER; return; }

    ss1.println("S_LEVEL," + String(level));
    ss2.println("S_LEVEL," + String(level));
    ss3.println("S_LEVEL," + String(level));

    simonState = SEND_MEMORY_NUM;
    return;
  }
}

void displayGameOver() {
  unsigned long now = millis();
  static bool shown=false;
  if (!shown) {
    lcd.clear();
    lcd.print("GAME OVER");
    shown=true;
    phaseStart=now;
  }
  if (now-phaseStart>=2000) {
    shown=false;
    showMenu();
  }
}

// =========================================================
//  SETUP
// =========================================================
void setup() {
  lcd.init();
  lcd.backlight();

  ss1.begin(9600);
  ss2.begin(9600);
  ss3.begin(9600);

  pinMode(joySW, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);

  for (int i=0;i<4;i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  randomSeed(analogRead(A0));
  showMenu();
}

// =========================================================
//  LOOP
// =========================================================
void loop() {
  handleMenuButtons();
  updateMelody();

  switch (simonState) {
    case MENU: break;
    case WAITING_START: if (millis()-phaseStart>1500) simonState=SEND_MEMORY_NUM; break;
    case SEND_MEMORY_NUM: runMemoryNumberPhase(); break;
    case LED_GAME:   runLEDGame(); break;
    case BUZ_GAME:   runBuzzerGame(); break;
    case US_GAME:    runUSGame(); break;
    case RECALL_GAME:runRecallGame(); break;
    case PROCESS_RESULTS: handleResult(); break;
    case GAME_OVER:  displayGameOver(); break;
    case PAUSED: break;
  }
}
