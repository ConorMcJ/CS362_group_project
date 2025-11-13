void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}

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
