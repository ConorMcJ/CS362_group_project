/*
 * Player 1 Device - Simon Says Multiplayer Game
 * 
 * This device responds to commands from the Simon device and collects
 * player input for various mini-games:
 * - LED Game: Button presses for pattern matching
 * - Buzzer Game: Joystick input for tone sequence matching
 * - Ultrasonic Game: Distance measurement (10-100cm range)
 * - Recall Game: IR remote input for number recall
 * 
 * Hardware:
 * - Arduino Uno R3
 * - 4x Pushbuttons (pins 2-5)
 * - Joystick (Y-axis on A0)
 * - Ultrasonic sensor (TRIG=7, ECHO=8)
 * - IR Receiver (pin 6)
 * - SoftwareSerial (RX=10, TX=11)
 * 
 * Communication Protocol:
 * - Listen for commands: '$'[CMD][DATA]'#'
 * - Send responses: '@'[PLAYER_ID][DATA]'!'
 * - Only transmit when explicitly requested by Simon
 */

#include <SoftwareSerial.h>
#include <IRremote.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- CONFIGURATION ---
#include "config.h"  // Contains PLAYER_ID (1, 2, or 3)

// --- PIN DEFINITIONS ---
#define RX 10
#define TX 11

#define BUTTON_1 2
#define BUTTON_2 3
#define BUTTON_3 4
#define BUTTON_4 5

#define JOYSTICK_Y A0

#define TRIG_PIN 7
#define ECHO_PIN 8

#define IR_PIN 6

// --- CONSTANTS ---
const byte MAX_PATTERN_LEN = 50;  // Support for higher levels with chunked transmission
const unsigned long DEBOUNCE_DELAY = 50;      // 50ms button debounce
const unsigned long JOYSTICK_DEBOUNCE = 300;  // 300ms joystick debounce
const unsigned long RECALL_TIMEOUT = 10000;   // 10 seconds for recall input

// --- STATE MACHINE ---
enum PlayerState {
  WAITING,        // Idle, listening for commands
  LED_INPUT,      // Recording button presses for LED game
  BUZZER_INPUT,   // Recording joystick moves for buzzer game
  US_CAPTURE,     // Capturing ultrasonic distance
  RECALL_INPUT,   // Recording IR remote input for recall game
  DATA_READY      // Data collected, ready to send
};

PlayerState currentState = WAITING;
PlayerState lastGameType = WAITING;  // Track which game type collected data

// --- COMMUNICATION ---
SoftwareSerial mySerial(RX, TX);

// --- INPUT STORAGE ---
byte ledPattern[MAX_PATTERN_LEN];
byte ledPatternIndex = 0;

byte buzzerPattern[MAX_PATTERN_LEN];
byte buzzerPatternIndex = 0;

int capturedDistance = 0;

byte recallDigits[3];
byte recallIndex = 0;

// --- TIMING ---
unsigned long inputStartTime = 0;
unsigned long lastButton1Press = 0;
unsigned long lastButton2Press = 0;
unsigned long lastButton3Press = 0;
unsigned long lastButton4Press = 0;
unsigned long lastJoystickInput = 0;

// --- BUTTON STATE TRACKING ---
// Track previous button states for edge detection (prevents repeat triggers while held)
byte prevButton1State = HIGH;
byte prevButton2State = HIGH;
byte prevButton3State = HIGH;
byte prevButton4State = HIGH;

// Track stable (debounced) button states
byte stableButton1State = HIGH;
byte stableButton2State = HIGH;
byte stableButton3State = HIGH;
byte stableButton4State = HIGH;

// IR Remote
// IRremote 4.x uses global IrReceiver object, no separate declaration needed

// LCD Debugging
LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C address 0x27, 16x2 display
unsigned long lastLCDUpdate = 0;
const unsigned long LCD_UPDATE_INTERVAL = 100;  // Update LCD every 100ms max

// Debug helper to get state name
const char* getStateName(PlayerState state) {
  switch (state) {
    case WAITING: return "WAITING";
    case LED_INPUT: return "LED_INPUT";
    case BUZZER_INPUT: return "BUZ_INPUT";
    case US_CAPTURE: return "US_CAPTURE";
    case RECALL_INPUT: return "RECALL_IN";
    case DATA_READY: return "DATA_RDY";
    default: return "UNKNOWN";
  }
}

// LCD debug helper - updates display with state and data info
void debugLCD(const char* line1, const char* line2 = "");
void debugLCDState();
void debugLCDInput(const char* inputType, int value);

// --- FORWARD DECLARATIONS ---
void checkForCommands();
void processCommand(byte* cmd, byte len);
void handleLEDInputState();
void handleBuzzerInputState();
void handleUSCaptureState();
void handleRecallInputState();
bool handlePushButton(byte reading, byte &prevReading, byte &stableState, unsigned long &lastDebounceTime);
int measureDistance();
byte decodeIRDigit(unsigned long code);
void sendDataToSimon();
void sendChunkedPattern(byte* pattern, byte length);

// --- SETUP ---
void setup() {
  // Initialize LCD first for debug output
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Player ");
  lcd.print(PLAYER_ID);
  lcd.print(" Init");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  
  // Serial communication
  mySerial.begin(9600);
  
  // Button pins with internal pull-up resistors
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);
  pinMode(BUTTON_4, INPUT_PULLUP);
  
  // Ultrasonic pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  
  // IR receiver (IRremote 4.x API)
  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK);
  
  // Seed random (if needed for future use)
  randomSeed(analogRead(A1));
  
  // Show ready message
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("P");
  lcd.print(PLAYER_ID);
  lcd.print(":WAITING");
  lcd.setCursor(0, 1);
  lcd.print("Ready for Simon");
}

// --- MAIN LOOP ---
void loop() {
  // Always check for incoming commands from Simon
  checkForCommands();
  
  // Handle current state
  switch (currentState) {
    case WAITING:
      // Do nothing, just wait for commands
      break;
      
    case LED_INPUT:
      handleLEDInputState();
      break;
      
    case BUZZER_INPUT:
      handleBuzzerInputState();
      break;
      
    case US_CAPTURE:
      handleUSCaptureState();
      break;
      
    case RECALL_INPUT:
      handleRecallInputState();
      break;
      
    case DATA_READY:
      // Wait for data request from Simon
      break;
  }
}

/*
  COMMUNICATION
*/
void checkForCommands() {
  static bool inMessage = false;
  static byte msgBuffer[10];
  static byte msgIndex = 0;
  
  while (mySerial.available() > 0) {
    char c = mySerial.read();
    
    if (c == '$') {
      // Start of message
      inMessage = true;
      msgIndex = 0;
    } else if (c == '#' && inMessage) {
      // End of message - process it
      processCommand(msgBuffer, msgIndex);
      inMessage = false;
      msgIndex = 0;
    } else if (inMessage && msgIndex < 10) {
      msgBuffer[msgIndex++] = c;
    }
  }
}

void processCommand(byte* cmd, byte len) {
  if (len < 1) return;
  
  char command = cmd[0];
  
  // Debug: Show received command
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CMD:");
  lcd.print((char)command);
  if (len > 1) {
    lcd.print((char)cmd[1]);
  }
  
  switch (command) {
    case 'W':  // Wait - return to idle
      currentState = WAITING;
      // Clear any stored data
      ledPatternIndex = 0;
      buzzerPatternIndex = 0;
      recallIndex = 0;
      capturedDistance = 0;
      lastGameType = WAITING;
      lcd.setCursor(0, 1);
      lcd.print("->WAITING");
      break;
      
    case 'S':  // Start game
      if (len > 1) {
        char gameType = cmd[1];
        switch (gameType) {
          case 'L':  // LED game
            currentState = LED_INPUT;
            lastGameType = LED_INPUT;
            ledPatternIndex = 0;
            inputStartTime = millis();
            lcd.setCursor(0, 1);
            lcd.print("->LED_INPUT");
            break;
            
          case 'B':  // Buzzer game
            currentState = BUZZER_INPUT;
            lastGameType = BUZZER_INPUT;
            buzzerPatternIndex = 0;
            inputStartTime = millis();
            lcd.setCursor(0, 1);
            lcd.print("->BUZZER_INPUT");
            break;
            
          case 'U':  // Ultrasonic game
            currentState = US_CAPTURE;
            lastGameType = US_CAPTURE;
            capturedDistance = 0;
            lcd.setCursor(0, 1);
            lcd.print("->US_CAPTURE");
            break;
            
          case 'R':  // Recall game
            currentState = RECALL_INPUT;
            lastGameType = RECALL_INPUT;
            recallIndex = 0;
            inputStartTime = millis();
            lcd.setCursor(0, 1);
            lcd.print("->RECALL_INPUT");
            break;
        }
      }
      break;
      
    case 'R':  // Request data
      if (len > 1 && cmd[1] == ('0' + PLAYER_ID)) {
        lcd.setCursor(0, 1);
        lcd.print("Sending data...");
        // This is a request for our data - send it
        sendDataToSimon();
        currentState = WAITING;
      } else {
        lcd.setCursor(0, 1);
        lcd.print("Not for me");
      }
      break;
      
    case 'E':  // End game
      currentState = WAITING;
      // Reset all data
      ledPatternIndex = 0;
      buzzerPatternIndex = 0;
      recallIndex = 0;
      capturedDistance = 0;
      lastGameType = WAITING;
      lcd.setCursor(0, 1);
      lcd.print("->END GAME");
      break;
  }
}

void sendDataToSimon() {
  // Debug: Show we're sending data
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SENDING->Simon");
  
  mySerial.write('@');
  mySerial.write('0' + PLAYER_ID);
  
  // Determine which data to send based on last game played
  switch (lastGameType) {
    case LED_INPUT:
      lcd.setCursor(0, 1);
      lcd.print("LED[");
      lcd.print(ledPatternIndex);
      lcd.print("]:");
      for (byte i = 0; i < min((int)ledPatternIndex, 8); i++) {
        lcd.print(ledPattern[i]);
      }
      sendChunkedPattern(ledPattern, ledPatternIndex);
      break;
      
    case BUZZER_INPUT:
      lcd.setCursor(0, 1);
      lcd.print("BUZ[");
      lcd.print(buzzerPatternIndex);
      lcd.print("]:");
      for (byte i = 0; i < min((int)buzzerPatternIndex, 8); i++) {
        lcd.print(buzzerPattern[i]);
      }
      sendChunkedPattern(buzzerPattern, buzzerPatternIndex);
      break;
      
    case US_CAPTURE:
      // Send distance (up to 3 digits)
      {
        lcd.setCursor(0, 1);
        lcd.print("US Dist: ");
        lcd.print(capturedDistance);
        lcd.print("cm");
        
        char distStr[4];
        itoa(capturedDistance, distStr, 10);
        byte len = strlen(distStr);
        mySerial.write('0' + len);
        for (byte i = 0; i < len; i++) {
          mySerial.write(distStr[i]);
        }
      }
      break;
      
    case RECALL_INPUT:
      // Send 3 digits
      lcd.setCursor(0, 1);
      lcd.print("RECALL: ");
      for (byte i = 0; i < 3; i++) {
        lcd.print(recallDigits[i]);
      }
      
      mySerial.write('0' + 3);
      for (byte i = 0; i < 3; i++) {
        mySerial.write('0' + recallDigits[i]);
      }
      break;
      
    default:
      // No data - send empty (length 0)
      lcd.setCursor(0, 1);
      lcd.print("(no data)");
      mySerial.write('0');
      break;
  }
  
  mySerial.write('!');
}

// Helper function for chunked pattern transmission (supports large patterns)
void sendChunkedPattern(byte* pattern, byte length) {
  const byte CHUNK_SIZE = 10;
  byte chunks = (length + CHUNK_SIZE - 1) / CHUNK_SIZE;
  
  mySerial.write('0' + length);  // Total length
  
  for (byte chunk = 0; chunk < chunks; chunk++) {
    byte startIdx = chunk * CHUNK_SIZE;
    byte endIdx = min((int)(startIdx + CHUNK_SIZE), (int)length);
    
    for (byte i = startIdx; i < endIdx; i++) {
      mySerial.write('0' + pattern[i]);
    }
    
    // Add chunk separator if not last chunk
    if (chunk < chunks - 1) {
      mySerial.write('|');
    }
  }
}

/*
  STATE HANDLERS
*/

// LED Game - Record button presses
void handleLEDInputState() {
  bool buttonPressed = false;
  byte pressedButton = 0;
  
  // Read current button states
  byte btn1 = digitalRead(BUTTON_1);
  byte btn2 = digitalRead(BUTTON_2);
  byte btn3 = digitalRead(BUTTON_3);
  byte btn4 = digitalRead(BUTTON_4);
  
  // Check button 1 - only trigger on falling edge (HIGH->LOW) with debounce
  if (handlePushButton(btn1, prevButton1State, stableButton1State, lastButton1Press)) {
    if (ledPatternIndex < MAX_PATTERN_LEN) {
      ledPattern[ledPatternIndex++] = 1;
      buttonPressed = true;
      pressedButton = 1;
    }
  }
  
  // Check button 2 - only trigger on falling edge (HIGH->LOW) with debounce
  if (handlePushButton(btn2, prevButton2State, stableButton2State, lastButton2Press)) {
    if (ledPatternIndex < MAX_PATTERN_LEN) {
      ledPattern[ledPatternIndex++] = 2;
      buttonPressed = true;
      pressedButton = 2;
    }
  }
  
  // Check button 3 - only trigger on falling edge (HIGH->LOW) with debounce
  if (handlePushButton(btn3, prevButton3State, stableButton3State, lastButton3Press)) {
    if (ledPatternIndex < MAX_PATTERN_LEN) {
      ledPattern[ledPatternIndex++] = 3;
      buttonPressed = true;
      pressedButton = 3;
    }
  }
  
  // Check button 4 - only trigger on falling edge (HIGH->LOW) with debounce
  if (handlePushButton(btn4, prevButton4State, stableButton4State, lastButton4Press)) {
    if (ledPatternIndex < MAX_PATTERN_LEN) {
      ledPattern[ledPatternIndex++] = 4;
      buttonPressed = true;
      pressedButton = 4;
    }
  }
  
  // Debug: Show button press on LCD
  if (buttonPressed) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("LED BTN:");
    lcd.print(pressedButton);
    lcd.setCursor(0, 1);
    lcd.print("Pattern[");
    lcd.print(ledPatternIndex);
    lcd.print("]:");
    // Show last few entries
    for (byte i = (ledPatternIndex > 8 ? ledPatternIndex - 8 : 0); i < ledPatternIndex; i++) {
      lcd.print(ledPattern[i]);
    }
  }
}

// Buzzer Game - Record joystick moves
void handleBuzzerInputState() {
  int yVal = analogRead(JOYSTICK_Y);
  bool inputRecorded = false;
  const char* direction = "";
  
  if (millis() - lastJoystickInput > JOYSTICK_DEBOUNCE) {
    if (yVal < 300) {  // Joystick pushed up = HIGH tone (1)
      if (buzzerPatternIndex < MAX_PATTERN_LEN) {
        buzzerPattern[buzzerPatternIndex++] = 1;
        inputRecorded = true;
        direction = "DOWN(0)";
      }
      lastJoystickInput = millis();
    } else if (yVal > 700) {  // Joystick pushed down = LOW tone (0)
      if (buzzerPatternIndex < MAX_PATTERN_LEN) {
        buzzerPattern[buzzerPatternIndex++] = 0;
        inputRecorded = true;
        direction = "UP(1)";
      }
      lastJoystickInput = millis();
    }
  }
  
  // Debug: Show joystick input on LCD
  if (inputRecorded) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("JOY:");
    lcd.print(direction);
    lcd.print(" Y=");
    lcd.print(yVal);
    lcd.setCursor(0, 1);
    lcd.print("Buzz[");
    lcd.print(buzzerPatternIndex);
    lcd.print("]:");
    // Show last few entries
    for (byte i = (buzzerPatternIndex > 8 ? buzzerPatternIndex - 8 : 0); i < buzzerPatternIndex; i++) {
      lcd.print(buzzerPattern[i]);
    }
  }
}

// Ultrasonic Game - Capture distance
void handleUSCaptureState() {
  capturedDistance = measureDistance();
  
  // Debug: Show captured distance on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("US CAPTURED!");
  lcd.setCursor(0, 1);
  lcd.print("Dist: ");
  lcd.print(capturedDistance);
  lcd.print(" cm");
  if (capturedDistance == 0) {
    lcd.setCursor(12, 1);
    lcd.print("ERR");
  }
  
  currentState = DATA_READY;
}

// Recall Game - Record IR remote digits
void handleRecallInputState() {
  // Check timeout
  if (millis() - inputStartTime > RECALL_TIMEOUT) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RECALL TIMEOUT!");
    lcd.setCursor(0, 1);
    lcd.print("Got ");
    lcd.print(recallIndex);
    lcd.print(" digits");
    currentState = DATA_READY;
    return;
  }
  
  // Periodic update showing time remaining (every second)
  static unsigned long lastTimeUpdate = 0;
  if (millis() - lastTimeUpdate > 1000) {
    unsigned long elapsed = millis() - inputStartTime;
    unsigned long remaining = (RECALL_TIMEOUT - elapsed) / 1000;
    lcd.setCursor(0, 0);
    lcd.print("RECALL ");
    lcd.print(remaining);
    lcd.print("s  ");
    lcd.setCursor(0, 1);
    lcd.print("Digits[");
    lcd.print(recallIndex);
    lcd.print("/3]:");
    for (byte i = 0; i < recallIndex; i++) {
      lcd.print(recallDigits[i]);
    }
    lcd.print("   ");
    lastTimeUpdate = millis();
  }
  
  // Check for IR input (IRremote 4.x API)
  if (IrReceiver.decode()) {
    // Get the raw command code - use decodedRawData for full code
    unsigned long irCode = IrReceiver.decodedIRData.decodedRawData;
    byte digit = decodeIRDigit(irCode);
    
    // Debug: Show IR code received
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("IR:0x");
    lcd.print(irCode, HEX);
    
    // Also show the command byte (useful for NEC remotes)
    lcd.setCursor(0, 1);
    lcd.print("Cmd:");
    lcd.print(IrReceiver.decodedIRData.command, HEX);
    
    if (digit <= 9 && recallIndex < 3) {
      recallDigits[recallIndex++] = digit;
      lcd.setCursor(8, 1);
      lcd.print("D");
      lcd.print(digit);
      lcd.print("[");
      lcd.print(recallIndex);
      lcd.print("/3]");
    } else if (digit == 255) {
      lcd.setCursor(8, 1);
      lcd.print("Unknown!");
    }
    
    IrReceiver.resume();  // Receive next value
    
    // If 3 digits entered, done
    if (recallIndex >= 3) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("RECALL COMPLETE!");
      lcd.setCursor(0, 1);
      lcd.print("Number: ");
      for (byte i = 0; i < 3; i++) {
        lcd.print(recallDigits[i]);
      }
      currentState = DATA_READY;
    }
  }
}

// --- HELPER FUNCTIONS ---

bool handlePushButton(byte reading, byte &prevReading, byte &stableState, unsigned long &lastDebounceTime) {
  bool pressed = false;
  
  // If reading changed from last raw reading, reset debounce timer
  if (reading != prevReading) {
    lastDebounceTime = millis();
  }
  
  // If reading has been stable for DEBOUNCE_DELAY, update stable state
  if (millis() - lastDebounceTime > DEBOUNCE_DELAY) {
    // If stable state is changing, check for falling edge
    if (reading != stableState) {
      if (reading == LOW) {
        // Falling edge detected (button pressed)
        pressed = true;
      }
      stableState = reading;
    }
  }
  
  // Always update previous reading for next iteration
  prevReading = reading;
  return pressed;
}

int measureDistance() {
  // Send trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Read echo pulse (30ms timeout = ~500cm max)
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  
  // Calculate distance in cm
  int distance = duration * 0.034 / 2;
  
  // Validate range (Simon only asks for 10-100cm)
  if (distance < 10 || distance > 100) {
    distance = 0;  // Invalid reading - outside game range
  }
  
  return distance;
}

byte decodeIRDigit(unsigned long code) {
  // Map IR remote codes to digits 0-9
  // NOTE: These values depend on your specific IR remote
  // IRremote 4.x may return codes in different formats
  // The LCD will show what codes your remote sends - update these values accordingly
  
  // Common NEC remote codes (old library format - 24-bit)
  switch (code) {
    case 0xFF16E9: return 0;
    case 0xFF0CF3: return 1;
    case 0xFF18E7: return 2;
    case 0xFF5EA1: return 3;
    case 0xFF08F7: return 4;
    case 0xFF1CE3: return 5;
    case 0xFF5AA5: return 6;
    case 0xFF42BD: return 7;
    case 0xFF52AD: return 8;
    case 0xFF4AB5: return 9;
  }
  
  // IRremote 4.x decodedRawData format may be different (32-bit with address)
  // Extract just the command byte (lower 8 bits after inverting)
  byte cmdByte = (code >> 16) & 0xFF;
  switch (cmdByte) {
    case 0x16: return 0;
    case 0x0C: return 1;
    case 0x18: return 2;
    case 0x5E: return 3;
    case 0x08: return 4;
    case 0x1C: return 5;
    case 0x5A: return 6;
    case 0x42: return 7;
    case 0x52: return 8;
    case 0x4A: return 9;
  }
  
  return 255;  // Invalid code
}
