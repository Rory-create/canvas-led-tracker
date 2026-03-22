#pragma once
#include <Arduino.h>

// Declarations for all LED control functions
void setAllLEDsOff();
void setLED(int ledPin, int brightness);
void pulseLED(int ledPin, unsigned long &lastTime, int &brightness, int &direction);
void displayErrorPattern(int errorCode);
void updateLEDs();
