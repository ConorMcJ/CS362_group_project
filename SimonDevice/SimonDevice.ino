// CS 362 (42222) - Fall 2025
// Group 21
// Final Project - Simon Says Multiplayer Game
//
// Abdur Raheem Faruki | (afaru)   | afaru@uic.edu
// Conor McJannett     | (cmcja2)  | cmcja2@uic.edu
// Tanmay Patel        | (tpate)   | tpate@uic.edu
// Rahul Gadhavi       | (rgadha2) | rgadha2@uic.edu
//
//
// Simon Device
/*
-------------------------------------------------------------------------------

  Description:

    Code for the Simon Device for the Simon Says Multiplayer Game. This device
    handles the gameplay loop logic and contains the settings menu, signals to
    player devices, receivers for player device output, and output devices for
    signaling gameplay patterns to users.


  IMPORTANT SYNTAX:
    - "session" = the set of all events which occur between the device states
      MENU and GAME_OVER

    - "game" = a played instance of one of the four types of gamemodes
      (LED_GAME, BUZ_GAME, US_GAME, or RECALL_GAME)

    - "round" = one full playthrough of all four gamemodes in a row
    
    - "level" = an enumerated round of the current session, represented in
      memory with a global stack variable `unsigned level`

  On initialization:

  The Simon device goes through different states:

    - MENU: Menu navigation handlers are active, settings may be modified with
      joystick input and LCD displays menu. All player devices are to be in
      WAITING state until further notice. The global variable currentSession
      is reset to starting values, such as currentSession.level = 1.
  
    - WAITING_START: In-between rounds, device switches to this mode to display
      what game is next on the LCD. During this mode, the Simon device state
      can be set to PAUSED by pressing the pushbutton next to the joystick.
      Otherwise, after a 3 second countdown, it continues onto LED_GAME.
  
    - SEND_MEMORY_NUM: At the start of each round, the Simon LCD displays a
      random 3-digit number that the players must be able to recall at the end
      of the round. After a few seconds, switch to LED_GAME.
  
    - LED_GAME: The LED blink pattern memory gamemode. The Simon device
      generates a random LED blink pattern the length of the current level + 2,
      then outputs the pattern to its LEDs for the players to memorize, and
      then signals to the player devices to all begin recording user input on
      the pushbuttons. It immediately then starts a timer with some length
      directly linearly proportional to the current level. When the timer is up
      it switches to PROCESS_RESULTS.
  
    - BUZ_GAME: The high-low sound frequency pattern memory gamemode. The Simon
      device generates a random high-low sound frequency pattern the length of
      the current level + 2, then outputs the pattern to its buzzer for the
      players to memorize, and then signals to the player devices to all begin
      recording user input on the joystick. It immediately then starts a timer
      with some length directly linearly proportional to the current level.
      When the timer is up, it switches to PROCESS_RESULTS.
  
    - US_GAME: The distance guessing gamemode. The Simon device generates a
      random distance between 10cm and 100cm, and prints the distance to the
      LCD for the players to read. It then starts a 5-second countdown, and at
      the end of the countdown it signals to the player devices to save the
      current distance recorded by their respective ultrasonic sensors. Then
      the Simon device switches to PROCESS_RESULTS.
  
    - RECALL_GAME: The number recall gamemode. The Simon device LCD prompts
      the users to enter the 3-digit number that was displayed at the start of
      the game, and signals the player devices to begin recording user input on
      the infrared remote controllers. After 10 seconds, the simon device
      switches to PROCESS_RESULTS.
  
    - PROCESS_RESULTS: The state for receiving and handling user input. Each
      remaining player in the session is signaled to return its recorded user
      input data from the last game. Then the input data is compared to the 
  
    - GAME_OVER: In this state, different information about the final states
      of the players, including final scores, level and game of each player's
      elimination or victory, etc. This gamemode can be exited by pressing the
      push button on the simon device, at which point the current session
      officially ends, and a new session begins starting in MENU state.
  
    - PAUSED: Displays a menu on the simon device, giving options to return
      to MENU or continue game from where it left off. Returning to MENU resets
      the current session and loses all game progress.
  
*/
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>

// Pin definitions
#define TX 7
#define RX_PLAYER1 12
#define RX_PLAYER2 11
#define RX_PLAYER3 9
#define BUZZER 10
#define PUSH_BUTTON 8
#define JOYSTICK_X A2
#define JOYSTICK_Y A3
#define LED_1 2
#define LED_2 3
#define LED_3 4
#define LED_4 5

// Simon device state enumeration
enum SimonState {
  MENU,
  SETTINGS_MENU,
  DEBUG_SELECT,
  DEBUG_SHOW_MEMORY,
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

// Session state structure
struct Session {
  SimonState prevState;
  SimonState currentState;
  unsigned short level;
  unsigned short numPlayers;
};

Session currentSession = { MENU, MENU, 1, 3 };

// Debug mode tracking
bool debugModeActive = false;
SimonState debugSelectedGame = LED_GAME;

// --- MENU MUSIC ---
// Note frequencies (Hz)
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_REST 0

// Simple catchy melody (Simon Says theme)
const int menuMelody[] = {
  NOTE_E4, NOTE_G4, NOTE_C5, NOTE_B4, NOTE_A4, NOTE_G4,
  NOTE_E4, NOTE_G4, NOTE_A4, NOTE_G4, NOTE_E4, NOTE_D4,
  NOTE_E4, NOTE_G4, NOTE_C5, NOTE_B4, NOTE_A4, NOTE_G4,
  NOTE_A4, NOTE_G4, NOTE_E4, NOTE_D4, NOTE_C4, NOTE_REST
};
const int menuNoteDurations[] = {
  200, 200, 400, 200, 200, 400,
  200, 200, 200, 200, 200, 400,
  200, 200, 400, 200, 200, 400,
  200, 200, 200, 200, 400, 800
};
const byte MENU_MELODY_LENGTH = 24;

// Menu music state
byte currentMenuNote = 0;
unsigned long lastNoteTime = 0;
bool menuMusicPlaying = false;

// State tracking variables
unsigned long stateStartTime = 0;
unsigned long lastButtonCheck = 0;
unsigned long countdownStartTime = 0;

// Game data
byte currentPattern[50];  // Max pattern length 50
byte patternLength = 0;
int memoryNumber = 0;
int targetDistance = 0;

// Level constraint (18 is already far beyond what anyone should reach)
const byte MAX_LEVEL = 18;

// Player tracking
struct PlayerData {
  bool isActive;
  byte inputData[50];
  byte inputLength;
  int score;
};

PlayerData players[3];

// Button debouncing logic variables
const unsigned long DEBOUNCE_DELAY = 50;
byte buttonState = HIGH;
byte lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

// Single TX serial for broadcasting to all players
// RX pin is unused for this serial, using pin A5 as dummy (not connected)
SoftwareSerial txSerial(A5, TX);  // TX on pin 7, dummy RX on A5

// Separate RX-only serials for each player
SoftwareSerial rx1Serial(RX_PLAYER1, TX);  // Player 1 RX on pin 12
SoftwareSerial rx2Serial(RX_PLAYER2, TX);  // Player 2 RX on pin 11  
SoftwareSerial rx3Serial(RX_PLAYER3, TX);  // Player 3 RX on pin 9

// I2C LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Forward declarations
void handleMenuState();
void handleSettingsMenuState();
void playMenuMusic();
void stopMenuMusic();
void handleDebugSelectState();
void handleWaitingStartState();
void handleSendMemoryNumState();
void handleLEDGameState();
void handleBuzGameState();
void handleUSGameState();
void handleRecallGameState();
void handleProcessResultsState();
void handleGameOverState();
void handlePausedState();
void sendCommand(char cmd, byte data = 0);
void requestPlayerData(byte playerID);
bool receivePlayerData(byte playerID, byte* buffer, byte maxLen, unsigned long timeout);
bool readButtonDebounced();
void turnOnLED(byte ledNum);
void turnOffAllLEDs();
void parsePlayerData(byte playerIndex, byte* buffer);
void evaluateResults();
bool checkPattern(byte playerIndex);
bool checkDistance(byte playerIndex);
bool checkRecallNumber(byte playerIndex);
byte countActivePlayers();
SimonState getNextGameState();
byte findWinner();
byte findAllWinners(byte* winnerArray);
void resetPlayerScores();

void setup() {
  // Define TX and RX pin modes for SoftwareSerial
  pinMode(TX, OUTPUT);
  pinMode(RX_PLAYER1, INPUT);
  pinMode(RX_PLAYER2, INPUT);
  pinMode(RX_PLAYER3, INPUT);

  // Button and LED pins
  pinMode(PUSH_BUTTON, INPUT_PULLUP);
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(LED_3, OUTPUT);
  pinMode(LED_4, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // Turn off all LEDs initially
  digitalWrite(LED_1, LOW);
  digitalWrite(LED_2, LOW);
  digitalWrite(LED_3, LOW);
  digitalWrite(LED_4, LOW);

  // Initialize TX serial for broadcasting
  txSerial.begin(9600);
  
  // Initialize RX serials for receiving
  rx1Serial.begin(9600);
  rx2Serial.begin(9600);
  rx3Serial.begin(9600);
  
  // Start listening on player 1 by default
  rx1Serial.listen();

  // LCD init
  lcd.init();
  lcd.backlight();

  // Seed random number generator
  randomSeed(analogRead(A0));

  // Initialize session
  currentSession.currentState = MENU;
  currentSession.prevState = MENU;
  currentSession.level = 1;
  currentSession.numPlayers = 3;
}

void loop() {
  // Handle current state
  switch (currentSession.currentState) {
    case MENU:
      handleMenuState();
      break;

    case SETTINGS_MENU:
      handleSettingsMenuState();
      break;

    case DEBUG_SELECT:
      handleDebugSelectState();
      break;

    case DEBUG_SHOW_MEMORY:
      handleDebugShowMemoryState();
      break;

    case WAITING_START:
      handleWaitingStartState();
      break;

    case SEND_MEMORY_NUM:
      handleSendMemoryNumState();
      break;

    case LED_GAME:
      handleLEDGameState();
      break;

    case BUZ_GAME:
      handleBuzGameState();
      break;

    case US_GAME:
      handleUSGameState();
      break;

    case RECALL_GAME:
      handleRecallGameState();
      break;

    case PROCESS_RESULTS:
      handleProcessResultsState();
      break;

    case GAME_OVER:
      handleGameOverState();
      break;

    case PAUSED:
      handlePausedState();
      break;

    default:
      // Error state - return to menu
      currentSession.currentState = MENU;
      break;
  }
}

/*
  STATE HANDLERS
*/

void handleMenuState() {
  static bool menuInitialized = false;
  static byte selectedOption = 0;        // 0=Start Game, 1=Settings
  static byte lastSelectedOption = 255;  // Track last displayed option
  static unsigned long lastJoystickRead = 0;
  const unsigned long JOYSTICK_DEBOUNCE = 50;  // 200ms debounce for joystick

  if (!menuInitialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Simon Says Game");
    lcd.setCursor(0, 1);
    lcd.print(">Start  Settings");

    // Initialize all players to WAITING
    sendCommand('W');

    // Reset session
    currentSession.level = 1;
    currentSession.numPlayers = 3;
    for (byte i = 0; i < 3; i++) {
      players[i].isActive = true;
      players[i].score = 0;
      players[i].inputLength = 0;
    }

    // Start menu music
    currentMenuNote = 0;
    lastNoteTime = millis();
    menuMusicPlaying = true;

    menuInitialized = true;
    lastSelectedOption = 0;
    stateStartTime = millis();
  }

  // Play menu music (non-blocking)
  playMenuMusic();

  // Read joystick for menu navigation with debouncing
  if (millis() - lastJoystickRead > JOYSTICK_DEBOUNCE) {
    int xVal = analogRead(JOYSTICK_X);

    // Left/Right to change selection
    if (xVal < 300 && selectedOption != 0) {
      selectedOption = 0;
      lastJoystickRead = millis();
    } else if (xVal > 700 && selectedOption != 1) {
      selectedOption = 1;
      lastJoystickRead = millis();
    }
  }

  // Only update LCD if selection changed
  if (selectedOption != lastSelectedOption) {
    lcd.setCursor(0, 1);
    if (selectedOption == 0) {
      lcd.print(">Start  Settings");
    } else {
      lcd.print(" Start >Settings");
    }
    lastSelectedOption = selectedOption;
  }

  // Check button press with debouncing
  if (readButtonDebounced()) {
    stopMenuMusic();  // Stop music when leaving menu
    if (selectedOption == 0) {
      // Start game
      debugModeActive = false;  // Normal game mode
      currentSession.prevState = MENU;
      currentSession.currentState = WAITING_START;
      menuInitialized = false;
      lastSelectedOption = 255;
      stateStartTime = millis();
    } else {
      // Settings menu
      currentSession.prevState = MENU;
      currentSession.currentState = SETTINGS_MENU;
      menuInitialized = false;
      lastSelectedOption = 255;
      stateStartTime = millis();
    }
  }
}

// --- MENU MUSIC FUNCTIONS ---
void playMenuMusic() {
  if (!menuMusicPlaying) return;

  unsigned long currentTime = millis();
  
  // Check if it's time for the next note
  if (currentTime - lastNoteTime >= (unsigned long)menuNoteDurations[currentMenuNote]) {
    // Move to next note
    currentMenuNote++;
    
    // Loop the melody
    if (currentMenuNote >= MENU_MELODY_LENGTH) {
      currentMenuNote = 0;
    }
    
    // Play the note (or rest)
    if (menuMelody[currentMenuNote] == NOTE_REST) {
      noTone(BUZZER);
    } else {
      tone(BUZZER, menuMelody[currentMenuNote]);
    }
    
    lastNoteTime = currentTime;
  }
}

void stopMenuMusic() {
  menuMusicPlaying = false;
  noTone(BUZZER);
}

void handleSettingsMenuState() {
  static bool initialized = false;
  static byte selectedOption = 0;        // 0=Exit, 1=Debug
  static byte lastSelectedOption = 255;
  static unsigned long lastJoystickRead = 0;
  const unsigned long JOYSTICK_DEBOUNCE = 50;

  if (!initialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Settings Menu");
    lcd.setCursor(0, 1);
    lcd.print(">Exit     Debug");
    
    initialized = true;
    selectedOption = 0;
    lastSelectedOption = 0;
  }

  // Read joystick for menu navigation
  if (millis() - lastJoystickRead > JOYSTICK_DEBOUNCE) {
    int xVal = analogRead(JOYSTICK_X);

    if (xVal < 300 && selectedOption != 0) {
      selectedOption = 0;
      lastJoystickRead = millis();
    } else if (xVal > 
     && selectedOption != 1) {
      selectedOption = 1;
      lastJoystickRead = millis();
    }
  }

  // Update LCD if selection changed
  if (selectedOption != lastSelectedOption) {
    lcd.setCursor(0, 1);
    if (selectedOption == 0) {
      lcd.print(">Exit     Debug ");
    } else {
      lcd.print(" Exit    >Debug ");
    }
    lastSelectedOption = selectedOption;
  }

  // Check button press
  if (readButtonDebounced()) {
    if (selectedOption == 0) {
      // Exit - return to main menu, reset scores
      debugModeActive = false;
      resetPlayerScores();
      currentSession.prevState = SETTINGS_MENU;
      currentSession.currentState = MENU;
      initialized = false;
      lastSelectedOption = 255;
    } else {
      // Debug mode - go to game selector
      debugModeActive = true;
      // Reset scores when first entering debug mode
      resetPlayerScores();
      // Initialize all players as active
      for (byte i = 0; i < 3; i++) {
        players[i].isActive = true;
        players[i].inputLength = 0;
      }
      currentSession.level = 1;
      // Tell players to wait
      sendCommand('W');
      currentSession.prevState = SETTINGS_MENU;
      currentSession.currentState = DEBUG_SELECT;
      initialized = false;
      lastSelectedOption = 255;
    }
  }
}

void handleDebugSelectState() {
  static bool initialized = false;
  static byte selectedGame = 0;  // 0=LED, 1=BUZ, 2=US, 3=RECALL
  static byte lastSelectedGame = 255;
  static unsigned long lastJoystickRead = 0;
  const unsigned long JOYSTICK_DEBOUNCE = 50;

  if (!initialized) {
    lcd.clear();
    // Display: LED  BUZ
    //          US   RCL
    lcd.setCursor(0, 0);
    lcd.print(">LED        BUZ");
    lcd.setCursor(0, 1);
    lcd.print(" US         RCL");
    
    // Initialize players for debug round
    for (byte i = 0; i < 3; i++) {
      players[i].isActive = true;
      players[i].inputLength = 0;
    }
    // Note: scores preserved from previous debug games
    
    initialized = true;
    selectedGame = 0;
    lastSelectedGame = 255;
  }

  // Read joystick for game selection
  if (millis() - lastJoystickRead > JOYSTICK_DEBOUNCE) {
    int xVal = analogRead(JOYSTICK_X);
    int yVal = analogRead(JOYSTICK_Y);

    byte newSelection = selectedGame;

    // Left/Right movement
    if (xVal < 300) {
      // Left
      if (selectedGame == 1) newSelection = 0;      // BUZ -> LED
      else if (selectedGame == 3) newSelection = 2; // RCL -> US
    } else if (xVal > 700) {
      // Right
      if (selectedGame == 0) newSelection = 1;      // LED -> BUZ
      else if (selectedGame == 2) newSelection = 3; // US -> RCL
    }
    
    // Up/Down movement
    if (yVal < 300) {
      // Up
      if (selectedGame == 2) newSelection = 0;      // US -> LED
      else if (selectedGame == 3) newSelection = 1; // RCL -> BUZ
    } else if (yVal > 700) {
      // Down
      if (selectedGame == 0) newSelection = 2;      // LED -> US
      else if (selectedGame == 1) newSelection = 3; // BUZ -> RCL
    }

    if (newSelection != selectedGame) {
      selectedGame = newSelection;
      lastJoystickRead = millis();
    }
  }

  // Update LCD if selection changed
  if (selectedGame != lastSelectedGame) {
    lcd.setCursor(0, 0);
    lcd.print(selectedGame == 0 ? ">LED" : " LED");
    lcd.setCursor(12, 0);
    lcd.print(selectedGame == 1 ? ">BUZ" : " BUZ");
    lcd.setCursor(0, 1);
    lcd.print(selectedGame == 2 ? ">US " : " US ");
    lcd.setCursor(12, 1);
    lcd.print(selectedGame == 3 ? ">RCL" : " RCL");
    lastSelectedGame = selectedGame;
  }

  // Check button press
  if (readButtonDebounced()) {
    // Store the selected game for returning after PROCESS_RESULTS
    debugSelectedGame = (selectedGame == 0) ? LED_GAME : 
                        (selectedGame == 1) ? BUZ_GAME : 
                        (selectedGame == 2) ? US_GAME : RECALL_GAME;
    
    // For RECALL_GAME, we need to show the memory number first
    if (debugSelectedGame == RECALL_GAME) {
      // Generate memory number and go to show state
      memoryNumber = random(100, 1000);
      currentSession.prevState = DEBUG_SELECT;
      currentSession.currentState = DEBUG_SHOW_MEMORY;
      initialized = false;
      lastSelectedGame = 255;
      stateStartTime = millis();
    } else {
      // Go directly to the selected game
      currentSession.prevState = DEBUG_SELECT;
      currentSession.currentState = debugSelectedGame;
      initialized = false;
      lastSelectedGame = 255;
      stateStartTime = millis();
    }
  }
}

// Handle debug show memory state (non-blocking display before RECALL_GAME)
void handleDebugShowMemoryState() {
  static bool initialized = false;
  const unsigned long DISPLAY_DURATION = 3000;

  if (!initialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Remember:");
    lcd.setCursor(0, 1);
    lcd.print("   ");
    lcd.print(memoryNumber);
    initialized = true;
    stateStartTime = millis();
  }

  // After 3 seconds, go to RECALL_GAME
  if (millis() - stateStartTime >= DISPLAY_DURATION) {
    currentSession.prevState = DEBUG_SHOW_MEMORY;
    currentSession.currentState = RECALL_GAME;
    initialized = false;
    stateStartTime = millis();
  }
}

// Helper function to reset player scores
void resetPlayerScores() {
  for (byte i = 0; i < 3; i++) {
    players[i].score = 0;
  }
}

void handleWaitingStartState() {
  static bool initialized = false;
  const unsigned long COUNTDOWN_DURATION = 3000;

  if (!initialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Level ");
    lcd.print(currentSession.level);
    lcd.setCursor(0, 1);
    lcd.print("Starting in 3...");
    initialized = true;
    stateStartTime = millis();
  }

  unsigned long elapsed = millis() - stateStartTime;

  // Update countdown
  byte secondsLeft = 3 - (elapsed / 1000);
  if (secondsLeft <= 3) {
    lcd.setCursor(12, 1);
    lcd.print(secondsLeft);
  }

  // Check for pause
  if (readButtonDebounced()) {
    currentSession.prevState = WAITING_START;
    currentSession.currentState = PAUSED;
    initialized = false;
    return;
  }

  // Transition
  if (elapsed >= COUNTDOWN_DURATION) {
    currentSession.prevState = WAITING_START;
    currentSession.currentState = SEND_MEMORY_NUM;
    initialized = false;
    stateStartTime = millis();
  }
}

void handleSendMemoryNumState() {
  static bool initialized = false;
  const unsigned long DISPLAY_DURATION = 5000;

  if (!initialized) {
    // Generate random 3-digit number
    memoryNumber = random(100, 1000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Remember:");
    lcd.setCursor(0, 1);
    lcd.print("   ");
    lcd.print(memoryNumber);

    initialized = true;
    stateStartTime = millis();
  }

  unsigned long elapsed = millis() - stateStartTime;

  // Transition after display time
  if (elapsed >= DISPLAY_DURATION) {
    currentSession.prevState = SEND_MEMORY_NUM;
    currentSession.currentState = LED_GAME;
    initialized = false;
    stateStartTime = millis();
  }
}

void handleLEDGameState() {
  static String fmt_text = "";
  static bool initialized = false;
  static bool patternShown = false;
  static byte currentLED = 0;
  static unsigned long lastLEDChange = 0;
  const unsigned long LED_ON_TIME = 500;
  const unsigned long LED_OFF_TIME = 300;

  if (!initialized) {
    // Generate random pattern (level + 2 length)
    patternLength = currentSession.level + 2;
    for (byte i = 0; i < patternLength; i++) {
      currentPattern[i] = random(1, 5);  // LEDs 1-4
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("LED Pattern Game");
    lcd.setCursor(0, 1);
    lcd.print("Watch & Remember");

    // Turn off all LEDs
    turnOffAllLEDs();

    initialized = true;
    currentLED = 0;
    stateStartTime = millis();
    lastLEDChange = millis();
  }

  // Show pattern
  if (!patternShown) {
    unsigned long elapsed = millis() - lastLEDChange;

    if (currentLED < patternLength) {
      // Blink current LED
      if (elapsed < LED_ON_TIME) {
        // Turn on appropriate LED
        turnOnLED(currentPattern[currentLED]);
      } else if (elapsed < LED_ON_TIME + LED_OFF_TIME) {
        // Turn off all LEDs
        turnOffAllLEDs();
      } else {
        // Move to next LED
        currentLED++;
        lastLEDChange = millis();
      }
    } else {
      // Pattern complete
      patternShown = true;

      // Signal players to start recording
      sendCommand('S', 'L');  // Start LED game
      lcd.setCursor(0, 0);
      lcd.print("      Go!       ");

      stateStartTime = millis();
      lcd.setCursor(0, 1);
    }
  } else {
    // Wait for players to input (time proportional to level)
    unsigned long inputTime = 5000 + (currentSession.level * 1000);
    unsigned long elapsed = millis() - stateStartTime;

    lcd.print("");

    if (elapsed >= inputTime) {
      // Time's up, process results
      currentSession.prevState = LED_GAME;
      currentSession.currentState = PROCESS_RESULTS;
      initialized = false;
      patternShown = false;
      stateStartTime = millis();
    }
  }
}

void handleBuzGameState() {
  static bool initialized = false;
  static bool patternShown = false;
  static byte currentTone = 0;
  static unsigned long lastToneChange = 0;
  const unsigned long TONE_DURATION = 600;
  const unsigned long TONE_GAP = 200;
  const int HIGH_FREQ = 1000;
  const int LOW_FREQ = 400;

  if (!initialized) {
    // Generate random high/low pattern
    patternLength = currentSession.level + 2;
    for (byte i = 0; i < patternLength; i++) {
      currentPattern[i] = random(0, 2);  // 0=LOW, 1=HIGH
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sound Game");
    lcd.setCursor(0, 1);
    lcd.print("Listen Carefully");

    initialized = true;
    currentTone = 0;
    lastToneChange = millis();
  }

  // Show pattern
  if (!patternShown) {
    unsigned long elapsed = millis() - lastToneChange;

    if (currentTone < patternLength) {
      if (elapsed < TONE_DURATION) {
        // Play tone
        int freq = (currentPattern[currentTone] == 1) ? HIGH_FREQ : LOW_FREQ;
        tone(BUZZER, freq);
      } else if (elapsed < TONE_DURATION + TONE_GAP) {
        // Silence
        noTone(BUZZER);
      } else {
        // Move to next tone
        currentTone++;
        lastToneChange = millis();
      }
    } else {
      // Pattern complete
      noTone(BUZZER);
      patternShown = true;
      sendCommand('S', 'B');  // Start buzzer game
      stateStartTime = millis();
    }
  } else {
    // Wait for player input
    unsigned long inputTime = 5000 + (currentSession.level * 1000);
    unsigned long elapsed = millis() - stateStartTime;

    if (elapsed >= inputTime) {
      currentSession.prevState = BUZ_GAME;
      currentSession.currentState = PROCESS_RESULTS;
      initialized = false;
      patternShown = false;
    }
  }
}

void handleUSGameState() {
  static bool initialized = false;
  static bool countdownStarted = false;
  static unsigned long countdownStart = 0;
  const unsigned long COUNTDOWN_DURATION = 5000;

  if (!initialized) {
    // Generate random target distance (10-100 cm)
    targetDistance = random(10, 101);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Target: ");
    lcd.print(targetDistance);
    lcd.print(" cm");
    lcd.setCursor(0, 1);
    lcd.print("Get ready...");

    initialized = true;
    countdownStarted = false;
    stateStartTime = millis();
  }

  // Start countdown after 2 seconds
  if (!countdownStarted && (millis() - stateStartTime >= 2000)) {
    countdownStarted = true;
    countdownStart = millis();
    lcd.setCursor(0, 1);
    lcd.print("Starting in 5...");
  }

  if (countdownStarted) {
    unsigned long elapsed = millis() - countdownStart;
    byte secondsLeft = 5 - (elapsed / 1000);

    // Update countdown display
    if (elapsed % 1000 < 50) {
      lcd.setCursor(12, 1);
      lcd.print(secondsLeft);
    }

    // Time's up
    if (elapsed >= COUNTDOWN_DURATION) {
      // Signal players to capture distance
      sendCommand('S', 'U');

      // Small delay for players to capture
      delay(100);

      currentSession.prevState = US_GAME;
      currentSession.currentState = PROCESS_RESULTS;
      initialized = false;
      stateStartTime = millis();
    }
  }
}

void handleRecallGameState() {
  static bool initialized = false;
  const unsigned long INPUT_TIME = 10000;

  if (!initialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Recall Number:");
    lcd.setCursor(0, 1);
    lcd.print("Use IR Remote");

    // Signal players to start recording IR input
    sendCommand('S', 'R');

    initialized = true;
    stateStartTime = millis();
  }

  unsigned long elapsed = millis() - stateStartTime;

  // Show time remaining
  byte secondsLeft = (INPUT_TIME - elapsed) / 1000;
  if (elapsed % 500 < 50) {
    lcd.setCursor(14, 1);
    lcd.print(secondsLeft < 10 ? " " : "");
    lcd.print(secondsLeft);
  }

  // Time's up
  if (elapsed >= INPUT_TIME) {
    currentSession.prevState = RECALL_GAME;
    currentSession.currentState = PROCESS_RESULTS;
    initialized = false;
    stateStartTime = millis();
  }
}

void handleProcessResultsState() {
  static bool initialized = false;
  static byte currentPlayer = 0;
  static bool requestSent = false;
  static unsigned long requestTime = 0;
  const unsigned long RESPONSE_TIMEOUT = 2000;

  if (!initialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Processing...");

    currentPlayer = 0;
    requestSent = false;
    initialized = true;
  }

  // Process each active player
  if (currentPlayer < 3) {
    if (!players[currentPlayer].isActive) {
      currentPlayer++;
      return;
    }

    if (!requestSent) {
      // Request data from current player
      requestPlayerData(currentPlayer + 1);
      requestSent = true;
      requestTime = millis();
      
      // Debug: Show which player we're waiting for
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Wait P");
      lcd.print(currentPlayer + 1);
      lcd.print(" RX:");
      lcd.print(currentPlayer == 0 ? RX_PLAYER1 : (currentPlayer == 1 ? RX_PLAYER2 : RX_PLAYER3));
    } else {
      // Wait for response
      byte buffer[50];
      if (receivePlayerData(currentPlayer + 1, buffer, 50, RESPONSE_TIMEOUT - (millis() - requestTime))) {
        // Parse and store player data
        lcd.setCursor(0, 1);
        lcd.print("Got P");
        lcd.print(currentPlayer + 1);
        lcd.print(" data!");
        parsePlayerData(currentPlayer, buffer);

        // Move to next player
        currentPlayer++;
        requestSent = false;
      } else if (millis() - requestTime >= RESPONSE_TIMEOUT) {
        // Timeout - mark player as failed
        lcd.setCursor(0, 1);
        lcd.print("P");
        lcd.print(currentPlayer + 1);
        lcd.print(" TIMEOUT");
        players[currentPlayer].isActive = false;
        currentPlayer++;
        requestSent = false;
      }
    }
  } else {
    // All players processed, evaluate results
    evaluateResults();

    // Determine next state
    byte activePlayers = countActivePlayers();

    // Check for game over conditions
    if (activePlayers == 0 || currentSession.level >= MAX_LEVEL) {
      // Game over: all eliminated OR max level reached
      if (debugModeActive) {
        // In debug mode, reset scores and return to settings menu
        resetPlayerScores();
        currentSession.prevState = PROCESS_RESULTS;
        currentSession.currentState = SETTINGS_MENU;
      } else {
        currentSession.prevState = PROCESS_RESULTS;
        currentSession.currentState = GAME_OVER;
      }
    } else if (activePlayers == 1) {
      // Single winner - game over
      if (debugModeActive) {
        // In debug mode, reset scores and return to settings menu
        resetPlayerScores();
        currentSession.prevState = PROCESS_RESULTS;
        currentSession.currentState = SETTINGS_MENU;
      } else {
        currentSession.prevState = PROCESS_RESULTS;
        currentSession.currentState = GAME_OVER;
      }
    } else {
      // Multiple players still active
      if (debugModeActive) {
        // In debug mode, return to settings menu (preserve scores)
        currentSession.prevState = PROCESS_RESULTS;
        currentSession.currentState = SETTINGS_MENU;
      } else {
        // Normal game - continue to next game
        SimonState nextGame = getNextGameState();

        if (nextGame == SEND_MEMORY_NUM) {
          // Completed a round, increase level
          currentSession.level++;
        }

        currentSession.prevState = PROCESS_RESULTS;
        currentSession.currentState = nextGame;
      }
    }

    initialized = false;
    stateStartTime = millis();
  }
}

void handleGameOverState() {
  static bool initialized = false;
  static byte displayMode = 0;
  static unsigned long lastModeChange = 0;
  static byte numWinners = 0;
  static byte winners[3];
  const unsigned long MODE_DURATION = 3000;

  if (!initialized) {
    lcd.clear();

    // Send END_GAME command
    sendCommand('E');

    // Find all winners (handle draws)
    numWinners = findAllWinners(winners);

    initialized = true;
    displayMode = 0;
    lastModeChange = millis();
  }

  unsigned long elapsed = millis() - lastModeChange;

  // Cycle through different displays
  switch (displayMode) {
    case 0:  // Show winner(s)
      if (elapsed == 0 || elapsed < 100) {
        lcd.clear();
        if (numWinners == 0) {
          lcd.setCursor(0, 0);
          lcd.print("All Eliminated!");
          lcd.setCursor(0, 1);
          lcd.print("No Winners");
        } else if (numWinners == 1) {
          lcd.setCursor(0, 0);
          lcd.print("Winner: Player ");
          lcd.print(winners[0] + 1);
          lcd.setCursor(0, 1);
          lcd.print("Score: ");
          lcd.print(players[winners[0]].score);
        } else if (numWinners == 2) {
          lcd.setCursor(0, 0);
          lcd.print("Draw! P");
          lcd.print(winners[0] + 1);
          lcd.print(" & P");
          lcd.print(winners[1] + 1);
          lcd.setCursor(0, 1);
          lcd.print("Score: ");
          lcd.print(players[winners[0]].score);
        } else {
          // All 3 players tied
          lcd.setCursor(0, 0);
          lcd.print("3-Way Draw!");
          lcd.setCursor(0, 1);
          lcd.print("Score: ");
          lcd.print(players[winners[0]].score);
        }
      }
      break;

    case 1:  // Show final level and reason
      if (elapsed == 0 || elapsed < 100) {
        lcd.clear();
        if (currentSession.level >= MAX_LEVEL) {
          lcd.setCursor(0, 0);
          lcd.print("Max Level ");
          lcd.print(MAX_LEVEL);
          lcd.setCursor(0, 1);
          lcd.print("Reached!");
        } else {
          lcd.setCursor(0, 0);
          lcd.print("Final Level: ");
          lcd.print(currentSession.level);
        }
      }
      break;

    case 2:  // Show instructions
      if (elapsed == 0 || elapsed < 100) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Press Button");
        lcd.setCursor(0, 1);
        lcd.print("for Menu");
      }
      break;
  }

  // Cycle displays
  if (elapsed >= MODE_DURATION) {
    displayMode = (displayMode + 1) % 3;
    lastModeChange = millis();
  }

  // Check for button press to return to menu
  if (readButtonDebounced()) {
    currentSession.prevState = GAME_OVER;
    currentSession.currentState = MENU;
    initialized = false;
    numWinners = 0;
  }
}

void handlePausedState() {
  static bool initialized = false;
  static byte selectedOption = 0;

  if (!initialized) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("PAUSED");
    lcd.setCursor(0, 1);
    lcd.print(">Resume  Quit");

    initialized = true;
  }

  // Read joystick
  int xVal = analogRead(JOYSTICK_X);

  if (xVal < 300 && selectedOption != 0) {
    selectedOption = 0;
    lcd.setCursor(0, 1);
    lcd.print(">Resume  Quit  ");
  } else if (xVal > 700 && selectedOption != 1) {
    selectedOption = 1;
    lcd.setCursor(0, 1);
    lcd.print(" Resume >Quit  ");
  }

  // Check button
  if (readButtonDebounced()) {
    if (selectedOption == 0) {
      // Resume
      currentSession.currentState = currentSession.prevState;
      initialized = false;
      stateStartTime = millis();
    } else {
      // Quit to menu
      currentSession.prevState = PAUSED;
      currentSession.currentState = MENU;
      initialized = false;
    }
  }
}



/*
  Communication Protocol
  Simon -> Players

  '$' [CMD] [DATA] '#'

  Commands:
  'W' - WAIT (all players idle)
  'S' - START_GAME [gameType]
  'R' - REQUEST_DATA [playerID]
  'E' - END_GAME
 */

// Helper to get the RX serial port for a specific player
SoftwareSerial* getPlayerRxSerial(byte playerID) {
  switch (playerID) {
    case 1: return &rx1Serial;
    case 2: return &rx2Serial;
    case 3: return &rx3Serial;
    default: return &rx1Serial;
  }
}

// Send command to all players (broadcast on TX - all players share RX from Simon)
void sendCommand(char cmd, byte data) {
  // Use txSerial for transmitting
  txSerial.write('$');
  txSerial.write(cmd);
  if (data != 0) {
    txSerial.write(data);
  }
  txSerial.write('#');
}

// Request data from specific player
void requestPlayerData(byte playerID) {
  // Use txSerial for transmitting
  txSerial.write('$');
  txSerial.write('R');
  txSerial.write('0' + playerID);
  txSerial.write('#');
}

// Non-blocking serial read with support for chunking data
// Uses listen() to switch to the appropriate player's RX line
bool receivePlayerData(byte playerID, byte* buffer, byte maxLen, unsigned long timeout) {
  unsigned long startTime = millis();
  byte index = 0;
  bool started = false;
  
  // Get the correct RX serial port for this player and start listening
  SoftwareSerial* rxSerial = getPlayerRxSerial(playerID);
  rxSerial->listen();

  while (millis() - startTime < timeout) {
    if (rxSerial->available() > 0) {
      char c = rxSerial->read();

      if (c == '@') {
        started = true;
        index = 0;
      } else if (c == '|') {
        // Chunk separator - continue reading
        continue;
      } else if (started && c == '!') {
        return true;  // Complete message received
      } else if (started && index < maxLen) {
        buffer[index++] = c;
      }
    }
  }
  return false;  // Timeout
}

// Helper functions

bool readButtonDebounced() {
  // Read the current state
  byte reading = digitalRead(PUSH_BUTTON);

  // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  // Check if enough time has passed
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    // If the button state has changed:
    if (reading != buttonState) {
      buttonState = reading;

      // Only trigger on button press (HIGH -> LOW transition)
      if (buttonState == LOW) {
        lastButtonState = reading;
        return true;
      }
    }
  }

  lastButtonState = reading;
  return false;
}

void turnOnLED(byte ledNum) {
  turnOffAllLEDs();
  switch (ledNum) {
    case 1: digitalWrite(LED_1, HIGH); break;
    case 2: digitalWrite(LED_2, HIGH); break;
    case 3: digitalWrite(LED_3, HIGH); break;
    case 4: digitalWrite(LED_4, HIGH); break;
  }
}

void turnOffAllLEDs() {
  digitalWrite(LED_1, LOW);
  digitalWrite(LED_2, LOW);
  digitalWrite(LED_3, LOW);
  digitalWrite(LED_4, LOW);
}

void parsePlayerData(byte playerIndex, byte* buffer) {
  // Format: [PLAYER_ID][DATA_LENGTH][DATA...]
  byte dataLength = buffer[1] - '0';

  // DEBUG: Show what we received
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("P");
  lcd.print(playerIndex + 1);
  lcd.print(" Len:");
  lcd.print(dataLength);
  lcd.print(" Raw:");
  // Show first few raw bytes
  for (byte i = 0; i < min(dataLength, (byte)5); i++) {
    lcd.print((char)buffer[2 + i]);
  }
  lcd.setCursor(0, 1);
  lcd.print("Exp:");
  for (byte i = 0; i < min(patternLength, (byte)8); i++) {
    lcd.print(currentPattern[i]);
  }
  delay(3000);  // Show debug for 3 seconds

  players[playerIndex].inputLength = dataLength;
  for (byte i = 0; i < dataLength && i < 50; i++) {
    players[playerIndex].inputData[i] = buffer[2 + i] - '0';
  }
}

void evaluateResults() {
  SimonState lastGame = currentSession.prevState;

  // Check if max level reached before processing
  if (currentSession.level >= MAX_LEVEL) {
    // All remaining players are winners!
    return;
  }

  for (byte i = 0; i < 3; i++) {
    if (!players[i].isActive) continue;

    bool correct = false;

    switch (lastGame) {
      case LED_GAME:
      case BUZ_GAME:
        // Check if pattern matches
        correct = checkPattern(i);
        break;

      case US_GAME:
        // Check if distance is within tolerance
        correct = checkDistance(i);
        break;

      case RECALL_GAME:
        // Check if number matches
        correct = checkRecallNumber(i);
        break;
    }

    if (correct) {
      players[i].score += 10 * currentSession.level;
    } else {
      players[i].isActive = false;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Player ");
      lcd.print(i + 1);
      lcd.print(" OUT!");
      delay(2000);
    }
  }
}

bool checkPattern(byte playerIndex) {
  // DEBUG: Show comparison
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("P");
  lcd.print(playerIndex + 1);
  lcd.print(" Got:");
  for (byte i = 0; i < min(players[playerIndex].inputLength, (byte)6); i++) {
    lcd.print(players[playerIndex].inputData[i]);
  }
  lcd.setCursor(0, 1);
  lcd.print("Need:");
  for (byte i = 0; i < min(patternLength, (byte)6); i++) {
    lcd.print(currentPattern[i]);
  }
  lcd.print(" L");
  lcd.print(players[playerIndex].inputLength);
  lcd.print("/");
  lcd.print(patternLength);
  delay(3000);

  if (players[playerIndex].inputLength != patternLength) {
    return false;
  }

  for (byte i = 0; i < patternLength; i++) {
    if (players[playerIndex].inputData[i] != currentPattern[i]) {
      return false;
    }
  }

  return true;
}

bool checkDistance(byte playerIndex) {
  // Extract distance from player data
  int playerDistance = 0;
  for (byte i = 0; i < players[playerIndex].inputLength; i++) {
    playerDistance = playerDistance * 10 + players[playerIndex].inputData[i];
  }

  // Allow ±7cm tolerance
  const int tolerance = 7;
  return abs(playerDistance - targetDistance) <= tolerance;
}

bool checkRecallNumber(byte playerIndex) {
  // Extract 3-digit number from player input
  if (players[playerIndex].inputLength != 3) {
    return false;
  }

  int playerNumber = players[playerIndex].inputData[0] * 100 + players[playerIndex].inputData[1] * 10 + players[playerIndex].inputData[2];

  return playerNumber == memoryNumber;
}

byte countActivePlayers() {
  byte count = 0;
  for (byte i = 0; i < 3; i++) {
    if (players[i].isActive) count++;
  }
  return count;
}

SimonState getNextGameState() {
  SimonState lastGame = currentSession.prevState;

  switch (lastGame) {
    case LED_GAME: return BUZ_GAME;
    case BUZ_GAME: return US_GAME;
    case US_GAME: return RECALL_GAME;
    case RECALL_GAME: return SEND_MEMORY_NUM;
    default: return LED_GAME;
  }
}

byte findWinner() {
  byte winner = 255;  // No winner
  int highestScore = -1;

  for (byte i = 0; i < 3; i++) {
    if (players[i].score > highestScore) {
      highestScore = players[i].score;
      winner = i;
    }
  }

  return winner;
}

// Find all winners (handles draws)
byte findAllWinners(byte* winnerArray) {
  int highestScore = -1;
  byte numWinners = 0;

  // First pass: find highest score
  for (byte i = 0; i < 3; i++) {
    if (players[i].score > highestScore) {
      highestScore = players[i].score;
    }
  }

  // Second pass: find all players with highest score
  if (highestScore >= 0) {
    for (byte i = 0; i < 3; i++) {
      if (players[i].score == highestScore) {
        winnerArray[numWinners] = i;
        numWinners++;
      }
    }
  }

  return numWinners;
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

