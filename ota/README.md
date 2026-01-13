# OTA Update System

This folder contains the Over-The-Air (OTA) update infrastructure for DueLight.

## Files

- **version.json** - Version manifest (checked by devices every 24 hours)
- **firmware.bin** - Latest compiled firmware binary

## How It Works

1. **Device checks** for updates every 24 hours
2. **Reads** `version.json` from GitHub
3. **Compares** remote version with current version
4. **Downloads** `firmware.bin` if newer version available
5. **Installs** and reboots automatically

## Pushing New Updates

### Step 1: Update Firmware Version

Edit `firmware/src/version.h`:
```cpp
#define FIRMWARE_VERSION "1.2.6"  // Increment version
```

### Step 2: Compile Firmware

```bash
cd firmware
pio run
```

### Step 3: Copy Binary

```bash
cp firmware/.pio/build/esp32dev/firmware.bin ota/firmware.bin
```

### Step 4: Update version.json

Edit `ota/version.json`:
```json
{
  "version": "1.2.6",
  "firmware_url": "https://raw.githubusercontent.com/Rory-create/canvas-led-tracker/main/ota/firmware.bin",
  "release_notes": "Fixed WiFi reconnection bug",
  "mandatory": false
}
```

### Step 5: Push to GitHub

```bash
git add ota/
git commit -m "OTA: Release v1.2.6 - Bug fixes"
git push origin main
```

**Devices will detect and install the update within 24 hours!**

## URL Configuration

Devices check this URL (configured in `version.h`):
```
https://raw.githubusercontent.com/Rory-create/canvas-led-tracker/main/ota/version.json
```

## Version Comparison

Semantic versioning (MAJOR.MINOR.PATCH):
- `1.2.5` < `1.2.6` (patch update)
- `1.2.6` < `1.3.0` (minor update)
- `1.3.0` < `2.0.0` (major update)

## Testing OTA

### Force OTA Check (Serial Command)

If you have serial access:
1. Open serial monitor at 115200 baud
2. Device will check on boot if 24 hours passed
3. Or wait 24 hours for automatic check

### Monitor OTA Process

Serial output will show:
```
🔄 Checking for firmware updates...
📦 Current: v1.2.5
📦 Remote: v1.2.6
⬇️  Downloading firmware...
✅ Update successful! Rebooting...
```

## Troubleshooting

### "OTA check failed"
- Check GitHub repo is public
- Verify `version.json` URL is correct
- Ensure device has internet access

### "Download failed"
- Large firmware.bin (>1MB) may timeout
- Check GitHub raw URL is accessible
- Verify SSL certificate (GitHub uses HTTPS)

### "Install failed"
- Not enough flash space
- Corrupted download
- Device will rollback automatically

## Safety Features

- ✅ Version comparison prevents downgrades
- ✅ Automatic rollback if install fails
- ✅ WiFi credentials preserved during update
- ✅ Watchdog timer prevents bricking
- ✅ 24-hour check interval (not excessive)

## Manual Update (If OTA Fails)

Always keep USB access available:
```bash
cd firmware
pio run --target upload
```

---

**Current Version:** 1.2.5 (OTA enabled)
**Last Updated:** January 13, 2026
