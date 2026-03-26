#include "led_control.h"
#include "config.h"
#include "wifi_manager.h"

void setAllLEDsOff() {
  int pins[] = LED_PINS;
  for (int p : pins) analogWrite(p, 0);
}

void setLED(int ledPin, int brightness) {
  setAllLEDsOff();
  analogWrite(ledPin, constrain(brightness, 0, ledConfig.maxBrightness));
}

void pulseLED(int ledPin, unsigned long &lastTime, int &brightness, int &direction) {
  unsigned long now = millis();
  if (now - lastTime >= ledConfig.flashInterval) {
    brightness += direction * ledConfig.flashStep;
    if (brightness >= ledConfig.maxBrightness) {
      brightness = ledConfig.maxBrightness;
      direction = -1;
    } else if (brightness <= 0) {
      brightness = 0;
      direction = 1;
    }
    analogWrite(ledPin, brightness);
    lastTime = now;
  }
}

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
    {-1, -1, -1, -1, -1, -1}                // Buffer: all flash together (handled separately)
  };

  // Buffer exhausted: all three LEDs flash together rapidly
  if (errorCode == ERR_BUFFER_EXHAUSTED) {
    static bool allLEDState = false;
    if (now - lastFlash >= 300) { // Fast flash: 300ms on/off
      lastFlash = now;
      allLEDState = !allLEDState;
      int brightness = allLEDState ? ledConfig.maxBrightness : 0;
      analogWrite(greenLED, brightness);
      analogWrite(yellowLED, brightness);
      analogWrite(redLED, brightness);
    }
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

void updateLEDs() {
  if (isQuietHours()) {
    setAllLEDsOff();
    return;
  }

  // Snooze: hold green LED for the snooze duration
  if (snoozeUntil > 0 && millis() < snoozeUntil) {
    setLED(greenLED, ledConfig.solidBrightness);
    return;
  } else if (snoozeUntil > 0 && millis() >= snoozeUntil) {
    snoozeUntil = 0;  // clear when expired
  }

  // ERROR state (0) = all LEDs flash rapidly
  // ERROR state (0) = display diagnostic pattern based on error code
  if (assignmentStatus == 0) {
    displayErrorPattern(currentErrorCode);
    return;
  }

  int targetLED = greenLED;  // Default: GREEN (1)
  if (assignmentStatus == 3) targetLED = redLED;    // RED (3)
  else if (assignmentStatus == 2) targetLED = yellowLED;  // YELLOW (2)

  if (ledConfig.useFlashing && assignmentStatus > 1) {  // Flash for YELLOW/RED only
    setAllLEDsOff();
    pulseLED(targetLED, lastPulseTime, pulseBrightness, fadeDirection);
  } else {
    setLED(targetLED, ledConfig.solidBrightness);
  }
}
