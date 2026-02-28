# Motion / Planner & Feature Overview

This repository contains the **firmware + web UI** for a wall/plotter-style machine (ESP32-based) with a strong focus on **smooth, accurate motion** and a **guided browser workflow** (setup → preview → run).

In short:  
You upload **SVG/PNG**, the web UI prepares/visualizes the job, the firmware converts it into motion tasks, and the motion system drives the steppers with **lookahead planning** plus **on-the-fly smoothing** so corners, small circles, and tight details don’t look like a shaky mess.

---

## What this is

A complete plotting stack consisting of:

- **Firmware (ESP32 / PlatformIO / Arduino)**  
  Controls motors, motion planning, pen/servo, file handling (LittleFS + SD), job execution, logs, and system endpoints.

- **Web UI (served by the ESP32)**  
  Provides a “slides” style workflow for calibration and job execution, plus preview/simulation tools and PNG/SVG processing options.

The main goal is **better drawing quality**:
- fewer vibrations and corner overshoot  
- controlled speed in micro-movements  
- reduced segment noise and overhead  
- predictable acceleration behavior  

---

## Motion system highlights

The motion pipeline is optimized in **two layers**:

### 1) Runner Lookahead Planner (predictive planning)
The Runner buffers and analyzes upcoming segments to compute stable transition speeds.

Key features:
- **Lookahead queue** (`lookaheadSegments`, default 48)  
  Plans multiple segments ahead for smoother speed transitions.
- **GRBL-like junction deviation** (`junctionDeviationMM`)  
  Calculates safe corner speed based on angle and acceleration.
- **Angle-based corner slowdown** (`cornerSlowdown`, `minCornerFactor`)  
  Prevents “hard brake to zero” while still protecting corners.
- **Arc segmentation (G2/G3)**  
  Converts arcs to small line segments with bounded chord error.  
  Optional **protect points** prevent important detail from being merged away later.
- **Segment cleanup** (`minSegmentLenMM`)  
  Drops tiny noise segments.
- **Collinear merging** (`collinearDeg`)  
  Merges near-straight sequences to reduce micro-segments and stepper overhead.

### 2) Movement::beginLinearTravel() (on-the-fly smoothing)
Applies local corrections right before executing a segment.

Key features:
- **Backlash compensation** (`backlashXmm`, `backlashYmm`)  
  Compensates mechanical play when direction flips.
- **Dynamic corner factor** (`lastSegmentDX`, `lastSegmentDY`)  
  Reduces speed depending on how strong the direction change is.
- **Micro-segment speed limiter** (`microSlowLenMM`, `microMinFactor`)  
  Prevents tiny segments from being executed “too fast”.
- **Minimum segment time clamp** (`minSegmentTimeMs`)  
  Avoids unrealistic high-speed bursts on very short moves.
- **Pseudo S-curve** (`sCurveFactor`)  
  Locally reduces acceleration near corners to reduce jerk and resonance.
- **FastAccelStepper backend**  
  Movement runs through a fast stepper backend with proper ramp handling.

---

## Web UI workflow (slides)

The UI guides you through typical setup steps:

1. **Reference / retract belts**  
   Manual left/right motor control.
2. **Set top distance**  
   Define anchor spacing.
3. **Extend to home**  
   Move the machine to the base position.
4. **Pen / servo calibration**  
   Slider + +/- controls.
5. **Load image**  
   Upload `.svg` or `.png`.
6. **Choose renderer**  
   - Path tracing  
   - Vector → Raster → Vector
7. **Preview + simulation**  
   Preview image, progress bar, distance display, canvas simulation with play/pause.

---

## PNG features

PNG processing supports multiple modes:

- **Black/White** (legacy)
- **Color layers** (new)  
  Detects color layers and supports painting them sequentially.
- **Direct raster** (no vector conversion)  
  Raster-driven plotting mode.

Tools:
- **Color tolerance** to group similar colors  
- **Speckle removal** (minimum pixel threshold)  
- **Raster resolution** (`step px`) control  
- **Paint layers sequentially** button

---

## SVG batch / series mode

- Select a folder on SD and run multiple SVG jobs sequentially via the frontend.

---

## Firmware API (run control)

Core endpoints:
- `/run` – start job  
- `/resume` – continue after pause  
- `/status` – status/progress  
- `/command` – send commands  
- `/setSpeeds` – print/move speed  
- `/extendToHome` – homing/base position  
- `/setServo` – direct servo control  
- `/setPenDistance` – pen parameters  
- `/setTopDistance` – set top distance  
- `/getState` – current state

Diagnostics/system:
- `/logs` – ringbuffer/weblog  
- `/sysinfo` – system info  
- `/diag` – diagnostics endpoint  
- `/reboot` – reboot

Filesystem (LittleFS + SD):
- `/fs/info`, `/sd/remount`, `/fs/list`, `/fs/read`, `/fs/download`  
- `/fs/delete`, `/fs/mkdir`, `/fs/rename`, `/fs/copy`, `/fs/move`  
- `/uploadCommands`, `/downloadCommands`

Driver / step signal:
- `/pulseWidths` (GET)  
- `/setPulseWidths` (POST)  
- `/gpio/driverEnable` (GET/POST)

---

## Why it exists

Plotters fail in the same boring ways:
- corners overshoot and ring  
- tiny circles become polygons at insane speed  
- segment noise creates jitter  
- stepper backend gets flooded with micro-moves  

This project exists to fix that with:
- lookahead planning  
- junction-aware corner control  
- segment cleanup + merging  
- micro-move speed limiting  
- more stable acceleration handling  

---

## License / Notes

Add your license, build instructions, and hardware wiring notes here. TMC2209 Nema 17,  MKS DLC32 V2.1 
# Motion / Planner & Feature Overview

This repository contains the **firmware + web UI** for a wall/plotter-style machine (ESP32-based) with a strong focus on **smooth, accurate motion** and a **guided browser workflow** (setup → preview → run).

In short:  
You upload **SVG/PNG**, the web UI prepares/visualizes the job, the firmware converts it into motion tasks, and the motion system drives the steppers with **lookahead planning** plus **on-the-fly smoothing** so corners, small circles, and tight details don’t turn into mechanical chaos.

---

## What this is

A complete plotting stack consisting of:

- **Firmware (ESP32 / PlatformIO / Arduino)**  
  Controls motors, motion planning, pen/servo, file handling (LittleFS + SD), job execution, logs, and system endpoints.

- **Web UI (served by the ESP32)**  
  Provides a “slides” style workflow for calibration and job execution, plus preview/simulation tools and PNG/SVG processing options.

The main goal is **better drawing quality**:
- fewer vibrations and corner overshoot  
- controlled speed in micro-movements  
- reduced segment noise and overhead  
- predictable acceleration behavior  

---

## Hardware target (typical build)

This project is designed around a common DIY motion stack:

- **Stepper motors:** NEMA17  
- **Drivers:** TMC2209  
- **Controller board:** **MKS DLC 32 v2.1** (a.k.a. MKS DLC32 2.1)

Notes:
- The firmware focuses on generating clean motion behavior; the exact pin mapping / wiring depends on your board setup.
- TMC2209 is sensitive to timing and resonance at certain speeds, which is why the motion pipeline includes micro-segment limiting, junction planning, and local accel shaping.

---

## Motion system highlights

The motion pipeline is optimized in **two layers**:

### 1) Runner Lookahead Planner (predictive planning)
The Runner buffers and analyzes upcoming segments to compute stable transition speeds.

Key features:
- **Lookahead queue** (`lookaheadSegments`, default 48)  
  Plans multiple segments ahead for smoother speed transitions.
- **GRBL-like junction deviation** (`junctionDeviationMM`)  
  Calculates safe corner speed based on angle and acceleration.
- **Angle-based corner slowdown** (`cornerSlowdown`, `minCornerFactor`)  
  Prevents “hard brake to zero” while still protecting corners.
- **Arc segmentation (G2/G3)**  
  Converts arcs to small line segments with bounded chord error.  
  Optional **protect points** prevent important detail from being merged away later.
- **Segment cleanup** (`minSegmentLenMM`)  
  Drops tiny noise segments.
- **Collinear merging** (`collinearDeg`)  
  Merges near-straight sequences to reduce micro-segments and stepper overhead.

### 2) Movement::beginLinearTravel() (on-the-fly smoothing)
Applies local corrections right before executing a segment.

Key features:
- **Backlash compensation** (`backlashXmm`, `backlashYmm`)  
  Compensates mechanical play when direction flips.
- **Dynamic corner factor** (`lastSegmentDX`, `lastSegmentDY`)  
  Reduces speed depending on how strong the direction change is.
- **Micro-segment speed limiter** (`microSlowLenMM`, `microMinFactor`)  
  Prevents tiny segments from being executed “too fast”.
- **Minimum segment time clamp** (`minSegmentTimeMs`)  
  Avoids unrealistic high-speed bursts on very short moves.
- **Pseudo S-curve** (`sCurveFactor`)  
  Locally reduces acceleration near corners to reduce jerk and resonance.
- **FastAccelStepper backend**  
  Movement runs through a fast stepper backend with proper ramp handling.

---

## Web UI workflow (slides)

The UI guides you through typical setup steps:

1. **Reference / retract belts**  
   Manual left/right motor control.
2. **Set top distance**  
   Define anchor spacing.
3. **Extend to home**  
   Move the machine to the base position.
4. **Pen / servo calibration**  
   Slider + +/- controls.
5. **Load image**  
   Upload `.svg` or `.png`.
6. **Choose renderer**  
   - Path tracing  
   - Vector → Raster → Vector
7. **Preview + simulation**  
   Preview image, progress bar, distance display, canvas simulation with play/pause.

---

## PNG features

PNG processing supports multiple modes:

- **Black/White** (legacy)
- **Color layers** (new)  
  Detects color layers and supports painting them sequentially.
- **Direct raster** (no vector conversion)  
  Raster-driven plotting mode.

Tools:
- **Color tolerance** to group similar colors  
- **Speckle removal** (minimum pixel threshold)  
- **Raster resolution** (`step px`) control  
- **Paint layers sequentially** button

---

## SVG batch / series mode

- Select a folder on SD and run multiple SVG jobs sequentially via the frontend.

---

## Firmware API (run control)

Core endpoints:
- `/run` – start job  
- `/resume` – continue after pause  
- `/status` – status/progress  
- `/command` – send commands  
- `/setSpeeds` – print/move speed  
- `/extendToHome` – homing/base position  
- `/setServo` – direct servo control  
- `/setPenDistance` – pen parameters  
- `/setTopDistance` – set top distance  
- `/getState` – current state

Diagnostics/system:
- `/logs` – ringbuffer/weblog  
- `/sysinfo` – system info  
- `/diag` – diagnostics endpoint  
- `/reboot` – reboot

Filesystem (LittleFS + SD):
- `/fs/info`, `/sd/remount`, `/fs/list`, `/fs/read`, `/fs/download`  
- `/fs/delete`, `/fs/mkdir`, `/fs/rename`, `/fs/copy`, `/fs/move`  
- `/uploadCommands`, `/downloadCommands`

Driver / step signal:
- `/pulseWidths` (GET)  
- `/setPulseWidths` (POST)  
- `/gpio/driverEnable` (GET/POST)

---

## Why it exists

Plotters fail in the same predictable ways:
- corners overshoot and ring  
- tiny circles become polygon-ish at insane speed  
- segment noise creates jitter  
- the stepper backend gets flooded with micro-moves  

This project exists to fix that with:
- lookahead planning  
- junction-aware corner control  
- segment cleanup + merging  
- micro-move speed limiting  
- more stable acceleration handling  


clear
pio run -t clean
pio run -t upload
pio run -t uploadfs

-------------------------------------------------
pio device monitor -b 115200
pio device monitor -p COM4 -b 115200