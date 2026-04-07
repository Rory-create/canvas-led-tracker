# DueLight - Canvas LMS Assignment Tracker

**One-liner:** ESP32-based WiFi LED tracker polling Canvas API every 10min, showing assignment urgency with colored LEDs (green/yellow/red).

## Build & Test Commands

```bash
# Build firmware
pio run

# Flash to device (DTR reset auto-triggers bootloader)
pio run --target upload

# Monitor serial output (115200 baud)
pio device monitor --baud 115200

# Factory reset device
pio run --target erase && pio run --target upload

# In Arduino IDE: set partition scheme to "Default (1.3MB APP / 3.4MB SPIFFS)" if OTA fails
```

**Serial port (macOS):** `/dev/tty.usbserial-0001`

## Architecture

- **src/main.cpp** - Core firmware: WiFi, Canvas API polling, LED control, web server
- **src/version.h** - Firmware version, DEV_MODE flag, GitHub credentials, OTA config
- **src/ota_update.cpp** - OTA update system (GitHub release checks, watchdog management)
- **platformio.ini** - Build config, ArduinoJson dependency (v6.21.3+), debug level

## Critical Gotchas

### Memory Management
- Canvas API responses with embedded HTML are HUGE (49KB unfiltered). **Always use ArduinoJson filter** to skip description fields.
- Default buffer starts 8KB. If `NoMemory` error: code auto-scales to 90% free heap (up to max ~81KB).
- Watchdog timer is **disabled during JSON parsing** (HTML-heavy responses take 10+ seconds). Re-enable with `esp_task_wdt_add(NULL)` after parsing.
- Never allocate large fixed buffers; use dynamic sizing based on `ESP.getFreeHeap()`.

### WiFi & AP Mode
- **CRITICAL:** Don't change WiFi mode in `connectWiFi()` — it kills the AP broadcast during setup. Mode is set once in `startSettingsAP()` as `WIFI_AP_STA`.
- If WiFi scan fails (error code -1), it's a mode conflict. Check AP is started *before* calling scan.

### LED & Error States
- **Status 0** = ERROR (diagnostic LED pattern based on `currentErrorCode`). Status 1 = GREEN, 2 = YELLOW, 3 = RED.
- Error patterns are **sequential blinks** (distinguishable), not timing-based flashing. Example: WiFi error = red blink twice, then 3s pause, repeat.
- Flashing only applies to YELLOW/RED (status > 1). GREEN is always solid.

### Timezone & Time
- Canvas API returns UTC (`due_at` format: `2025-04-15T18:00:00Z`).
- Must convert UTC to local time using offset calculated from `localtime_r()` and `gmtime_r()`.
- Time sync happens via NTP on first WiFi connect. If NTP fails, Canvas polling is disabled until sync succeeds.

### Canvas API
- Endpoint: `https://ojrsd.instructure.com/api/v1/users/self/todo`
- Token validation: 401 = invalid token (don't retry), 500+ = server outage (break early, don't retry).
- Rate limit: 429 response keeps current status, doesn't increment consecutive errors.
- Default: exclude overdue assignments (toggle via `includeOverdue` pref). Some extensions don't sync completion with API.

## Conventions

- **Naming:** `snakeCase` for config vars, `camelCase` for functions, `CAPS` for constants.
- **Error Codes:** Enum `ErrorCode` maps 0-7 to specific faults (WiFi, auth, server, time, memory, JSON, buffer). Update both enum AND `errorNames[]` array when adding.
- **LED Pins:** GPIO 32 (green), 25 (yellow), 27 (red). Hardcoded; matches v1.3.0 board layout.
- **Version Format:** `MAJOR.MINOR.PATCH` (e.g., "1.3.0"). Increment in `version.h` for each firmware release.
- **DEV_MODE:** Set `true` for testing only. Resets non-credential config on boot (keeps WiFi/Canvas token). Never ship with `DEV_MODE true`.

## Common Pitfalls

- **Credentials lost after update?** Save WiFi SSID/password and Canvas token *before* clearing preferences. The `loadConfig()` function does this in DEV_MODE.
- **JSON parse hangs for 10+ seconds?** Watchdog timer is running. This is expected for HTML-heavy responses. If timeout (30s), watchdog will trigger ESP.restart(). Check heap size.
- **"Scan failed" on setup page?** WiFi mode not set to `WIFI_AP_STA` before scan. Restart device.
- **LEDs stuck on/won't blink?** Check if in quiet hours (22:00-07:00 default). Adjust in settings or disable quiet hours.
- **Timezone shows wrong time?** Verify timezone string format in settings (e.g., "EST5EDT,M3.2.0/2,M11.1.0/2"). Empty string defaults to US Eastern.

## MCP Integrations

None currently. GitHub integration uses standard HTTPS (API token in `version.h`).

---

**For detailed Canvas payload investigation, memory profiling, or OTA testing:** See `/docs/canvas-memory-analysis.md` (add if needed).
