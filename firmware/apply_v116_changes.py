#!/usr/bin/env python3
"""
Script to apply v1.1.6 changes to main.cpp
This performs surgical replacements to upgrade to dynamic buffer + new LED states
"""

# Read the entire main.cpp
with open('/Users/roryharmer/Documents/canvas-led-tracker/firmware/src/main.cpp', 'r') as f:
    lines = f.readlines()

# Make a backup
with open('/Users/roryharmer/Documents/canvas-led-tracker/firmware/src/main.cpp.backup', 'w') as f:
    f.writelines(lines)

print("âœ… Backup created: main.cpp.backup")

# Change 1: Update CanvasConfig structure (around line 28)
for i in range(len(lines)):
    if 'struct CanvasConfig {' in lines[i]:
        # Find the closing brace
        for j in range(i, min(i+10, len(lines))):
            if '} canvasConfig;' in lines[j]:
                # Replace the entire struct
                lines[i:j+1] = [
                    'struct CanvasConfig {\n',
                    '  char apiUrl[256] = "https://ojrsd.instructure.com/api/v1/users/self/todo";\n',
                    '  char apiToken[128] = "";\n',
                    '  int itemsPerPage = 5;  // Balanced coverage with 80KB buffer\n',
                    '  unsigned long fetchInterval = 10UL * 60UL * 1000UL;\n',
                    '  size_t jsonBufferSize = 81920;  // 80KB starting buffer\n',
                    '} canvasConfig;\n'
                ]
                print("âœ… Updated CanvasConfig structure")
                break
        break

# Change 2: Replace fetchCanvasAssignments function
# Find start and end
fetch_start = -1
fetch_end = -1
for i in range(len(lines)):
    if 'int fetchCanvasAssignments() {' in lines[i]:
        fetch_start = i
    if fetch_start >= 0 and i > fetch_start and lines[i].strip().startswith('// ============================================') and 'LED CONTROL' in lines[i]:
        fetch_end = i
        break

if fetch_start >= 0 and fetch_end >= 0:
    # Read the new function
    with open('/Users/roryharmer/Documents/canvas-led-tracker/firmware/updated_fetch_function.cpp', 'r') as f:
        new_func_lines = f.readlines()[2:]  # Skip first 2 comment lines
    
    # Replace
    lines[fetch_start:fetch_end] = new_func_lines + ['\n']
    print(f"âœ… Replaced fetchCanvasAssignments (lines {fetch_start}-{fetch_end})")
else:
    print(f"â ï¸ Could not find fetchCanvasAssignments boundaries")

# Change 3: Update updateLEDs function
for i in range(len(lines)):
    if 'void updateLEDs() {' in lines[i]:
        # Find the closing brace
        brace_count = 0
        for j in range(i, len(lines)):
            brace_count += lines[j].count('{') - lines[j].count('}')
            if brace_count == 0 and j > i:
                # Replace the entire function
                lines[i:j+1] = [
                    'void updateLEDs() {\n',
                    '  if (isQuietHours()) {\n',
                    '    setAllLEDsOff();\n',
                    '    return;\n',
                    '  }\n',
                    '\n',
                    '  // ERROR state (0) = all LEDs flash rapidly\n',
                    '  if (assignmentStatus == 0) {\n',
                    '    unsigned long now = millis();\n',
                    '    if (now - lastPulseTime >= 200) {  // Fast flash (200ms)\n',
                    '      lastPulseTime = now;\n',
                    '      static bool flashState = false;\n',
                    '      flashState = !flashState;\n',
                    '      int brightness = flashState ? ledConfig.maxBrightness : 0;\n',
                    '      analogWrite(greenLED, brightness);\n',
                    '      analogWrite(yellowLED, brightness);\n',
                    '      analogWrite(redLED, brightness);\n',
                    '    }\n',
                    '    return;\n',
                    '  }\n',
                    '\n',
                    '  int targetLED = greenLED;  // Default: GREEN (1)\n',
                    '  if (assignmentStatus == 3) targetLED = redLED;    // RED (3)\n',
                    '  else if (assignmentStatus == 2) targetLED = yellowLED;  // YELLOW (2)\n',
                    '\n',
                    '  if (ledConfig.useFlashing && assignmentStatus > 1) {  // Flash for YELLOW/RED only\n',
                    '    setAllLEDsOff();\n',
                    '    pulseLED(targetLED, lastPulseTime, pulseBrightness, fadeDirection);\n',
                    '  } else {\n',
                    '    setLED(targetLED, ledConfig.solidBrightness);\n',
                    '  }\n',
                    '}\n'
                ]
                print(f"âœ… Updated updateLEDs function")
                break
        break

# Change 4: Update status text references
for i in range(len(lines)):
    if 'if (assignmentStatus == 1) statusText = "Today";' in lines[i]:
        lines[i:i+2] = [
            '  if (assignmentStatus == 0) statusText = "ERROR - Check Device";\n',
            '  else if (assignmentStatus == 3) statusText = "Today";\n',
            '  else if (assignmentStatus == 2) statusText = "Tomorrow";\n',
            '  else statusText = "All Clear";\n'
        ]
        print("âœ… Updated status text (web interface)")
        break

# Change 5: Update statusName
for i in range(len(lines)):
    if 'String statusName = assignmentStatus == 1' in lines[i]:
        # Find end of this statement (could be multi-line)
        for j in range(i, min(i+5, len(lines))):
            if ';' in lines[j]:
                lines[i:j+1] = [
                    '  String statusName = assignmentStatus == 0 ? "ERROR (check device)" :\n',
                    '                      assignmentStatus == 3 ? "RED (due today)" :\n', 
                    '                      assignmentStatus == 2 ? "YELLOW (due soon)" : "GREEN (all clear)";\n'
                ]
                print("âœ… Updated statusName")
                break
        break

# Write the modified file
with open('/Users/roryharmer/Documents/canvas-led-tracker/firmware/src/main.cpp', 'w') as f:
    f.writelines(lines)

print("\nâœ… All changes applied to main.cpp")
print("ðŸ"„ Backup saved as: main.cpp.backup")
