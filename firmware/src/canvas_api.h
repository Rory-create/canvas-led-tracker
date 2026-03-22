#pragma once
#include <Arduino.h>

// Declaration for Canvas assignment fetching
// Returns: 0=ERROR, 1=GREEN (all clear), 2=YELLOW (due soon), 3=RED (due today)
int fetchCanvasAssignments();
