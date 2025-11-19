#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <IRremote.h>

// CONFIGURATION
#define PLAYER_ID 2
// Pins
const int buttonR = 4;
const int buttonG = 5;
const int buttonY = 6;
const int buttonB = 7;

const int joyX = A1;      // buzzer input using joystick LR
const int trigPin = 8;    
const int echoPin = 9;

const int irPin = 3;      // IR receiver pin

SoftwareSerial link(10,11);   // Shared RX/TX bus for Simon

LiquidCrystal_I2C lcd(0x27, 16, 2);

// PLAYER STATE
String ledInput = "";
String buzInput = "";
String usInput = "";
String memInput = "";

bool ledLocked = false;
bool buzLocked = false;
bool usLocked  = false;
bool memLocked = false;

int currentLevel = 1;

// Countdown timer
int timeRemaining = 0;
unsigned long lastSecondTick = 0;
bool timerActive = false;

// Serial buffer
String serialLine = "";

// IR remote
IRrecv irrecv(irPin);
decode_results results;


// BASIC FUNCTIONS

void printHUD() {
  lcd.setCursor(0,1);
  lcd.print("P");
  lcd.print(PLAYER_ID);
  lcd.print(" L:");
  lcd.print(currentLevel);
  lcd.print(" ");

  if (timerActive) {
    lcd.print(timeRemaining);
    lcd.print("s ");
  } else {
    lcd.print("    ");
  }
}

void startCountdown(int seconds) {
  timeRemaining = seconds;
  timerActive = true;
  lastSecondTick = millis();
  printHUD();
}

void handleCountdown() {
  if (!timerActive) return;

  unsigned long now = millis();

  if (now - lastSecondTick >= 1000) {
    lastSecondTick = now;

    if (timeRemaining > 0) {
      timeRemaining--;
      printHUD();
    } else {
      timerActive = false;

      // auto-lock all answers for the mini-game
      ledLocked = true;
      buzLocked = true;
      usLocked  = true;
      memLocked = true;
    }
  }
}


// LED INPUT
void handleLEDInput() {
  if (ledLocked) return;

  if (digitalRead(buttonR) == LOW) { ledInput += "R"; delay(200); }
  if (digitalRead(buttonG) == LOW) { ledInput += "G"; delay(200); }
  if (digitalRead(buttonY) == LOW) { ledInput += "Y"; delay(200); }
  if (digitalRead(buttonB) == LOW) { ledInput += "B"; delay(200); }
}


// BUZZER INPUT
void handleBuzzerInput() {
  if (buzLocked) return;

  int x = analogRead(joyX);

  if (x < 300) { buzInput += "L"; delay(300); }
  else if (x > 700) { buzInput += "H"; delay(300); }
}


// ULTRASONIC
long readDistance() {
  digitalWrite(trigPin, LOW);  
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);  
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH);
  return duration * 0.034 / 2;  // cm
}

void handleUltrasonicInput() {
  if (usLocked) return;

  long d = readDistance();
  usInput = String((int)d);
}


// IR MEMORY INPUT
void handleIR() {
  if (memLocked) return;

  if (irrecv.decode(&results)) {
    unsigned long val = results.value;

    // numeric keys 0–9
    if (val >= 0xFFA25D && val <= 0xFF9867) {
      int digit = decodeIRDigit(val);
      if (digit >= 0) {
        memInput += String(digit);
        if (memInput.length() >= 3) memLocked = true;
      }
    }

    irrecv.resume();
  }
}

int decodeIRDigit(unsigned long code) {
  switch(code) {
    case 0xFF6897: return 0;
    case 0xFF30CF: return 1;
    case 0xFF18E7: return 2;
    case 0xFF7A85: return 3;
    case 0xFF10EF: return 4;
    case 0xFF38C7: return 5;
    case 0xFF5AA5: return 6;
    case 0xFF42BD: return 7;
    case 0xFF4AB5: return 8;
    case 0xFF52AD: return 9;
  }
  return -1;
}


// PROCESS SIMON MSGS
void processLine(String line) {

  // Simon sets level
  if (line.startsWith("S_LEVEL,")) {
    currentLevel = line.substring(8).toInt();
    printHUD();
    return;
  }

  // Simon says how much time we have
  if (line.startsWith("S_TIME,")) {
    int t = line.substring(7).toInt();
    startCountdown(t);
    return;
  }

  // Simon displays memory number
  if (line.startsWith("S_MEM,")) {
    String num = line.substring(6);
    memInput = "";
    memLocked = false;

    lcd.clear();
    lcd.print("Memory: ");
    lcd.print(num);
    printHUD();
    return;
  }

  // Simon announces LED / BUZ / US game start
  if (line == "S_LED_START") {
    ledInput = "";
    ledLocked = false;
    lcd.clear();
    lcd.print("LED Game...");
    printHUD();
    return;
  }

  if (line == "S_BUZ_START") {
    buzInput = "";
    buzLocked = false;
    lcd.clear();
    lcd.print("BUZ Game...");
    printHUD();
    return;
  }

  if (line == "S_US_START") {
    usInput = "";
    usLocked = false;
    lcd.clear();
    lcd.print("Distance...");
    printHUD();
    return;
  }

  if (line == "S_MEM_START") {
    memInput = "";
    memLocked = false;
    lcd.clear();
    lcd.print("Enter #...");
    printHUD();
    return;
  }

  // Simon requests answer
  // Format:
  //    S_REQ,<playerID>,<GAME>
  if (line.startsWith("S_REQ,")) {
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1+1);

    int reqId = line.substring(c1+1, c2).toInt();
    String game = line.substring(c2+1);

    if (reqId != PLAYER_ID) return;

    String payload = "P_RESP," + String(PLAYER_ID) + "," + game + ",";

    if (game == "LED") payload += ledInput;
    if (game == "BUZ") payload += buzInput;
    if (game == "US")  payload += usInput;
    if (game == "MEM") payload += memInput;

    link.println(payload);
    return;
  }

  // Simon says pass/fail
  if (line.startsWith("S_RES,")) {
    lcd.clear();
    lcd.print(line.endsWith("OK") ? "PASS" : "FAIL");
    printHUD();
    return;
  }
}


// SERIAL INPUT
void handleSerial() {
  while (link.available()) {
    char c = link.read();

    if (c == '\n') {
      processLine(serialLine);
      serialLine = "";
    } 
    else if (c != '\r') {
      serialLine += c;
    }
  }
}


// SETUP
void setup() {
  lcd.init();
  lcd.backlight();

  pinMode(buttonR, INPUT_PULLUP);
  pinMode(buttonG, INPUT_PULLUP);
  pinMode(buttonY, INPUT_PULLUP);
  pinMode(buttonB, INPUT_PULLUP);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  link.begin(9600);
  irrecv.enableIRIn();

  lcd.clear();
  lcd.print("Player ");
  lcd.print(PLAYER_ID);
  delay(1000);
}


// LOOP
void loop() {
  handleSerial();
  handleCountdown();

  handleLEDInput();
  handleBuzzerInput();
  handleUltrasonicInput();
  handleIR();
}
