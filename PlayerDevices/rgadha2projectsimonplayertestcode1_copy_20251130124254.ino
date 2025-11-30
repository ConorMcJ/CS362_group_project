/*
    SIMON SAYS - PLAYER DEVICE (Networked Version)
    With Serial Communication Protocol
    Fixed LCD brightness issue
    
    PLAYER ID CONFIGURATION - SET THIS FOR EACH DEVICE
*/

#define PLAYER_ID 1  // SET TO 1, 2, 3, or 4 FOR EACH PLAYER DEVICE

/*
    Communication Protocol
    Simon -> Players: '$' [CMD] [DATA] '#'
    Player -> Simon: '@' [PLAYER_ID] [DATA...] '!'
    
    Commands from Simon:
    'W' - WAIT (all players idle)
    'S' - START_GAME [patternLength]
    'P' - SEND_PATTERN [color1][color2]...
    'R' - REQUEST_DATA [playerID]
    'E' - END_GAME
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

// Serial Communication
#define RX_PIN 11
#define TX_PIN 12
SoftwareSerial mySerial(RX_PIN, TX_PIN);

// LCD Display
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Player Hardware Pins
#define LED_R 6
#define LED_Y 7
#define LED_G 8
#define LED_B 9

#define BTN_R 5
#define BTN_Y 4
#define BTN_G 3
#define BTN_B 2

#define BUZZER 10
#define POT A0

// Game State
enum PlayerState { 
    BOOT,
    WAITING, 
    DISPLAYING_PATTERN, 
    GETTING_INPUT, 
    SENDING_RESULT, 
    ELIMINATED 
};

PlayerState currentState = BOOT;

// Pattern storage
const uint8_t MAXIMUM_PATTERN_LENGTH = 20;
uint8_t patternLength = 0;
byte playerInput[MAXIMUM_PATTERN_LENGTH];
uint8_t inputIndex = 0;
uint8_t displayIndex = 0;

// LEDGameState logic variables
bool gameInitiated = false;

// Timing variables
unsigned long previousMillis = 0;
unsigned long patternDisplayMillis = 0;
unsigned long buttonDebounceMillis = 0;
unsigned long feedbackTimer = 0;
unsigned long bootTimeout = 0;

// Display state
bool ledOn = false;
bool patternDisplayComplete = false;
bool feedbackActive = false;

// Constants
const unsigned long LED_DISPLAY_TIME = 800;
const unsigned long LED_PAUSE_TIME = 400;
const unsigned long DEBOUNCE_TIME = 50;
const unsigned long INPUT_TIMEOUT = 15000;
const unsigned long FEEDBACK_DURATION = 300;
const unsigned long BOOT_TIMEOUT = 10000;

// Tone frequencies for each color
const int tones[4] = {262, 330, 392, 523}; // C, E, G, C

// Button state tracking
bool lastButtonState[4] = {HIGH, HIGH, HIGH, HIGH};
bool buttonsEnabled = false;

// Communication buffer
byte msgBuffer[50];
byte msgIndex = 0;
bool inMessage = false;

void setup() {
    Serial.begin(9600);
    mySerial.begin(9600);
    
    // Initialize I2C
    Wire.begin();
    
    // Initialize LCD
    Serial.println("Initializing LCD...");
    
    lcd.init();
    lcd.backlight();
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Player ");
    lcd.print(PLAYER_ID);
    lcd.setCursor(0, 1);
    lcd.print("Booting...");
    
    delay(1000);
    
    Serial.print("Player ");
    Serial.print(PLAYER_ID);
    Serial.println(" initialized");
    
    // Initialize LED pins
    pinMode(LED_R, OUTPUT);
    pinMode(LED_Y, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(LED_B, OUTPUT);
    
    // Initialize button pins with pull-up resistors
    pinMode(BTN_R, INPUT_PULLUP);
    pinMode(BTN_Y, INPUT_PULLUP);
    pinMode(BTN_G, INPUT_PULLUP);
    pinMode(BTN_B, INPUT_PULLUP);
    
    // Initialize buzzer
    pinMode(BUZZER, OUTPUT);
    noTone(BUZZER);
    
    // Initialize potentiometer
    pinMode(POT, INPUT);
    
    // Set initial states
    turnOffAllLEDs();
    
    // Send boot announcement
    sendBootMessage();
    bootTimeout = millis();
    
    Serial.println("Player Device Ready");
    Serial.println("Waiting for Simon commands...");
}

void loop() {
    unsigned long currentMillis = millis();
    
    // Always check for incoming commands
    checkForCommands();
    
    // Handle feedback LED/tone auto-off
    if(feedbackActive && (currentMillis - feedbackTimer >= FEEDBACK_DURATION)) {
        turnOffAllLEDs();
        noTone(BUZZER);
        feedbackActive = false;
    }
    
    switch(currentState) {
        case BOOT:
            handleBootState(currentMillis);
            break;
            
        case WAITING:
            handleWaitingState(currentMillis);
            break;
            
        case DISPLAYING_PATTERN:
            handleDisplayPattern(currentMillis);
            break;
            
        case GETTING_INPUT:
            handleGetInput(currentMillis);
            break;
            
        case SENDING_RESULT:
            handleSendResult(currentMillis);
            break;
            
        case ELIMINATED:
            handleEliminatedState(currentMillis);
            break;
    }
}

void handleBootState(unsigned long currentMillis) {
    // Flash LED to indicate boot state
    static unsigned long flashMillis = 0;
    static bool flashState = false;
    
    if(currentMillis - flashMillis > 500) {
        flashState = !flashState;
        digitalWrite(LED_G, flashState ? HIGH : LOW);
        flashMillis = currentMillis;
    }
    
    // Timeout after 10 seconds if no command received
    if(currentMillis - bootTimeout > BOOT_TIMEOUT) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("No Simon found");
        lcd.setCursor(0, 1);
        lcd.print("Check connection");
        turnOffAllLEDs();
        
        // Resend boot message
        sendBootMessage();
        bootTimeout = currentMillis;
    }
}

void handleWaitingState(unsigned long currentMillis) {
    if mySerial.available() {

    } 
    // Just wait for commands from Simon
    // No local button presses handled in waiting state
}

void handleDisplayPattern(unsigned long currentMillis) {
    if(displayIndex >= patternLength) {
        if(!patternDisplayComplete) {
            patternDisplayComplete = true;
            turnOffAllLEDs();
            noTone(BUZZER);
            
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Your Turn!");
            lcd.setCursor(0, 1);
            lcd.print("Repeat pattern");
            
            inputIndex = 0;
            buttonsEnabled = true;
            previousMillis = currentMillis;
            currentState = GETTING_INPUT;
        }
        return;
    }
    
    if(!ledOn) {
        if(currentMillis - patternDisplayMillis >= LED_PAUSE_TIME) {
            uint8_t color = receivedPattern[displayIndex];
            lightLED(color, true);
            playTone(color);
            ledOn = true;
            patternDisplayMillis = currentMillis;
            
            lcd.setCursor(0, 1);
            lcd.print("Step ");
            lcd.print(displayIndex + 1);
            lcd.print("/");
            lcd.print(patternLength);
            lcd.print("  ");
        }
    } else {
        if(currentMillis - patternDisplayMillis >= LED_DISPLAY_TIME) {
            uint8_t color = receivedPattern[displayIndex];
            lightLED(color, false);
            noTone(BUZZER);
            ledOn = false;
            displayIndex++;
            patternDisplayMillis = currentMillis;
        }
    }
}

void handleGetInput() {
    int buttons[] = {BTN_R, BTN_Y, BTN_G, BTN_B};
    
    for(int btn = 0; btn < 4; btn++) {
        bool currentButtonState = digitalRead(buttons[btn]);
        
        if(lastButtonState[btn] != currentButtonState) {
            lastButtonState[btn] = currentButtonState;
            if(millis() - buttonDebounceMillis > DEBOUNCE_TIME && currentButtonState == LOW) {
                buttonDebounceMillis = currentMillis;
                
                if (inputIndex < MAXIMUM_PATTERN_LENGTH) {
                    playerInput[inputIndex] = btn+1;
                    inputIndex++;
                }
                
                buttonDebounceMillis = millis();
                break;  // Only one button input read per debounce
                }
            }
        }
        
    }
}

void checkPatternCorrectness() {
    bool correct = true;
    
    Serial.println("Checking pattern...");
    for(int i = 0; i < patternLength; i++) {
        Serial.print("Expected: ");
        Serial.print(receivedPattern[i]);
        Serial.print(" Got: ");
        Serial.println(playerInput[i]);
        
        if(playerInput[i] != receivedPattern[i]) {
            correct = false;
            break;
        }
    }
    
    if(correct) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Correct!");
        lcd.setCursor(0, 1);
        lcd.print("Waiting...");
        playSuccessTone();
        
        sendResultToSimon(true);
        currentState = WAITING;
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Wrong!");
        lcd.setCursor(0, 1);
        lcd.print("Eliminated");
        playFailTone();
        flashRedLEDs();
        
        sendResultToSimon(false);
        currentState = ELIMINATED;
    }
}

void handleSendResult(unsigned long currentMillis) {
    // Wait for request from Simon
}

void handleEliminatedState(unsigned long currentMillis) {
    static unsigned long flashMillis = 0;
    static bool flashState = false;
    
    if(currentMillis - flashMillis > 500) {
        flashState = !flashState;
        digitalWrite(LED_R, flashState ? HIGH : LOW);
        flashMillis = currentMillis;
    }
}

// ===== COMMUNICATION FUNCTIONS =====

void sendBootMessage() {
    mySerial.write('@');
    mySerial.write('0' + PLAYER_ID);
    mySerial.write('B');  // Boot message
    mySerial.write('!');
    
    Serial.print("Sent boot message for Player ");
    Serial.println(PLAYER_ID);
}

void sendResultToSimon(bool correct) {
    mySerial.write('@');
    mySerial.write('0' + PLAYER_ID);
    mySerial.write(correct ? 'C' : 'F');  // C=Correct, F=Failed
    mySerial.write('!');
    
    Serial.print("Sent result: ");
    Serial.println(correct ? "Correct" : "Failed");
}

void checkForCommands() {
    while(mySerial.available() > 0) {
        char c = mySerial.read();
        
        if(c == '$') {
            inMessage = true;
            msgIndex = 0;
        } else if(c == '#' && inMessage) {
            processCommand(msgBuffer, msgIndex);
            inMessage = false;
        } else if(inMessage && msgIndex < 30) {
            msgBuffer[msgIndex++] = c;
        }
    }
}

void processCommand(byte* cmd, byte len) {
    if(len < 1) return;
    
    Serial.print("Received command: ");
    Serial.write(cmd[0]);
    Serial.println();
    
    switch(cmd[0]) {
        case 'W': // Wait
            currentState = WAITING;
            turnOffAllLEDs();
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Player ");
            lcd.print(PLAYER_ID);
            lcd.setCursor(0, 1);
            lcd.print("Ready...");
            Serial.println("State: WAITING");
            break;
            
        case 'S': // Start game - receive pattern
            currentState = 
            if(len > 1) {
                char gameMode = cmd[1];
                switch (gameMode) {
                    case 'L':
                    handleLEDGameState();
                    break;
                }
            }
            break;
            
        case 'R': // Data request
            if(len > 1 && cmd[1] == ('0' + PLAYER_ID)) {
                sendResultToSimon(true);  // Resend last result
            }
            break;
            
        case 'E': // End game
            resetPlayer();
            break;
    }
}

void handleLEDGameState() {
    if (!gameInitiated) {
        for (int i = 0; i < MAXIMUM_PATTERN_LENGTH; i++) {
            playerInput[i] = 0;
        }
        inputIndex = 0;
        gameInitiated = True;
        return;
    }


}

void resetPlayer() {
    currentState = WAITING;
    turnOffAllLEDs();
    noTone(BUZZER);
    buttonsEnabled = false;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Player ");
    lcd.print(PLAYER_ID);
    lcd.setCursor(0, 1);
    lcd.print("Game Over");
}

// ===== LED CONTROL FUNCTIONS =====

void lightLED(uint8_t color, bool state) {
    switch(color) {
        case 0:
            digitalWrite(LED_R, state ? HIGH : LOW);
            break;
        case 1:
            digitalWrite(LED_Y, state ? HIGH : LOW);
            break;
        case 2:
            digitalWrite(LED_G, state ? HIGH : LOW);
            break;
        case 3:
            digitalWrite(LED_B, state ? HIGH : LOW);
            break;
    }
}

void turnOffAllLEDs() {
    digitalWrite(LED_R, LOW);
    digitalWrite(LED_Y, LOW);
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_B, LOW);
}

// ===== AUDIO FUNCTIONS =====

void playTone(uint8_t color) {
    if(color < 4) {
        int potVal = analogRead(POT);
        int pitchOffset = map(potVal, 0, 1023, -50, 50);
        int adjustedTone = tones[color] + pitchOffset;
        tone(BUZZER, adjustedTone);
    }
}

void playSuccessTone() {
    tone(BUZZER, 523, 200);
    unsigned long toneStart = millis();
    while(millis() - toneStart < 250) {}
    
    tone(BUZZER, 659, 200);
    toneStart = millis();
    while(millis() - toneStart < 250) {}
    
    tone(BUZZER, 784, 400);
}

void playFailTone() {
    tone(BUZZER, 392, 300);
    unsigned long toneStart = millis();
    while(millis() - toneStart < 350) {}
    
    tone(BUZZER, 330, 300);
    toneStart = millis();
    while(millis() - toneStart < 350) {}
    
    tone(BUZZER, 262, 500);
}

void flashRedLEDs() {
    for(int i = 0; i < 6; i++) {
        turnOffAllLEDs();
        digitalWrite(LED_R, HIGH);
        unsigned long flashStart = millis();
        while(millis() - flashStart < 200) {}
        
        turnOffAllLEDs();
        flashStart = millis();
        while(millis() - flashStart < 200) {}
    }
}