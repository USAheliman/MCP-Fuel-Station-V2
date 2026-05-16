# MCP Fuel Station V2 — User Manual

**Firmware v2.7.8** | ESP32-S3 N16R8 | ST7796S 3.5" IPS Display

---

## Table of Contents

1. [Overview](#1-overview)
2. [Hardware Summary](#2-hardware-summary)
3. [First Power-On](#3-first-power-on)
4. [Power & Button Controls](#4-power--button-controls)
5. [Screen Interface](#5-screen-interface)
   - 5.1 [HOME Screen](#51-home-screen)
   - 5.2 [MODEL BROWSER Screen](#52-model-browser-screen)
   - 5.3 [NETWORK / OTA Screen](#53-network--ota-screen)
   - 5.4 [HELP Screen](#54-help-screen)
6. [Web Interface](#6-web-interface)
   - 6.1 [Connecting](#61-connecting)
   - 6.2 [Main Page](#62-main-page)
   - 6.3 [Fill Page](#63-fill-page)
   - 6.4 [Drain Page](#64-drain-page)
   - 6.5 [Setup Page](#65-setup-page)
   - 6.6 [Station Page](#66-station-page)
   - 6.7 [OTA Update Page](#67-ota-update-page)
   - 6.8 [Event Log Page](#68-event-log-page)
7. [Helicopter Models](#7-helicopter-models)
8. [Fill Operation](#8-fill-operation)
9. [Drain Operation](#9-drain-operation)
10. [Auto Fill Sequence (No Tank Sensor)](#10-auto-fill-sequence-no-tank-sensor)
11. [Supply Tank Management](#11-supply-tank-management)
12. [Battery Management](#12-battery-management)
13. [Pump Failsafes](#13-pump-failsafes)
14. [Flow Sensor Calibration](#14-flow-sensor-calibration)
15. [Pump Speed Calibration](#15-pump-speed-calibration)
16. [Standby and Auto-Shutdown](#16-standby-and-auto-shutdown)
17. [OTA Firmware Update](#17-ota-firmware-update)
18. [Troubleshooting](#18-troubleshooting)
19. [Log Event Reference](#19-log-event-reference)

---

## 1. Overview

The MCP Fuel Station V2 is an automated fuel management system for RC helicopters. It uses a bidirectional peristaltic pump controlled by a closed-loop PI controller to fill and drain helicopter fuel tanks to a precise volume at a selectable flow rate. Up to 20 helicopter models can be stored, each with their own tank volume, speeds, and settings.

The system is operated either from a 3.5" touchless display with a rotary encoder, or from any web browser on the same network.

**Key capabilities:**
- Fill a model tank to its exact volume at a set flow rate
- Drain a tank to empty, detecting the empty point automatically
- Auto-fill sequence: drain first then fill, for models without a tank-full sensor
- Overflow purge: briefly reverse the pump after fill to clear the fuel line
- Supply tank tracking: deducts fill volumes, warns when running low
- Event logging: every pump operation, fault, and setting change is time-stamped and stored

---

## 2. Hardware Summary

| Component | Detail |
|-----------|--------|
| Controller | ESP32-S3 DevKitC-1 N16R8 (16 MB flash, 8 MB PSRAM) |
| Display | ST7796S 3.5" IPS, 480×320, landscape |
| Motor driver | BTS7960 H-bridge |
| Pump direction | Positive PWM = Fill, Negative PWM = Drain |
| Flow sensors | Two hall-effect pulse sensors (fill line and drain line) |
| Tank sensor | Reed-switch full sensor + shell detect wire |
| Battery ADC | 100k/33k voltage divider on GPIO1 — reads up to 12.6V (3S) |
| Buzzer | Passive piezo on GPIO3, bit-bang driven |
| Encoder | KY-040 rotary encoder — navigation and speed adjustment |
| Power button | Encoder push-button doubles as power/action button |
| AP network | SSID: `MCP-FuelStation-V2`, always active |
| Home network | Connects to `SilverLining` on boot |
| mDNS | `fuelstation.local` works on both networks |

---

## 3. First Power-On

1. Power on the station. The screen shows the **splash screen** and the buzzer plays a four-note ascending fanfare (~1.2 seconds).
2. After 2 seconds the HOME screen appears. The message bar briefly shows the AP IP address (e.g. `192.168.4.1`).
3. If home WiFi is configured the station connects in the background. Once connected the message bar updates to show the OTA address (e.g. `OTA: 192.168.1.42/ota`) for about 6 seconds, then clears.
4. The RTC clock is synchronised via NTP on first WiFi connection. The date and time appear in the top-left of the header.
5. The station is ready to use. No calibration is needed if models and calibration values were previously saved.

---

## 4. Power & Button Controls

The rotary encoder push-button is the sole physical control. It has three behaviours:

| Action | Duration | Function |
|--------|----------|----------|
| Short press | < 1.5 s | Activate highlighted item / confirm |
| Back hold | ≥ 3 s | Return to HOME screen; stop pump if running |
| Shutdown hold | ≥ 8 s | Save all data, play shutdown tone, power off |

The **rotary encoder** (turning) navigates menus and adjusts pump speed while running.

### Standby and Wake
After **10 minutes** of inactivity the display dims to standby and the buzzer plays a short descending tone. Any encoder movement or button press wakes it immediately. After **15 minutes** the station saves all data and powers off automatically.

> **Note:** The pump continuing to run does not reset the standby timer. The display may go into standby during a long fill — the pump keeps running normally. Turn the encoder to wake the display.

---

## 5. Screen Interface

The screen is divided into three horizontal zones that appear on every page:

| Zone | Rows | Content |
|------|------|---------|
| **Header** | 0–35 | Date/time (left) · Screen title (centre) · Firmware version (right) |
| **Content** | 36–283 | Left panel (photo/info) + Right panel (data/controls) |
| **Message bar** | 284–319 | `[?]` help button · Status message |

The message bar pulses the background colour to the active operation's colour (green for fill, orange for drain) while the pump is running.

---

### 5.1 HOME Screen

The default screen. The header title reads **HOME** when idle, **FILL MODE** or **DRAIN MODE** when the pump is running.

#### Left Panel
- **Photo**: Shows a thumbnail of the active model's photo if one has been uploaded, otherwise a coloured letter placeholder.
- **Model name**
- **Tank volume** (yellow) — e.g. `450 ml`
- **Sensor status** — `SENSOR: YES` (green) or `SENSOR: NO` (orange)
- **RESET SUPPLY button** (idle) — resets supply tank to full capacity. Scroll to it with the encoder and press.
- **STOP / BACK buttons** (pump running) — STOP is always highlighted red. Press to stop immediately. BACK returns to HOME without acting.
- **START / BACK buttons** (post-pump review) — BACK is highlighted by default. Use encoder to select START to repeat the last operation.

#### Right Panel — Idle
Three status bars and four action buttons.

**Status bars:**
| Bar | Colour logic |
|-----|-------------|
| **MODEL TANK** | Blue always. Shows tank volume in ml. A yellow tick mark shows the fill target. |
| **SUPPLY TANK** | Green when above the low threshold. Yellow when between the threshold and half the threshold. Red below half the threshold. A white tick shows the low-threshold position. |
| **BATTERY** | Green ≥ 50%. Yellow 20–50%. Red < 20%. Shows voltage per cell and percentage. |

**Action buttons** (scroll with encoder, confirm with press):

| Button | Colour | Action |
|--------|--------|--------|
| **FILL** | Green | Start filling the active model |
| **DRAIN** | Orange | Start draining the active model |
| **MODEL** | Blue | Open the model browser |
| **NET** | Cyan | Open the network/OTA screen |
| **HELP** | Yellow | Open context-sensitive help (in message bar `[?]`) |
| **RESET SUPPLY** | Cyan | Reset supply tank to full (in left panel) |

#### Right Panel — Pump Running
The four action buttons are replaced by three live bars:

| Bar | Description |
|-----|-------------|
| **PUMP SPEED** | Current PWM as a percentage of maximum. Colour matches operation (green=fill, orange=drain). |
| **FLOW RATE** | Current flow in ml/min, scaled 0–2000 ml/min. A tick mark shows the target speed. |
| **BATTERY** | Same as idle. |

The MODEL TANK and SUPPLY TANK bars remain visible above the separator line throughout.

---

### 5.2 MODEL BROWSER Screen

Accessed from HOME → **MODEL** or from HELP on the model screen.

Shows up to three helicopter model cards at once. Scroll with the encoder; press to activate the highlighted model.

Each card shows:
- **Thumbnail photo** (or letter placeholder)
- **Model name** — highlighted when selected; `ACTIVE` badge shown in green for the currently active model
- **Specs row**: Tank volume · Fill speed · Drain speed · Sensor fitted · Purge time
- **Stats row**: Total fills · Total drains · Total volume pumped

A scroll indicator bar appears on the far right when there are more than three models.

**To change the active model:** scroll to the desired model and press the button. The screen returns to HOME with the new model selected.

---

### 5.3 NETWORK / OTA Screen

Accessed from HOME → **NET**.

Two cards side by side:

| Card | Content |
|------|---------|
| **ACCESS POINT** | Always-on AP. mDNS name `fuelstation.local` · AP IP address. |
| **HOME WIFI** | `CONNECTED` (green) or `NOT CONNECTED` (orange) · STA IP address when connected. |

Below these is an **EVENT LOG** card showing `fuelstation.local/log` — open this URL in any browser to view the log.

A hint strip at the bottom reads: *Open browser to configure or update firmware | OTA update available via WiFi.*

Press the encoder button or hold 3 seconds to return to HOME.

---

### 5.4 HELP Screen

Context-sensitive help that changes depending on which screen and operation is active when you press the `[?]` button (bottom-left of the message bar) or select HELP from the action strip.

| Context | Help content |
|---------|-------------|
| HOME idle | Overview of all action buttons |
| HOME filling | Fill operation controls and auto-stop conditions |
| HOME draining | Drain operation controls and empty detection |
| MODEL browser | Encoder controls and card field descriptions |
| NETWORK screen | AP and home WiFi details, web page index |

Press the encoder button to return from help.

---

## 6. Web Interface

### 6.1 Connecting

1. **On the same home network**: open `http://fuelstation.local` in any browser.
2. **Direct to the station** (no router needed): connect your device's WiFi to `MCP-FuelStation-V2` (no password), then open `http://fuelstation.local` or `http://192.168.4.1`.

The web interface communicates over WebSocket and updates at 4 Hz. All pages auto-navigate based on pump state — if a fill starts from the screen the web page switches to the Fill page automatically.

---

### 6.2 Main Page

The status dashboard, shown when the pump is idle.

**Supply Tank card:**
- Volume remaining vs. capacity (e.g. `14.2L / 20.0L`)
- Percentage bar — green/yellow/red with low-threshold tick mark (same logic as the screen)

**Battery card:**
- Pack voltage and percentage bar
- Cell chemistry type (2S / 3S)

**Model card:**
- Active model name and photo (or initial placeholder)
- Fuel tank capacity
- Tank sensor status

**Station stats:**
- Supply cap, remaining, low threshold
- Flow calibration values
- Empty detection settings
- Battery cutoff voltage
- Lifetime totals (fills, drains, volume)

---

### 6.3 Fill Page

Shown automatically when a fill starts.

**Controls:**
- **STOP** button — stops the pump immediately
- **Speed slider** — adjusts fill target rate (50–3000 ml/min) in real time. The model's saved fill speed is restored on the next fill.

**Live data:**
- Flow rate bar and ml/min value
- Fill progress bar (volume filled vs. target)
- Volume filled / target volume text (e.g. `312 / 450 ml`)
- Supply tank bar (with threshold marker)
- Battery bar

**Status messages** during fill:
| Message | Meaning |
|---------|---------|
| `Draining before Refueling` | Auto-fill drain phase (models without sensor) |
| `Filling` | Actively filling |
| `Purging...` | Overflow purge running after fill complete |
| `Complete` | Fill finished successfully |
| `Tank already full` | Tank-full sensor triggered before fill started |
| `Connect Tank Full Sensor` | Sensor model selected but cable not plugged in |
| `Sensor lost — fill stopped!` | Sensor cable pulled during fill |
| `Supply tank empty — fill stopped!` | Supply ran out mid-fill |
| `BLOCKED LINE - fill stopped!` | Blocked-line failsafe tripped |
| `Check filter - pump working hard` | Filter-wear warning |
| `LOW BATTERY!` | Battery cutoff latched, pump disabled |

---

### 6.4 Drain Page

Shown automatically when a standalone drain starts.

**Controls:**
- **STOP** button
- **Speed slider** — adjusts drain target rate in real time

**Live data:**
- Flow rate bar and ml/min
- Drain progress bar (volume drained, counts up from the right — bar shrinks as tank empties)
- Volume drained text
- Supply tank and battery bars

**Status messages** during drain:
| Message | Meaning |
|---------|---------|
| `Draining` | Actively draining |
| `Tank empty` | Drain completed normally — empty detected |
| `Tank was empty` | Drain started but flow never exceeded 200 ml/min (tank was already empty) |
| `BLOCKED LINE - drain stopped!` | Blocked-line failsafe |
| `Check filter - pump working hard` | Filter wear warning |

---

### 6.5 Setup Page

Model configuration. All changes save immediately to the device.

**Model selector:**
- Drop-down list of all saved models
- **+ New Model** button — creates a model with a default name
- **Delete** button — permanently removes the selected model and its photo

**Model parameters:**

| Field | Range | Description |
|-------|-------|-------------|
| Name | ≤ 23 chars | Displayed on screen and in the model browser |
| Tank Volume | ≥ 100 ml | The model fuel tank capacity. Used as fill target and drain reference. |
| Fill Speed | 50–3000 ml/min | Target flow rate for filling. Adjustable live on the fill page. |
| Drain Speed | 50–3000 ml/min | Target flow rate for draining. |
| Tank Sensor | YES / NO | Whether this model uses the tank-full sensor. Set NO for models that are filled by volume only; the auto-drain-then-fill sequence will run automatically. |
| Purge Time | 0–30 s | Seconds to run the pump in reverse (drain direction) after a fill completes. Used to clear fuel from the fill line so it doesn't drip. Set 0 to skip. |
| Photo | JPEG | Upload a photo of the helicopter. Shown as thumbnail on screen and web. |

**Flow calibration shortcut** (in Setup):
- Start / Stop / Commit buttons to run the automatic calibration procedure (described in [Section 14](#14-flow-sensor-calibration)).

---

### 6.6 Station Page

Global station settings, calibration, and statistics.

**Supply Tank:**
| Field | Description |
|-------|-------------|
| Capacity | Total volume of the supply tank in ml. Default 20,000 ml (20L). |
| Low Threshold | Volume at which the supply bar turns yellow. Set this to the minimum you want to keep on hand. Default 2,000 ml (2L). The bar turns red below half this threshold. |
| Full Refill | Resets the remaining volume counter to the full capacity. Press this after physically refilling the supply tank. |

**Flow Sensor Calibration:**
| Field | Description |
|-------|-------------|
| Fill cal (pulses/L) | Measured pulses per litre for the fill sensor. Default 1696. |
| Drain cal (pulses/L) | Measured pulses per litre for the drain sensor. Default 1696. |
| Set manually | Type a known value and apply directly. |
| Automatic cal | Start pump, collect a measured volume in a jug, stop, enter the ml. System calculates pulses/litre automatically. |

**Tank Empty Detection:**
| Field | Range | Description |
|-------|-------|-------------|
| Flow drop % | 5–90% | How far flow must drop below its peak before the tank is considered empty. Default 30%. Lower values = more sensitive. |
| Min run time | 1–60 s | How many seconds the pump must have been running before empty detection activates. Prevents false trips during ramp-up. Default 8 s. |

**Battery:**
| Field | Description |
|-------|-------------|
| Cutoff V/cell | Per-cell voltage at which the low battery latch triggers. Default 3.82V. Range 3.0–4.2V. |
| Clear latch | If the battery latch has fired, pressing this clears it so you can continue (only do this if you have verified the battery is safe). |

**Station Statistics:**
- Lifetime fills, drains, fill volume and drain volume across all models
- Per-model totals visible on the Setup page model stats section

---

### 6.7 OTA Update Page

Accessed at `http://fuelstation.local/ota`.

Shows stored firmware slots (up to 3 versions kept). Each slot shows the filename, size, and upload time.

**To update firmware:**
1. Download or build a new `.bin` file
2. Click **Choose file** and select the firmware binary
3. Click **Upload & Install** — the file uploads (progress bar shown), then installs automatically
4. The station reboots in ~3 seconds

**Version history:** previously installed versions remain in slots 1 and 2. Click **Install** on any slot to roll back.

**From PlatformIO** (developer workflow):
```
pio run -e esp32-s3-ota --target upload       # firmware
pio run -e esp32-s3-ota --target uploadfs     # web files (index.html etc.)
```

---

### 6.8 Event Log Page

Accessed at `http://fuelstation.local/log`.

A searchable, filterable table of all events. The log is stored in CSV format in the device filesystem.

**Columns:**
| Column | Content |
|--------|---------|
| Time | Date and time from the RTC (synced via NTP) |
| Level | INFO / WARN / ERROR / FAULT |
| Category | SYSTEM / PUMP / SENSOR / POWER / NETWORK |
| Event | Event code (see [Section 19](#19-log-event-reference)) |
| Detail | Free text — model name, stop reason, etc. |
| Value 1 | First numeric value where applicable |
| Value 2 | Second numeric value where applicable |

**Filters:** click any level or category button to show only those entries. Text search filters all columns simultaneously.

**Rotation:** when the log exceeds 128 KB it is renamed to `log_old.csv` and a new file starts. Both files are accessible from the log page. The archive button downloads `log_old.csv`.

---

## 7. Helicopter Models

Up to **20 models** can be stored. Each is saved as a JSON file in `/models/<name>/model.json` on the device filesystem.

### Adding a Model
1. Open the web Setup page
2. Click **+ New Model**
3. Edit the name, tank volume, speeds, sensor setting, and purge time
4. Optionally upload a photo

### Selecting a Model (Screen)
1. From HOME, scroll to **MODEL** and press the encoder
2. In the model browser, scroll to the desired model
3. Press the encoder — the screen returns to HOME with the new model active

### Selecting a Model (Web)
- Use the model drop-down on the Setup page, or
- Click a model name in the model list on the Main page

### Tank Sensor Setting
This is the most important parameter per model.

| Setting | Behaviour |
|---------|-----------|
| **YES** | The physical tank-full sensor must be connected before a fill starts. Filling stops automatically when the sensor triggers. If the sensor is not connected when FILL is pressed, the display shows *Connect Tank Full Sensor* and waits. |
| **NO** | No sensor required. The system uses the **Auto Fill Sequence** (drain-first, then fill to the set volume). See [Section 10](#10-auto-fill-sequence-no-tank-sensor). |

---

## 8. Fill Operation

### Starting a Fill
**Screen:** scroll to **FILL** on HOME, press the encoder.
**Web:** on the Fill page click **START FILL** (or the same button on the Main page).

### What Happens
1. The pump ramps up to `MIN_PWM` (about 5 seconds to reach fill speed).
2. The closed-loop PI controller adjusts PWM every 500 ms to hit the target flow rate.
3. Fill volume is tracked from the flow sensor pulse count.
4. Supply volume is decremented in real time.

### Auto-Stop Conditions
The pump stops automatically under any of these conditions:

| Condition | Action |
|-----------|--------|
| **Tank-full sensor triggers** (sensor=YES) | Pump stops, overflow purge starts if configured |
| **Target volume reached** | Pump stops, overflow purge starts if configured |
| **Supply tank empty** | Pump stops, message: *Supply tank empty — fill stopped!* |
| **Tank sensor lost mid-fill** (sensor=YES) | Pump stops immediately, message: *Sensor lost — fill stopped!* |
| **Blocked line** (see Section 13) | Pump stops, message: *BLOCKED LINE - fill stopped!* |
| **Low battery** | Pump stops, message: *LOW BATTERY!* |

### Overflow Purge
If **Purge Time > 0** for the model, after the fill stop the pump reverses for the configured number of seconds (drain direction) to draw any fuel remaining in the fill line back into the supply tank. The message bar shows `Purging...` in orange during this phase. Pump speed during purge uses the model's drain speed.

### Adjusting Speed During Fill
- **Screen:** rotate the encoder — speed changes in 50 ml/min steps and is saved to the model
- **Web:** move the speed slider — sends the new target immediately

### After Fill Stops
The screen shows **START** and **BACK** buttons in the left panel.
- **START** (encoder scroll to select, then press): restarts the same fill
- **BACK**: returns to the HOME idle state

---

## 9. Drain Operation

### Starting a Drain
**Screen:** scroll to **DRAIN** on HOME, press the encoder.
**Web:** on the Drain page click **START DRAIN**.

### What Happens
1. Pump ramps up in drain direction.
2. Flow is tracked via the drain flow sensor.
3. Peak flow is recorded. The system watches for the flow to drop sharply below peak.
4. The drain bar counts from 100% down to 0% as volume increases.
5. Supply volume is incremented (fuel returns to supply).

### Empty Detection
The drain stops automatically when any of these conditions are met:

| Condition | Description |
|-----------|-------------|
| **Flow drop** | Measured flow drops below `peak × (1 - flowDrop%)` for 4 consecutive readings (2 seconds). Default threshold: 30% drop. |
| **Never reached flow** | After the minimum run time, if peak flow never exceeded 200 ml/min the tank was likely already empty. |

The minimum run time (default 8 s) prevents false detection during pump ramp-up. Increase this if your pump is slow to reach speed.

**Tuning empty detection:** if the drain stops too early (tank still has fuel) increase the **Flow drop %** setting. If it runs too long decrease it. Accessible on the Station web page.

### After Drain Stops
Same as fill — START and BACK appear. START re-runs the drain.

---

## 10. Auto Fill Sequence (No Tank Sensor)

When a fill is started for a model with **Tank Sensor = NO**, the station runs a three-phase automatic sequence:

```
Phase 1: DRAIN    →    Phase 2: FILL    →    Phase 3: PURGE (if configured)
```

**Why drain first?** Without a tank sensor the station cannot know how much fuel is already in the tank. Draining first ensures the tank is empty, giving a known starting point so the fill volume is accurate.

### Sequence Detail

| Phase | Display message | What happens |
|-------|----------------|-------------|
| **Pending** (0.5 s pause) | `Draining before Refueling` | Brief pause before drain begins |
| **Draining** | `Draining before Refueling` | Pump runs in drain direction until empty detection confirms the tank is empty. The fill page is shown (not the drain page) and the STOP button is visible. |
| **Transition** (2 s pause) | `Drain done — filling...` or `Already empty — filling...` | Short pause before fill begins. *Already empty* appears if the tank was already empty at the start of drain. |
| **Filling** | `Filling` | Standard fill to target volume |
| **Purging** | `Purging...` | If purge time > 0, pump reverses briefly |
| **Complete** | `Complete` | Sequence done |

The STOP button is visible throughout the entire sequence. Pressing it at any point aborts immediately.

---

## 11. Supply Tank Management

The station tracks fuel in the supply tank by measuring the volume moved by the flow sensors.

### Parameters
| Parameter | Description |
|-----------|-------------|
| **Capacity** | Total volume when full. Default 20,000 ml (20L). Set this once to match your actual supply tank. |
| **Remaining** | Current estimated volume. Updated after every fill (decremented) and drain (incremented). |
| **Low Threshold** | Volume at which warnings begin. Default 2,000 ml (2L). |

### Colour Coding (Screen and Web)
| Condition | Bar colour |
|-----------|-----------|
| Remaining > Low Threshold | Green |
| Remaining between Low Threshold and ½ × Low Threshold | Yellow |
| Remaining < ½ × Low Threshold | Red |

A white tick mark on both the screen bar and web bar shows exactly where the low threshold sits.

### After Refilling the Supply Tank
After you physically refill the supply tank:

**Screen:** scroll to **RESET SUPPLY** (left panel, idle) and press the encoder.
**Web:** on the Station page, click **Full Refill** next to the supply tank section.

Both actions set the remaining volume to the full capacity and save immediately.

> **Accuracy note:** the supply volume is calculated from flow sensor pulses. Calibration errors accumulate over time. Periodically reset to full after a physical refill to keep the counter accurate.

---

## 12. Battery Management

### Cell Count Detection
On boot the station reads the battery voltage and automatically determines whether a 2S or 3S LiPo is connected (based on voltage thresholds). The detected count is used for per-cell voltage calculations and percentage lookups.

### Percentage Lookup
Battery percentage uses the standard LiPo discharge curve, not a linear voltage map. A fully charged 3S reads ~12.6V (100%), nominal is ~11.1V (50%), and 10.5V is empty (0%).

### Low Battery Latch
If per-cell voltage stays at or below the cutoff threshold (default **3.82V/cell**) for 3 consecutive seconds (6 × 500 ms readings), the low battery latch fires:

1. Pump stops immediately if running
2. Message: `LOW BATTERY!`
3. The pump is **disabled** — FILL and DRAIN will not start
4. Web page shows a low battery warning

The latch has hysteresis: the voltage must recover to `cutoff + 0.05V/cell` before the count resets (preventing rapid latch/unlatch on a sagging battery).

**To clear the latch** (only after verifying the battery is safe):
- **Web:** Station page → Clear latch button
- Command code `7018`

### Cutoff Voltage
Default 3.82V/cell. Adjustable on the Station web page (range 3.0–4.2V). Set lower (e.g. 3.6V) if you want maximum run time; set higher (e.g. 3.9V) for a conservative cutoff that preserves battery health.

---

## 13. Pump Failsafes

Two independent automatic failsafes protect against pump damage and fuel spills.

### Blocked Line Detection

**Purpose:** stops the pump if the fuel line becomes kinked, pinched, or capped — preventing pump burnout.

**How it works:**
- Monitors PWM level and measured flow rate simultaneously
- If PWM ≥ 90% of maximum AND flow rate < 100 ml/min for **8 consecutive seconds**: pump stops
- Fires during both fill and drain operations (not during calibration or overflow purge)

**Screen message:** `BLOCKED LINE - fill stopped!` or `BLOCKED LINE - drain stopped!`
**Log event:** `FILL_BLOCKED` or `DRAIN_BLOCKED` (level FAULT)
**Log values:** actual PWM at time of stop, measured flow rate

**What to do:** check the fuel line for kinks, check fittings, check the pump head. Clear the blockage then restart.

### Filter Wear Warning

**Purpose:** warns when the inline fuel filter is becoming clogged, causing the pump to work harder than expected for the same flow rate.

**How it works:**
- After 10 seconds of running (to allow the closed loop to settle)
- Compares actual PWM to what the calibration says should produce the target flow rate
- If actual PWM > expected PWM × 1.4 (40% higher than expected): warning fires once per session

**Screen message:** `Check filter - pump working hard`
**Log event:** `FILTER_WARN` (level WARN)
**Log values:** actual PWM, expected PWM

**What to do:** the pump continues running after this warning. It is advisory only. Inspect and replace the fuel filter at your next opportunity.

---

## 14. Flow Sensor Calibration

Both flow sensors (fill and drain) have independent calibration values stored as **pulses per litre** (default 1696.0 for both).

Calibration needs to be done:
- On first setup
- After replacing a flow sensor
- If measured volumes are consistently off

### Automatic Calibration (Recommended)

This procedure uses a measuring jug to establish the true pulses/litre ratio.

**Fill sensor calibration (web):**
1. Station page → Flow Calibration → Fill sensor → **Start Cal**
2. Direct the fill outlet into a clean measuring jug
3. The pump starts and counts pulses
4. When enough fuel has flowed (aim for 200–500 ml for best accuracy), click **Stop Cal**
5. Enter the volume you collected in ml
6. Click **Commit** — the new pulses/litre value is calculated and saved

**Drain sensor calibration:**
Same process using the Drain cal section. The pump drains in the drain direction.

### Manual Calibration
If you know the correct pulses/litre value from a previous calibration or data sheet, enter it directly in the manual field and press **Set**.

### Verification
After calibration, run a measured fill (e.g. into a known container) and check the reported volume against your measurement. For ±2% accuracy the calibration should be stable over many sessions.

---

## 15. Pump Speed Calibration

The closed-loop PI controller uses a linear model relating PWM to flow rate in ml/min. This model is stored as **mlPerMinPerPwm** (fill) and **drainMlPerMinPerPwm** (drain).

These values are updated automatically in the background as the PI controller operates — the system learns from each session. In normal use you do not need to set these manually.

If the controller is sluggish (takes a long time to reach target flow) or oscillates:
- Check that the flow sensor is calibrated correctly first
- Verify the fuel line is not kinked
- The PI gains (KP=0.015, KI=0.010) are fixed in firmware

---

## 16. Standby and Auto-Shutdown

| Event | Delay | Behaviour |
|-------|-------|-----------|
| **Display standby** | 10 minutes inactivity | Screen dims, buzzer plays standby tone |
| **Auto-shutdown** | 15 minutes inactivity | Data saved, buzzer plays shutdown tone, power off |

**Activity** is defined as any encoder movement or button press. A running pump does not reset the timer — the screen may go into standby during a long operation.

**Waking from standby:** any encoder turn or short press wakes the display. The first encoder turn during standby only wakes — it does not scroll.

**Shutdown save:** before power is cut the station saves all model data, station config, and supply volume to flash. No data is lost on clean shutdown.

---

## 17. OTA Firmware Update

The station supports wireless firmware updates without a USB cable.

### From a Web Browser
1. Build a new firmware `.bin` file in PlatformIO
2. Navigate to `http://fuelstation.local/ota`
3. Click **Choose file**, select the firmware binary
4. Click **Upload & Install**
5. The station reboots automatically after ~3 seconds

### From PlatformIO
With the station on the same network:
```bash
# Firmware only
pio run -e esp32-s3-ota --target upload

# Web files only (index.html, images)
pio run -e esp32-s3-ota --target uploadfs

# Both in sequence
pio run -e esp32-s3-ota --target upload && pio run -e esp32-s3-ota --target uploadfs
```

The OTA system stores up to 3 firmware versions on the filesystem. You can roll back to a previous version from the OTA web page.

### Power During OTA
Do not power off the station during an OTA upload. A failed upload (partial write) is safe — the old firmware remains installed until the install step completes successfully.

---

## 18. Troubleshooting

### Station won't power on
- Check the battery voltage. The Pololu power regulator requires sufficient input voltage.
- Check the battery connector orientation.
- Try a long press (8+ seconds) to ensure it's not in a deep sleep state.

### No flow / flow reads zero
1. Check the flow sensor connectors — both fill and drain sensors must be connected.
2. Run a manual fill for 10+ seconds and check the log for `FILL_START` and flow values.
3. If flow stays at 0 ml/min with the pump running: the sensor is not generating pulses. Check the sensor cable and sensor wheel.
4. If flow reads very high or very low for a known volume: recalibrate (Section 14).

### Fill stops immediately with "Tank already full"
- The tank-full sensor has detected a full state before the fill started.
- Check whether the sensor is stuck or wet. Blow it clear.
- If the model should not use a sensor, change Tank Sensor to NO on the Setup page.

### Fill stops immediately with "Connect Tank Full Sensor"
- The model is set to use a tank sensor but the sensor shell detect wire is not connected.
- Plug in the tank sensor connector. The fill will start automatically within 150 ms of the sensor being detected (no need to press anything again).

### Fill stops with "Sensor lost"
- The sensor cable was unplugged or lost contact during filling.
- Check the cable and connector. The stop is a safety measure — fuel stops immediately to prevent overflow.

### Fill stops with "BLOCKED LINE"
- The fuel line is blocked. Common causes: kinked line, closed valve, tight bend, air lock in the pump head.
- Check the entire fuel path from supply to tank inlet.
- Log entry `FILL_BLOCKED` will show the PWM and flow values at the time of stop.

### "Check filter" warning appears
- The filter is restricting flow — the pump is working 40%+ harder than calibrated for the current flow rate.
- Replace or clean the inline fuel filter.
- This is a warning only — the fill continues.

### Drain stops too early / tank still has fuel
- Increase **Flow drop %** on the Station web page (e.g. from 30% to 40%).
- Ensure the minimum run time is long enough for the pump to reach full speed (increase if pump is slow to start).
- Check log for `DRAIN_COMPLETE` — the *Value 2* field shows peak flow at the time of stop.

### Drain never stops / runs forever
- Flow drop % may be too low (below the natural fluctuation in your sensor).
- Check that the drain flow sensor is working (should show > 0 ml/min once running).
- Decrease **Flow drop %** (e.g. from 30% to 20%).

### Auto-fill sequence: fill starts but tank isn't empty
- The drain phase may have ended on the blocked-line failsafe rather than empty detection.
- Check the log for `DRAIN_COMPLETE` vs `DRAIN_BLOCKED` during the auto sequence.
- Ensure the drain line path is clear.

### Supply volume drifts / reads wrong
- Calibration error has accumulated. Reset the supply to full after physically refilling and re-calibrate the flow sensors.
- Check that `supplyCapMl` is set correctly for your actual tank size (Station web page).

### Web interface shows no data / stuck
- Refresh the browser page (hard refresh: Ctrl+Shift+R).
- Check the WebSocket connection indicator at the top of the page (green dot = connected).
- If the station restarted, the WebSocket reconnects automatically within a few seconds.

### Clock shows wrong time
- NTP sync requires home WiFi. Check the Network screen — home WiFi must show CONNECTED.
- If the RTC battery is depleted the clock resets to a default time on each boot. The log event `RTC_FAIL` will appear if the DS3231 is not found.
- Once NTP syncs the clock is set and maintained by the RTC even without WiFi.

### "LOW BATTERY!" and pump won't start
- The battery is at or below the cutoff threshold. Charge the battery.
- If the battery is healthy and this appears incorrectly, check the ADC divider connections (GPIO1).
- Adjust cutoff on Station page if needed, then clear the latch.

### Station rebooted unexpectedly
- Check the log for `PREV_CRASH` (FAULT level) — indicates a panic reset.
- Check for `BROWNOUT` — indicates the supply voltage collapsed, usually a bad battery or connector.
- `PUMP_INTERRUPTED` means the station was powered off or crashed while the pump was running. The log shows which model and how much had been pumped.

---

## 19. Log Event Reference

The log is accessible at `http://fuelstation.local/log` and is stored in `/logs/log.csv` on the device filesystem. A second file `/logs/log_old.csv` holds the previous rotation.

### Log Levels
| Level | Meaning |
|-------|---------|
| `INFO` | Normal operation |
| `WARN` | Degraded condition or advisory |
| `ERROR` | Recoverable fault |
| `FAULT` | Serious fault — crash, hardware failure, safety stop |

---

### SYSTEM Category

| Event | Level | Detail | Val1 | Val2 | Meaning |
|-------|-------|--------|------|------|---------|
| `BOOT` | INFO | `v2.7.x / REASON` | Free heap (KB) | | Normal boot. Reset reason shown in detail. |
| `PREV_CRASH` | FAULT | | | | Previous boot ended in a panic/crash. |
| `BROWNOUT` | FAULT | | | | Power supply voltage collapsed. |
| `SHUTDOWN` | INFO | | Pack voltage | | Clean power-off via encoder hold. |
| `NTP_SYNC_OK` | INFO | | | | RTC synchronised from internet time. |
| `NTP_SYNC_FAIL` | WARN | | | | NTP failed — no internet or WiFi not connected. |
| `SUPPLY_CAP_SET` | INFO | | New cap (ml) | | Supply capacity changed via web. |
| `SUPPLY_LOW_SET` | INFO | | New threshold (ml) | | Supply low threshold changed. |
| `SUPPLY_RESET` | INFO | `web` or `screen` | | | Supply reset to full. |
| `MODEL_TANK_SET` | INFO | Model name | New volume (ml) | | Tank volume changed for a model. |
| `MODEL_FILL_SPD` | INFO | Model name | New speed | | Fill speed saved for model. |
| `MODEL_DRAIN_SPD` | INFO | Model name | New speed | | Drain speed saved for model. |
| `MODEL_SENSOR_SET` | INFO | Model name | 0 or 1 | | Tank sensor setting changed. |
| `MODEL_PURGE_SET` | INFO | Model name | Seconds | | Purge time changed. |
| `MODEL_SELECT` | INFO | Model name | | | Active model changed. |

---

### PUMP Category

| Event | Level | Detail | Val1 | Val2 | Meaning |
|-------|-------|--------|------|------|---------|
| `FILL_START` | INFO | Model name | Target (ml) | | Fill started. |
| `FILL_STOP` | INFO | Stop reason | Volume pumped (ml) | | Fill stopped normally. Reasons: MANUAL, WEB, ENCODER, BACK\_HOLD, CAL\_STOP, SUPPLY\_EMPTY |
| `FILL_COMPLETE` | INFO | Reason | Volume pumped (ml) | | Fill reached its target. Reasons: VOLUME\_REACHED, TANK\_FULL |
| `FILL_BLOCKED` | FAULT | Model name | PWM at stop | Flow at stop (ml/min) | Blocked-line failsafe tripped during fill. |
| `DRAIN_START` | INFO | Model name | Target (ml) | | Drain started. |
| `DRAIN_STOP` | INFO | Stop reason | Volume drained (ml) | | Drain stopped manually. |
| `DRAIN_COMPLETE` | INFO | Model name | Volume drained (ml) | Peak flow (ml/min) | Drain completed — empty detected normally. |
| `DRAIN_EMPTY` | WARN | Model name | Volume drained (ml) | | Tank was already empty (peak flow < 200 ml/min). |
| `DRAIN_BLOCKED` | FAULT | Model name | PWM at stop | Flow at stop (ml/min) | Blocked-line failsafe tripped during drain. |
| `FILTER_WARN` | WARN | Model name | Actual PWM | Expected PWM | Pump working harder than calibrated — check filter. |
| `AUTO_FILL_SEQ` | INFO | Model name | Tank volume | | Auto fill sequence started (no-sensor model). |
| `AUTO_FILL_DRAIN` | INFO | Model name | | | Drain phase of auto sequence starting. |
| `AUTO_FILL_FILL` | INFO | Model name | | | Fill phase of auto sequence starting. |
| `AUTO_FILL_PURGE` | INFO | Model name | Purge secs | Purge PWM | Purge phase starting. |
| `AUTO_FILL_DONE` | INFO | Model name | Volume filled (ml) | | Auto sequence complete. |
| `PUMP_INTERRUPTED` | WARN | Op · Model · Target | Pumped so far (ml) | | Station was power-cycled while pump was running. Logged on next boot. |

---

### SENSOR Category

| Event | Level | Detail | Val1 | Val2 | Meaning |
|-------|-------|--------|------|------|---------|
| `RTC_FAIL` | ERROR | | | | DS3231 RTC not found on I2C bus at boot. |
| `FILL_NO_SENSOR` | WARN | Model name | | | Fill requested but tank sensor not connected. |
| `FILL_SENSOR_OK` | INFO | Model name | | | Sensor plugged in — fill auto-started. |
| `FILL_SENSOR_LOST` | WARN | Model name | Volume at stop (ml) | | Sensor disconnected during fill — safety stop. |
| `FILL_CAL_START` | INFO | Model name | | | Fill sensor calibration started. |
| `FILL_CAL_STOP` | INFO | | Pulse count | | Fill cal stopped — pulses recorded. |
| `FILL_CAL_COMMIT` | INFO | | New cal (ppl) | | Fill calibration value saved. |
| `FILL_CAL_MANUAL` | INFO | | New cal (ppl) | | Fill calibration set manually. |
| `DRAIN_CAL_START` | INFO | Model name | | | Drain sensor calibration started. |
| `DRAIN_CAL_STOP` | INFO | | Pulse count | | Drain cal stopped. |
| `DRAIN_CAL_COMMIT` | INFO | | New cal (ppl) | | Drain calibration value saved. |
| `DRAIN_CAL_MANUAL` | INFO | | New cal (ppl) | | Drain calibration set manually. |

---

### POWER Category

| Event | Level | Detail | Val1 | Val2 | Meaning |
|-------|-------|--------|------|------|---------|
| `BATT_LOW` | WARN | `Pump disabled` | Pack voltage | Cell voltage | Low battery latch fired. |
| `BATT_LATCH_CLEARED` | INFO | `web` | | | Low battery latch manually cleared. |
| `CUTOFF_SET` | INFO | | New cutoff (V/cell) | | Battery cutoff voltage changed. |

---

### NETWORK Category

| Event | Level | Detail | Val1 | Val2 | Meaning |
|-------|-------|--------|------|------|---------|
| `WIFI_CONNECTED` | INFO | STA IP address | | | Station joined home WiFi. |

---

### Reading Logs for Common Problems

**"Pump keeps cutting out during fill"**
Search for `FILL_STOP` entries. The Detail column will show the stop reason. If it is `SUPPLY_EMPTY` the supply ran out; if `MANUAL` or `ENCODER` the button was pressed. If you see `FILL_BLOCKED` the line was blocked at the time shown.

**"Fill consistently delivers less than the target volume"**
Compare `FILL_COMPLETE` Val1 against the target in `FILL_START`. If they differ check `FILL_CAL_COMMIT` — the calibration value in use may be off. Run a fresh calibration.

**"Station crashed overnight"**
Look for `PREV_CRASH` or `BROWNOUT` on the next boot entry. If `PUMP_INTERRUPTED` appears, the pump was running when power was lost — check what it was doing and verify no fuel spilled.

**"Drain keeps running past empty"**
Find `DRAIN_COMPLETE` entries. Val2 is the peak flow at detection. If peak flow was low (< 300 ml/min) the sensor may not be responding. Check `DRAIN_CAL_COMMIT` to verify the drain sensor is calibrated.

**"Auto-fill sequence started fill phase but tank wasn't empty"**
Look for the preceding `AUTO_FILL_DRAIN` entry, then search for what stopped the drain: `DRAIN_COMPLETE` (normal), `DRAIN_BLOCKED` (blocked line, tank may not be empty), or `DRAIN_EMPTY` (tank was already empty — fine). If `DRAIN_BLOCKED` appears the drain stopped early due to a blocked line rather than true empty detection.

---

*MCP Fuel Station V2 — v2.7.8 — github.com/USAheliman/MCP-Fuel-Station-V2*
