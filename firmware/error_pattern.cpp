void displayErrorPattern(int errorCode) {
  unsigned long now = millis();
  static unsigned long lastFlash = 0;
  static int stepIndex = 0;
  static bool isInPause = false;
  static unsigned long pauseStart = 0;
  
  // Define patterns: array of LED pins to light in sequence
  // 0 = all off, greenLED/yellowLED/redLED = specific LED
  int pattern[7][6] = {
    {redLED, 0, redLED, 0, 0, 0},           // WiFi: Red blink twice
    {yellowLED, redLED, 0, 0, 0, 0},        // Auth: Yellow-Red
    {redLED, yellowLED, redLED, 0, 0, 0},   // Server: Red-Yellow-Red
    {greenLED, yellowLED, 0, 0, 0, 0},      // Time: Green-Yellow
    {redLED, redLED, redLED, 0, 0, 0},      // Memory: Red-Red-Red
    {yellowLED, greenLED, yellowLED, 0, 0, 0}, // JSON: Yellow-Green-Yellow
    {-1, -1, -1, -1, -1, -1}                // Buffer: all solid (handled separately)
  };
  
  // Buffer exhausted: all solid
  if (errorCode == ERR_BUFFER_EXHAUSTED) {
    analogWrite(greenLED, ledConfig.maxBrightness);
    analogWrite(yellowLED, ledConfig.maxBrightness);
    analogWrite(redLED, ledConfig.maxBrightness);
    return;
  }
  
  int patternIndex = errorCode - 1; // ERR_WIFI_DISCONNECT=1 → pattern[0]
  if (patternIndex < 0 || patternIndex >= 6) patternIndex = 0;
  
  // Handle 3-second pause between pattern loops
  if (isInPause) {
    if (now - pauseStart >= 3000) {
      isInPause = false;
      stepIndex = 0;
    }
    setAllLEDsOff();
    return;
  }
  
  // Flash each LED in pattern for 500ms
  if (now - lastFlash >= 500) {
    lastFlash = now;
    
    int ledPin = pattern[patternIndex][stepIndex];
    
    if (ledPin == 0 || ledPin == -1) {
      // End of pattern, start pause
      setAllLEDsOff();
      isInPause = true;
      pauseStart = now;
      stepIndex = 0;
    } else {
      // Light up the LED
      setAllLEDsOff();
      analogWrite(ledPin, ledConfig.maxBrightness);
      stepIndex++;
    }
  }
}
