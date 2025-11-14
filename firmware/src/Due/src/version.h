#ifndef VERSION_H
#define VERSION_H

// Semantic versioning: MAJOR.MINOR.PATCH
#define FIRMWARE_VERSION "1.0.0"
#define BUILD_TIMESTAMP __DATE__ " " __TIME__

// OTA Configuration
#define OTA_UPDATE_URL "https://raw.githubusercontent.com/Rory-create/canvas-led-tracker/main/ota/version.json"
#define OTA_CHECK_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL)  // 24 hours

// Version history:
// 1.0.0 - Initial production release (first batch of 5 devices)

#endif
