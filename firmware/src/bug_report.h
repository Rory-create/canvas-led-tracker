#pragma once
#include <Arduino.h>

bool shouldReportBug();
String collectDiagnostics();
void reportBug();
