#pragma once
#include <Arduino.h>

// Send a health heartbeat to the configured dashboard URL.
// No-op if dashboardUrl is empty or WiFi is not connected.
void sendTelemetry();
