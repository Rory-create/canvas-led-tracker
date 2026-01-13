# GitHub Configuration for OTA Updates

## ✅ What's Already Done

- ✅ OTA code integrated into main.cpp
- ✅ Firmware compiled with OTA support
- ✅ version.json created
- ✅ firmware.bin copied to /ota folder
- ✅ OTA README created

## 🎯 What YOU Need to Do on GitHub

### Step 1: Push OTA Files to GitHub

```bash
cd ~/Documents/canvas-led-tracker

# Add the OTA folder
git add ota/

# Commit
git commit -m "Add OTA infrastructure - v1.2.5 with OTA support"

# Push to main branch
git push origin main
```

### Step 2: Verify Files Are Accessible

After pushing, check these URLs in your browser:

**Version manifest:**
```
https://raw.githubusercontent.com/Rory-create/canvas-led-tracker/main/ota/version.json
```

**Firmware binary:**
```
https://raw.githubusercontent.com/Rory-create/canvas-led-tracker/main/ota/firmware.bin
```

Both should load without errors!

### Step 3: Make Repository Public (If Private)

**If your repo is private:**
1. Go to: https://github.com/Rory-create/canvas-led-tracker/settings
2. Scroll to "Danger Zone"
3. Click "Change visibility"
4. Make public

**Why:** ESP32 can't authenticate to private GitHub repos for OTA

**Alternative:** Use GitHub Releases (more complex, can skip for alpha)

---

## 🔧 After Pushing to GitHub

### Flash All 3 Units with OTA-Enabled Firmware

```bash
cd ~/Documents/canvas-led-tracker/firmware

# Flash unit #1 (3ED8)
pio run --target upload
# Unplug, label, set aside

# Flash unit #2 (55BC)
pio run --target upload
# Unplug, label, set aside

# Flash unit #3 (EE54)
pio run --target upload
# Unplug, label, ready!
```

---

## ✅ Verification Checklist

Before shipping:

- [ ] `git status` shows ota/ folder committed
- [ ] `git push` successful to main branch
- [ ] version.json URL loads in browser
- [ ] firmware.bin URL loads in browser (shows download)
- [ ] Repository is public (or uses GitHub releases)
- [ ] All 3 units flashed with OTA firmware
- [ ] Each unit labeled with MAC last 4

---

## 🚀 Pushing Future Updates (After Alpha)

When you fix a bug and want to push v1.2.6:

### Step 1: Update Version
```bash
# Edit firmware/src/version.h
#define FIRMWARE_VERSION "1.2.6"
```

### Step 2: Compile
```bash
cd firmware
pio run
```

### Step 3: Copy Binary
```bash
cp .pio/build/esp32dev/firmware.bin ../ota/firmware.bin
```

### Step 4: Update Manifest
```bash
# Edit ota/version.json
{
  "version": "1.2.6",
  "firmware_url": "https://raw.githubusercontent.com/Rory-create/canvas-led-tracker/main/ota/firmware.bin",
  "release_notes": "Bug fixes for WiFi reconnection",
  "mandatory": false
}
```

### Step 5: Push to GitHub
```bash
git add ota/ firmware/src/version.h
git commit -m "OTA: Release v1.2.6 - WiFi bug fixes"
git push origin main
```

**All 3 devices will auto-update within 24 hours!** ✨

---

## ⚠️ Important Notes

### OTA Check Interval
- Devices check **once every 24 hours**
- First check happens 24 hours after boot
- Not on every boot (would waste bandwidth)

### Force Immediate Update
If you need devices to update NOW:
1. Have user power cycle device
2. Check serial output for: "🔄 Checking for firmware updates..."
3. If it says "No update available", wait 24 hours

### OTA Fails?
If OTA doesn't work, you can always:
- Manually flash via USB at school
- Debug via serial monitor
- Check GitHub URLs are accessible

---

## 🎯 Current Status

**Firmware Version:** 1.2.5 (with OTA)
**OTA Check URL:** https://raw.githubusercontent.com/Rory-create/canvas-led-tracker/main/ota/version.json
**Binary URL:** https://raw.githubusercontent.com/Rory-create/canvas-led-tracker/main/ota/firmware.bin

**Next Steps:**
1. Push to GitHub (commands above)
2. Verify URLs work
3. Flash all 3 units
4. Ship to alpha testers!

---

**Ready to push to GitHub? Just run the commands in Step 1!** 🚀