# Simon Says – Multiplayer Survival Memory Game

### Arduino Distributed Multiplayer Game (1 Simon + 3 Player Devices)

This project implements a multiplayer survival memory game with **four Arduino devices**:

* **1 Simon Device (Master)**
* **3 Player Devices (Players 1–3)**

Each round consists of **5 mini-games**, each testing a different input modality.
Simon sends game instructions, timers, and then individually polls players using a token system.
Incorrect responses eliminate the player from the game.

---

# System Architecture

```
                  ┌──────────────┐
                  │   SIMON      │
                  │   (Master)   │
                  └──────┬───────┘
                         │ Shared SoftwareSerial Bus
        ┌────────────────┼────────────────┬────────────────┐
   ┌────▼─────┐     ┌────▼─────┐     ┌────▼─────┐
   │ Player 1 │     │ Player 2 │     │ Player 3 │
   └──────────┘     └──────────┘     └──────────┘
```

All devices share the same **SoftwareSerial bus**.
Simon is always the only transmitter except when explicitly requesting a single player’s response.

---

# Hardware Overview

## Simon Device

| Component         | Purpose                            | Pins            |
| ----------------- | ---------------------------------- | --------------- |
| I²C LCD (16×2)    | HUD                                | SDA=A4, SCL=A5  |
| Joystick          | Menu (Up/Down/Left/Right + button) | A1, A2, D12     |
| LEDs (R,G,Y,B)    | LED minigame output                | D8, D9, D13, A3 |
| Buzzer            | Buzzer minigame output             | D4              |
| Shared Serial Bus | Communication with players         | RX=10, TX=11    |

---

## Player Devices (x3)

| Component          | Purpose                     | Pins           |
| ------------------ | --------------------------- | -------------- |
| I²C LCD (16×2)     | Player HUD                  | SDA=A4, SCL=A5 |
| 4 Buttons          | LED input (R,G,Y,B)         | D2, D3, D4, D5 |
| Joystick (Y-axis)  | Buzzer input (UP=H, DOWN=L) | A0             |
| Ultrasonic HC-SR04 | US distance game            | TRIG=6, ECHO=7 |
| IR Receiver        | Memory recall digits        | D8             |
| Shared Serial Bus  | Communication               | RX=10, TX=11   |

---

# Minigame Flow (Every Round)

Each level consists of **five games**, always in this order:

| Order | Game                | Code | Description                                    |
| ----- | ------------------- | ---- | ---------------------------------------------- |
| 1     | Memory Number       | A    | Simon shows digits; players memorize           |
| 2     | LED Pattern         | B    | Simon flashes LEDs; players reproduce sequence |
| 3     | Buzzer Pattern      | C    | Simon plays tones; players recreate pattern    |
| 4     | Ultrasonic Distance | D    | Players match target distance                  |
| 5     | Memory Recall (IR)  | A    | Players recall the digits from game 1          |

If a player fails any game → they are eliminated immediately.
Game continues until all players are eliminated.

---

# Difficulty Scaling

### Memory Number Length

```
digits = 3 + floor((level - 1) / 3)
```

Examples:
Level 1 → 3 digits
Level 3 → 4 digits
Level 6 → 5 digits

---

### Timers Used by Mini-games

**LED, BUZ, MEM (recall)**

```
responseTime = round(6 + 1.5 × level)
```

**US distance game**

```
8 seconds fixed
```

**IR recall**

```
8 seconds fixed
```

---

# LCD Behavior

## Simon Device LCD

### Menu

```
Close  Start
End    Pause
```

### Level Start

```
Get Ready!
Level X
```

### Minigame HUD

```
G:<game> L:<lvl>
T:<seconds>s
```

Where `<game>` is one of:
`LED`, `BUZ`, `US`, `MEM`

---

## Player Device LCD

Top line = game info
Bottom line = persistent HUD

Examples:

### Memory Number

```
Mem: 4829
G:A P1 L:03
```

### LED Game

```
LED T:09s
G:B P1 L:03
```

### Buzzer Game

```
BUZ T:10s
G:C P1 L:03
```

### Ultrasonic Game

```
Aim 27cm T:08s
G:D P1 L:03
```

### Memory Recall

```
MEM T:08s
G:A P1 L:03
```

The HUD line always displays:

```
G:<gameCode> P<id> L:<level>
```

---

# Token-Based Communication Protocol

To avoid collisions, only Simon may broadcast freely.
Players only transmit during a **request window**.

---

## Commands From Simon

```
S_LEVEL,<level>
S_MEM,<digits>
S_LED_START
S_BUZ_START
S_US_START,<targetCm>
S_RECALL_START
S_TIME,<seconds>
```

### Request to a specific player

```
S_REQ,<playerID>,<gameType>
```

Examples:

```
S_REQ,1,LED
S_REQ,3,BUZ
S_REQ,2,US
```

---

## Player Response Format

Players only respond when requested:

```
P_RESP,<id>,<gameType>,<answer>
```

Examples:

```
P_RESP,1,LED,RBGY
P_RESP,2,BUZ,HLLH
P_RESP,3,US,29
P_RESP,1,MEM,4829
```

---

## Simon Evaluation Results

```
S_RES,<id>,<gameType>,OK
S_RES,<id>,<gameType>,FAIL
```

If FAIL → player is eliminated.

---

# Minigames in Detail

## A — Memory Number (First Phase)

* Simon generates digits using:

  ```
  digits = 3 + floor((level - 1) / 3)
  ```
* Broadcasts `S_MEM,NNN...`
* Players simply view and memorize.

---

## B — LED Pattern Game

* Simon flashes sequence of R/G/Y/B.
* Players press their 4 buttons to enter the same pattern.
* Answer is automatically locked when timer expires.

---

## C — Buzzer Pattern Game

* Simon plays sequence of tones:

  * `H` → high + short
  * `L` → low + long
* Player inputs via joystick:

  * Up → H
  * Down → L

---

## D — Ultrasonic Distance Game

* Simon sends:

  ```
  S_US_START,<targetCm>
  S_TIME,8
  ```
* Player reads their US sensor continuously.
* Final answer = **last stable distance before timeout**.
* Pass condition:

  ```
  |answer - target| <= 5 cm
  ```

---

## A — Memory Recall (IR)

* Player uses IR remote to enter digits.
* Simon compares exact string:

  ```
  ans == memoryNumber
  ```
* No tolerance.

---

# Pin Assignments Summary

## Simon

```
LCD: I2C A4 (SDA), A5 (SCL)
Joystick: A1, A2, D12
LEDs: D8, D9, D13, A3
Buzzer: D4
SoftwareSerial: RX=10, TX=11
```

## Player

```
LCD: I2C A4 (SDA), A5 (SCL)
Buttons (LED input): D2, D3, D4, D5
Joystick Y: A0
Ultrasonic: TRIG=6, ECHO=7
IR Receiver: D8
SoftwareSerial: RX=10, TX=11
```

---

# Game Over Logic

A level ends after all 5 minigames are completed.
If **no players remain alive**, Simon enters GAME_OVER state.

Otherwise:

* Level increments
* Next level starts automatically with new difficulty settings

---

# Important Internal Timers

| Purpose                  | Device          | Duration                 |
| ------------------------ | --------------- | ------------------------ |
| Pattern pace (LED)       | Simon           | 600 ms per LED           |
| Memory display           | Simon & Players | ~2 seconds               |
| LED response window      | All             | `round(6 + 1.5 × level)` |
| BUZ response window      | All             | `round(6 + 1.5 × level)` |
| MEM recall               | All             | `round(6 + 1.5 × level)` |
| US distance              | All             | 8 seconds fixed          |
| Token timeout per player | Simon           | 3 seconds                |

---

# Remaining TODOs

### Both Sides

* IR remote’s **digit→hex code mapping** must be finalized.
* Adjust tolerances or timings if playtesting suggests improvements.

### Player Side

* Add any missing IR hex bindings once identified.

### Simon Side

* Allow optional round summaries or spectator display modes.
