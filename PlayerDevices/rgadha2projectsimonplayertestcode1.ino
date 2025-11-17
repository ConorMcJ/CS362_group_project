/*
    SIMON SAYS - PLAYER DEVICE (Single Player Test)
    Fixed LCD brightness issue
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD Display - Try different addresses if 0x27 doesn't work
// Common addresses: 0x27, 0x3F
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
    WAITING, 
    DISPLAYING_PATTERN, 
    GETTING_INPUT, 
    SENDING_RESULT, 
    ELIMINATED 
};

PlayerState currentState = WAITING;

// Test pattern for standalone testing
uint8_t testPattern[] = {0, 1, 2, 3, 0, 1}; // R, Y, G, B, R, Y
uint8_t patternLength = 4;
uint8_t playerInput[20];
uint8_t inputIndex = 0;
uint8_t displayIndex = 0;

// Timing variables
unsigned long previousMillis = 0;
unsigned long patternDisplayMillis = 0;
unsigned long buttonDebounceMillis = 0;
unsigned long feedbackTimer = 0;

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

// Tone frequencies for each color
const int tones[4] = {262, 330, 392, 523}; // C, E, G, C

// Button state tracking
bool lastButtonState[4] = {HIGH, HIGH, HIGH, HIGH};
bool buttonsEnabled = false;

void setup() {
    Serial.begin(9600);
    
    // Initialize I2C
    Wire.begin();
    
    // Initialize LCD with multiple attempts
    Serial.println("Initializing LCD...");
    
    lcd.init();
    lcd.backlight();  // Turn on backlight at FULL brightness
    
    // Try to set contrast if supported (some modules ignore this)
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("LCD Test");
    lcd.setCursor(0, 1);
    lcd.print("Adjusting...");
    
    delay(1000);
    
    // Clear and show ready message
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Player Ready");
    lcd.setCursor(0, 1);
    lcd.print("Press any button");
    
    Serial.println("LCD initialized");
    Serial.println("If LCD is dim:");
    Serial.println("1. Adjust blue potentiometer on back of LCD module (turn with screwdriver)");
    Serial.println("2. Try changing I2C address to 0x3F if 0x27 doesn't work");
    Serial.println("3. Check I2C wiring: SDA->A4, SCL->A5, VCC->5V, GND->GND");
    
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
    
    Serial.println("Player Device Ready");
    Serial.println("LEDs: R=6, Y=7, G=8, B=9");
    Serial.println("BTNs: R=5, Y=4, G=3, B=2");
    Serial.println("Buzzer: 10, POT: A0");
}

void loop() {
    unsigned long currentMillis = millis();
    
    // Handle feedback LED/tone auto-off
    if(feedbackActive && (currentMillis - feedbackTimer >= FEEDBACK_DURATION)) {
        turnOffAllLEDs();
        noTone(BUZZER);
        feedbackActive = false;
    }
    
    switch(currentState) {
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

void handleWaitingState(unsigned long currentMillis) {
    int buttons[] = {BTN_R, BTN_Y, BTN_G, BTN_B};
    
    for(int i = 0; i < 4; i++) {
        if(digitalRead(buttons[i]) == LOW) {
            if(currentMillis - buttonDebounceMillis > DEBOUNCE_TIME) {
                startTestGame();
                buttonDebounceMillis = currentMillis;
                break;
            }
        }
    }
}

void startTestGame() {
    Serial.println("Starting test game...");
    currentState = DISPLAYING_PATTERN;
    displayIndex = 0;
    ledOn = false;
    patternDisplayComplete = false;
    patternDisplayMillis = millis();
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Watch Pattern:");
    lcd.setCursor(0, 1);
    lcd.print("Round 1");
    
    // Generate a random pattern
    patternLength = random(3, 6);
    for(int i = 0; i < patternLength; i++) {
        testPattern[i] = random(0, 4);
    }
    
    Serial.print("Pattern length: ");
    Serial.println(patternLength);
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
            uint8_t color = testPattern[displayIndex];
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
            uint8_t color = testPattern[displayIndex];
            lightLED(color, false);
            noTone(BUZZER);
            ledOn = false;
            displayIndex++;
            patternDisplayMillis = currentMillis;
        }
    }
}

void handleGetInput(unsigned long currentMillis) {
    if(currentMillis - previousMillis > INPUT_TIMEOUT) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Timeout!");
        lcd.setCursor(0, 1);
        lcd.print("Too slow");
        playFailTone();
        currentState = ELIMINATED;
        return;
    }
    
    if(inputIndex >= patternLength) {
        checkPatternCorrectness();
        return;
    }
    
    int buttons[] = {BTN_R, BTN_Y, BTN_G, BTN_B};
    
    for(int btn = 0; btn < 4; btn++) {
        bool currentButtonState = digitalRead(buttons[btn]);
        
        if(lastButtonState[btn] == HIGH && currentButtonState == LOW) {
            if(currentMillis - buttonDebounceMillis > DEBOUNCE_TIME) {
                buttonDebounceMillis = currentMillis;
                
                if(buttonsEnabled && !feedbackActive) {
                    playerInput[inputIndex] = btn;
                    inputIndex++;
                    
                    lightLED(btn, true);
                    playTone(btn);
                    feedbackActive = true;
                    feedbackTimer = currentMillis;
                    
                    lcd.setCursor(0, 1);
                    lcd.print("Input ");
                    lcd.print(inputIndex);
                    lcd.print("/");
                    lcd.print(patternLength);
                    lcd.print("    ");
                    
                    previousMillis = currentMillis;
                    
                    Serial.print("Button pressed: ");
                    Serial.println(btn);
                }
            }
        }
        
        lastButtonState[btn] = currentButtonState;
    }
}

void checkPatternCorrectness() {
    bool correct = true;
    
    Serial.println("Checking pattern...");
    for(int i = 0; i < patternLength; i++) {
        Serial.print("Expected: ");
        Serial.print(testPattern[i]);
        Serial.print(" Got: ");
        Serial.println(playerInput[i]);
        
        if(playerInput[i] != testPattern[i]) {
            correct = false;
            break;
        }
    }
    
    if(correct) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Correct!");
        lcd.setCursor(0, 1);
        lcd.print("+1 Point");
        playSuccessTone();
        
        unsigned long waitStart = millis();
        while(millis() - waitStart < 2000) {
            // Non-blocking wait
        }
        
        startTestGame();
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Wrong!");
        lcd.setCursor(0, 1);
        lcd.print("Game Over");
        playFailTone();
        flashRedLEDs();
        currentState = ELIMINATED;
    }
}

void handleSendResult(unsigned long currentMillis) {
    // For future Simon communication
}

void handleEliminatedState(unsigned long currentMillis) {
    static unsigned long flashMillis = 0;
    static bool flashState = false;
    
    if(currentMillis - flashMillis > 500) {
        flashState = !flashState;
        digitalWrite(LED_R, flashState ? HIGH : LOW);
        flashMillis = currentMillis;
    }
    
    int buttons[] = {BTN_R, BTN_Y, BTN_G, BTN_B};
    for(int i = 0; i < 4; i++) {
        if(digitalRead(buttons[i]) == LOW) {
            if(currentMillis - buttonDebounceMillis > DEBOUNCE_TIME) {
                resetPlayer();
                buttonDebounceMillis = currentMillis;
                break;
            }
        }
    }
}

void resetPlayer() {
    currentState = WAITING;
    turnOffAllLEDs();
    noTone(BUZZER);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Player Ready");
    lcd.setCursor(0, 1);
    lcd.print("Press any button");
}

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