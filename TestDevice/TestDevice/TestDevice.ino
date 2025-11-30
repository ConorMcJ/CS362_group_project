#include <IRremote.h>

const int irPin = 6;    // same pin from player file

void setup() {
  Serial.begin(9600);
  Serial.println("IR Remote Test Ready");
  
  IrReceiver.begin(irPin, ENABLE_LED_FEEDBACK);

  Serial.println("Press buttons on the remote...");
  Serial.println("--------------------------------");
}

void loop() {
  if (IrReceiver.decode()) {

    // Raw decoded value
    unsigned long code = IrReceiver.decodedIRData.decodedRawData;

    Serial.print("Button HEX = 0x");
    Serial.println(code, HEX);

    Serial.print("Address: 0x");
    Serial.println(IrReceiver.decodedIRData.address, HEX);

    Serial.print("Command: 0x");
    Serial.println(IrReceiver.decodedIRData.command, HEX);

    Serial.println("--------------------------------");

    IrReceiver.resume();
  }
}


// <<< UPDATE CODES HERE LATER >>>
#define IR_POWER   0xFFA25D
#define IR_0       0xFF16E9
#define IR_1       0xFF0CF3
#define IR_2       0xFF18E7
#define IR_3       0xFF5EA1
#define IR_4       0xFF08F7
#define IR_5       0xFF1CE3
#define IR_6       0xFF5AA5
#define IR_7       0xFF42BD
#define IR_8       0xFF52AD
#define IR_9       0xFF4AB5