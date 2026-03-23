#pragma once
#include <Arduino.h>

// Declarations for GitHub automated bug reporting
bool shouldReportBug();
String collectDiagnostics();
void createGitHubIssue();
