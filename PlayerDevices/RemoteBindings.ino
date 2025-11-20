#include <IRremote.h>

const int irPin = 8;  // IR receiver pin

void setup() {
  Serial.begin(9600);
  IrReceiver.begin(irPin, ENABLE_LED_FEEDBACK);
  Serial.println("Ready. Press buttons...");
}

void loop() {
  if (IrReceiver.decode()) {
    Serial.print("HEX = 0x");
    Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);
    IrReceiver.resume();
  }
}
