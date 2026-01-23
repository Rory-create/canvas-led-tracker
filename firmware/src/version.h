#ifndef VERSION_H
#define VERSION_H

// Semantic versioning: MAJOR.MINOR.PATCH
#define FIRMWARE_VERSION "1.2.7"
#define BUILD_TIMESTAMP __DATE__ " " __TIME__

// Development mode - resets non-credential preferences on boot
// Set to false for production builds
#define DEV_MODE false

// GitHub bug reporting
#define GITHUB_TOKEN "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"
#define GITHUB_REPO "Rory-create/canvas-led-tracker"

// OTA Configuration
#define OTA_UPDATE_URL "https://raw.githubusercontent.com/Rory-create/canvas-led-tracker/main/ota/version.json"
#define OTA_CHECK_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL)  // 24 hours

// Version history:
// 1.0.0 - Initial production release (first batch of 5 devices)
// 1.1.5 - JSON parsing investigation (reverted to String buffering)
// 1.1.6 - Production robustness: 80KB buffer, dynamic scaling, error state LEDs
// 1.1.7 - DEV_MODE: Serial commands + web UI for LED pattern testing
// 1.1.8 - Sequential error patterns (distinguishable blink sequences), AP mode command
// 1.1.9 - Assignment display on web UI (name, due date, description, Canvas link)
// 1.2.0 - GitHub bug logging (auto-report critical errors to GitHub Issues)
// 1.2.1 - Production ready: Specific error messages, DEV_MODE disabled
// 1.2.2 - Bug fixes: Smart buffer scaling logic, flashing LEDs for buffer exhaustion
// 1.2.3 - Watchdog management: Disable during slow JSON parsing (HTML-heavy responses)
// 1.2.4 - ArduinoJson filtering: Skip description field, 8KB initial buffer (was 81KB)
// 1.2.5 - Overdue assignment filtering: Add toggle to exclude overdue (default OFF)
// 1.2.6 - OTA test release: Added version:info command for update verification
// 1.2.7 - CRITICAL: Fix OTA watchdog timeout causing boot loops during updates

#endif
