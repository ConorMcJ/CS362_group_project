/*
 * Player 2 Device - Simon Says Multiplayer Game
 * 
 * This device responds to commands from the Simon device and collects
 * player input for various mini-games:
 * - LED Game: Button presses for pattern matching
 * - Buzzer Game: Joystick input for tone sequence matching
 * - Ultrasonic Game: Distance measurement (10-100cm range)
 * - Recall Game: IR remote input for number recall
 * 
 * Hardware:
 * - Arduino Uno R4
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

// ============= CONFIGURATION =============
#include "config.h"  // Contains PLAYER_ID (1, 2, or 3)

// ============= PIN DEFINITIONS =============
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

// ============= CONSTANTS =============
const byte MAX_PATTERN_LEN = 50;  // Support for higher levels with chunked transmission
const unsigned long DEBOUNCE_DELAY = 50;      // 50ms button debounce
const unsigned long JOYSTICK_DEBOUNCE = 300;  // 300ms joystick debounce
const unsigned long RECALL_TIMEOUT = 10000;   // 10 seconds for recall input

// ============= STATE MACHINE =============
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

// ============= COMMUNICATION =============
SoftwareSerial mySerial(RX, TX);

// ============= INPUT STORAGE =============
byte ledPattern[MAX_PATTERN_LEN];
byte ledPatternIndex = 0;

byte buzzerPattern[MAX_PATTERN_LEN];
byte buzzerPatternIndex = 0;

int capturedDistance = 0;

byte recallDigits[3];
byte recallIndex = 0;

// ============= TIMING =============
unsigned long inputStartTime = 0;
unsigned long lastButton1Press = 0;
unsigned long lastButton2Press = 0;
unsigned long lastButton3Press = 0;
unsigned long lastButton4Press = 0;
unsigned long lastJoystickInput = 0;

// ============= IR REMOTE =============
IRrecv irrecv(IR_PIN);
decode_results results;

// ============= FORWARD DECLARATIONS =============
void checkForCommands();
void processCommand(byte* cmd, byte len);
void handleLEDInputState();
void handleBuzzerInputState();
void handleUSCaptureState();
void handleRecallInputState();
int measureDistance();
byte decodeIRDigit(unsigned long code);
void sendDataToSimon();
void sendChunkedPattern(byte* pattern, byte length);

// ============= SETUP =============
void setup() {
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
  
  // IR receiver
  irrecv.enableIRIn();
  
  // Seed random (if needed for future use)
  randomSeed(analogRead(A1));
}

// ============= MAIN LOOP =============
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

// ============= COMMUNICATION =============
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
  
  switch (command) {
    case 'W':  // Wait - return to idle
      currentState = WAITING;
      // Clear any stored data
      ledPatternIndex = 0;
      buzzerPatternIndex = 0;
      recallIndex = 0;
      capturedDistance = 0;
      lastGameType = WAITING;
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
            break;
            
          case 'B':  // Buzzer game
            currentState = BUZZER_INPUT;
            lastGameType = BUZZER_INPUT;
            buzzerPatternIndex = 0;
            inputStartTime = millis();
            break;
            
          case 'U':  // Ultrasonic game
            currentState = US_CAPTURE;
            lastGameType = US_CAPTURE;
            capturedDistance = 0;
            break;
            
          case 'R':  // Recall game
            currentState = RECALL_INPUT;
            lastGameType = RECALL_INPUT;
            recallIndex = 0;
            inputStartTime = millis();
            break;
        }
      }
      break;
      
    case 'R':  // Request data
      if (len > 1 && cmd[1] == ('0' + PLAYER_ID)) {
        // This is a request for our data - send it
        sendDataToSimon();
        currentState = WAITING;
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
      break;
  }
}

void sendDataToSimon() {
  mySerial.write('@');
  mySerial.write('0' + PLAYER_ID);
  
  // Determine which data to send based on last game played
  switch (lastGameType) {
    case LED_INPUT:
      sendChunkedPattern(ledPattern, ledPatternIndex);
      break;
      
    case BUZZER_INPUT:
      sendChunkedPattern(buzzerPattern, buzzerPatternIndex);
      break;
      
    case US_CAPTURE:
      // Send distance (up to 3 digits)
      {
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
      mySerial.write('0' + 3);
      for (byte i = 0; i < 3; i++) {
        mySerial.write('0' + recallDigits[i]);
      }
      break;
      
    default:
      // No data - send empty (length 0)
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

// ============= STATE HANDLERS =============

// LED Game - Record button presses
void handleLEDInputState() {
  // Check button 1
  if (digitalRead(BUTTON_1) == LOW && 
      (millis() - lastButton1Press > DEBOUNCE_DELAY)) {
    if (ledPatternIndex < MAX_PATTERN_LEN) {
      ledPattern[ledPatternIndex++] = 1;
    }
    lastButton1Press = millis();
  }
  
  // Check button 2
  if (digitalRead(BUTTON_2) == LOW && 
      (millis() - lastButton2Press > DEBOUNCE_DELAY)) {
    if (ledPatternIndex < MAX_PATTERN_LEN) {
      ledPattern[ledPatternIndex++] = 2;
    }
    lastButton2Press = millis();
  }
  
  // Check button 3
  if (digitalRead(BUTTON_3) == LOW && 
      (millis() - lastButton3Press > DEBOUNCE_DELAY)) {
    if (ledPatternIndex < MAX_PATTERN_LEN) {
      ledPattern[ledPatternIndex++] = 3;
    }
    lastButton3Press = millis();
  }
  
  // Check button 4
  if (digitalRead(BUTTON_4) == LOW && 
      (millis() - lastButton4Press > DEBOUNCE_DELAY)) {
    if (ledPatternIndex < MAX_PATTERN_LEN) {
      ledPattern[ledPatternIndex++] = 4;
    }
    lastButton4Press = millis();
  }
}

// Buzzer Game - Record joystick moves
void handleBuzzerInputState() {
  int yVal = analogRead(JOYSTICK_Y);
  
  if (millis() - lastJoystickInput > JOYSTICK_DEBOUNCE) {
    if (yVal < 300) {  // Joystick pushed down = LOW tone (0)
      if (buzzerPatternIndex < MAX_PATTERN_LEN) {
        buzzerPattern[buzzerPatternIndex++] = 0;
      }
      lastJoystickInput = millis();
    } else if (yVal > 700) {  // Joystick pushed up = HIGH tone (1)
      if (buzzerPatternIndex < MAX_PATTERN_LEN) {
        buzzerPattern[buzzerPatternIndex++] = 1;
      }
      lastJoystickInput = millis();
    }
  }
}

// Ultrasonic Game - Capture distance
void handleUSCaptureState() {
  capturedDistance = measureDistance();
  currentState = DATA_READY;
}

// Recall Game - Record IR remote digits
void handleRecallInputState() {
  // Check timeout
  if (millis() - inputStartTime > RECALL_TIMEOUT) {
    currentState = DATA_READY;
    return;
  }
  
  // Check for IR input
  if (irrecv.decode(&results)) {
    byte digit = decodeIRDigit(results.value);
    
    if (digit <= 9 && recallIndex < 3) {
      recallDigits[recallIndex++] = digit;
    }
    
    irrecv.resume();  // Receive next value
    
    // If 3 digits entered, done
    if (recallIndex >= 3) {
      currentState = DATA_READY;
    }
  }
}

// ============= HELPER FUNCTIONS =============

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
  // Use a test sketch to print received codes and update as needed
  switch (code) {
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
    default: return 255;  // Invalid code
  }
}
