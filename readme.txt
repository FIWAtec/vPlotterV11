

-------------------------------------------------

clear
pio run -t clean
pio run -t upload
pio run -t uploadfs
  


-------------------------------------------------
pio device monitor -b 115200
pio device monitor -p COM4 -b 115200



# README – ESP32 Wall Plotter (MKS DLC32 V2.1 + NEMA17 + MG995R) V3.0.01

## Project Summary
This project is a DIY wall/pen plotter controlled by an **ESP32-based MKS DLC32 V2.1** board.  
Two **NEMA 17 stepper motors** move the drawing head by pulling belts/strings (left and right).  
A **MG995R servo** controls the **pen mechanism (pen up/down)**.

The device is controlled via a web interface served directly from the ESP32 (LittleFS), using an asynchronous web server.

## Hardware Used
- **Controller:** MKS DLC32 V2.1 (ESP32)
- **Motors:** 2× NEMA 17 stepper motors
- **Stepper Drivers:** 2× TB6600 (external drivers)
- **Servo (Pen Control):** 1× MG995R (pen up/down actuator)
- **Power:**
  - Motor power supply for TB6600 (typically 12–36V, depending on setup)
  - A solid 5V supply for DLC32 logic and especially for the MG995R servo

## Stepper Control Wiring (TB6600)
The TB6600 inputs are opto-isolated. Recommended wiring:
- **STEP+ / DIR+ / ENA+** → **+5V**
- **STEP- / DIR- / ENA-** → controller signals (GPIO) / GND  
This uses the controller as a “sinking” output (active-low pulses on the “-” terminals).

**Important:** Common ground between controller and drivers is mandatory.

## GPIO Mapping (Current Plan)

### Motor Left (TB6600)
- **LEFT_STEP** → GPIO4  (I2C header IO4)
- **LEFT_DIR**  → GPIO25 (EXP1 header IO25)

### Motor Right (TB6600)
- **RIGHT_STEP** → GPIO26 (EXP1 header IO26)
- **RIGHT_DIR**  → GPIO22 (PROBE header IO22)

Optional additional EXP1 pins (if needed):
- **GPIO33** (EXP1 other column)
- **GPIO27** (EXP1 other column)

## Pen Control (MG995R Servo)
- The **MG995R servo is dedicated to pen up/down control**.
- **Signal** goes to a servo-capable GPIO (PWM output).
- **Power:** Use a real 5V supply (MG995R can pull high current).
