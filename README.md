Simon Says – Multiplayer Survival Memory Game
Arduino Distributed Multiplayer Game (1 Simon + 3 Player Devices)

This project implements a multiplayer survival memory game with four Arduino devices:

1 Simon Device (Master)

3 Player Devices (Players 1–3)

Each round consists of 4 mini-games, each testing a different input modality.
Simon sends game instructions, timers, and then individually polls players using a token system.
Incorrect responses eliminate the player from the game.

System Architecture
                  ┌──────────────┐
                  │    SIMON     │
                  │   (Master)   │
                  └──────┬───────┘
                         │ 3 Dedicated Serial Links
        ┌────────────────┼────────────────┬────────────────┐
   ┌────▼─────┐     ┌────▼─────┐     ┌────▼─────┐
   │ Player 1 │     │ Player 2 │     │ Player 3 │
   └──────────┘     └──────────┘     └──────────┘


Each player now has their own SoftwareSerial connection to Simon.
No more shared bus → no collisions possible.

Hardware Overview
✔️ Simon Device
Component	Purpose	Pins (Final, Verified)
I²C LCD (16×2)	HUD	SDA=A4, SCL=A5
Joystick	Menu (Up/Down/Left/Right + click)	A1, A2, D12
LEDs (R,G,Y,B)	LED minigame output	D8, D9, D13, A3
Buzzer	Buzzer minigame output	D4
Player 1 Serial	SoftwareSerial link	RX=2, TX=3
Player 2 Serial	SoftwareSerial link	RX=4, TX=5
Player 3 Serial	SoftwareSerial link	RX=6, TX=7
Random Seed Input	Noise source	A0

These match the exact code:

SoftwareSerial ss1(2, 3);
SoftwareSerial ss2(4, 5);
SoftwareSerial ss3(6, 7);

✔️ Player Devices (x3)

(Each device has identical wiring except for PLAYER_ID)

Component	Purpose	Pins (Final, Verified)
I²C LCD (16×2)	Player HUD	SDA=A4, SCL=A5
4 Buttons	LED input (R,G,Y,B)	D4, D5, D6, D7
Joystick (Y-axis)	Buzzer input (UP=H,DOWN=L)	A0
Ultrasonic HC-SR04	US distance game	TRIG=8, ECHO=9
IR Receiver	Memory recall digits	D10
Serial Link to Simon	SoftwareSerial	RX=2, TX=3 (Player 1)
RX=4, TX=5 (Player 2)
RX=6, TX=7 (Player 3)

Matches the exact players you have:

// Example: Player 1
const int rxPin = 2;
const int txPin = 3;

Minigame Flow (Every Round)

Each level consists of four games, always in this order:

Order	Game	Code	Description
1	Memory Number	A	Simon shows digits; players memorize
2	LED Pattern	B	Simon flashes LEDs; players reproduce sequence
3	Buzzer Pattern	C	Simon plays tones; players recreate pattern
4	Ultrasonic Distance	D	Players match target distance
5	Memory Recall (IR)	A	Players recall digits shown earlier

If a player fails any game → they are instantly eliminated.

Difficulty Scaling
Memory Number Length
digits = 3 + floor((level - 1) / 3)


Examples:

Level 1 → 3 digits

Level 3 → 4 digits

Level 6 → 5 digits

Timers Used by Mini-games

LED, BUZZER, RECALL (IR)

responseTime = round(6 + 1.5 × level)


Ultrasonic (US)

8 seconds fixed

LCD Behavior
Simon Device LCD
Menu
Close  Start
End    Pause

Level Start
Get Ready!
Level X

Minigame HUD
G:<game> L:<lvl>
T:<seconds>s


Where <game> ∈ {LED, BUZ, US, MEM}

Player Device LCD

Top = info,
Bottom = persistent HUD

Examples:

Mem: 4829        LED T:09s        BUZ T:10s
G:A P1 L:03      G:B P1 L:03      G:C P1 L:03


Ultrasonic:

Aim 27cm
G:D P1 L:03


Memory Recall:

MEM T:08s
G:A P1 L:03

Token-Based Communication Protocol

Only Simon broadcasts freely.
Players only reply during:

S_REQ,<playerID>,<gameType>

Commands From Simon
S_LEVEL,<level>
S_MEM,<digits>
S_LED_START
S_BUZ_START
S_US_START,<targetCm>
S_RECALL_START
S_TIME,<seconds>

Directed request:
S_REQ,<id>,<type>


Examples:

S_REQ,1,LED
S_REQ,2,BUZ
S_REQ,3,US

Player Response Format
P_RESP,<id>,<gameType>,<answer>


Examples:

P_RESP,1,LED,RBGY
P_RESP,2,BUZ,HLLH
P_RESP,3,US,29
P_RESP,1,MEM,4829

Simon Results Broadcast
S_RES,<id>,<gameType>,OK
S_RES,<id>,<gameType>,FAIL


FAIL → player eliminated.

Mini-games in Detail

(Your text unchanged — all accurate)

✔️ Corrected Pin Assignments Summary
SIMON (Master Device)
LCD (I2C): SDA=A4, SCL=A5
Joystick: A1 (X), A2 (Y), D12 (SW)
LEDs: D8 (R), D9 (G), D13 (Y), A3 (B)
Buzzer: D4
SoftwareSerial:
  Player 1 → RX=2, TX=3
  Player 2 → RX=4, TX=5
  Player 3 → RX=6, TX=7
Random Noise Input: A0

PLAYERS (1, 2, and 3 identical except serial pins)
LCD (I2C): SDA=A4, SCL=A5
Buttons (R,G,Y,B): D4, D5, D6, D7
Joystick Y-axis: A0
Ultrasonic: TRIG=8, ECHO=9
IR Receiver: D10
SoftwareSerial:
  Player 1 → RX=2, TX=3
  Player 2 → RX=4, TX=5
  Player 3 → RX=6, TX=7
