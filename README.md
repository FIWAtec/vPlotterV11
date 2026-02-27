# Motion/Planner & Funktionsübersicht

Dieses Dokument beschreibt die **Bewegungs-Optimierungen (Motion/Planner)** sowie die **Gesamt-Funktionen** der Software (Firmware + Web-UI).

---

## A) Bewegungs-Optimierungen (Motion/Planner)

### 1) Lookahead-Planner im Runner (vorausschauende Geschwindigkeitsplanung)

- **Lookahead-Queue (Vorschau-Puffer)**
  - Einstellbare Tiefe: `lookaheadSegments` (Standard: **48**)
  - Zweck: Mehrere Segmente werden vorab betrachtet, um Geschwindigkeit und Übergänge sinnvoll zu planen.

- **Junction-Deviation / Eckgeschwindigkeit (GRBL-ähnlich)**
  - Berechnet eine **maximal zulässige Eckgeschwindigkeit** aus:
    - Segmentwinkel
    - Beschleunigung
    - `junctionDeviationMM`
  - Ergebnis: Stabilere Ecken, weniger Schwingen, weniger “Abreißen” in engen Radien.

- **Winkelbasierte Eck-Abbremsung**
  - Parameter:
    - `cornerSlowdown`
    - `minCornerFactor`
  - Begrenzen, wie stark an Ecken reduziert wird (kontrolliertes Abbremsen ohne “totzubremsen”).

- **G2/G3 Bögen (Arc) → Segmentierung**
  - Kreisbögen werden in lineare Segmente zerlegt, so dass der **Chord-Error (Sehnenfehler)** klein bleibt.
  - Einige erzeugte Punkte können als `protect` markiert werden, damit sie nicht durch spätere Optimierung entfernt/verschmolzen werden.

- **Segment-Cleanup**
  - Entfernt sehr kurze Segmente als Rauschen/Noise:
    - Schwellwert: `minSegmentLenMM`

- **Collinear-Merging (Kollinearität)**
  - Verschmilzt Move-Move-Move Sequenzen, wenn der Winkel innerhalb `collinearDeg` liegt.
  - Effekt:
    - Weniger Mikro-Segmente
    - Ruhigerer Lauf
    - Weniger Overhead im Stepper-Backend

---

### 2) “On-the-fly” Optimierungen in `Movement::beginLinearTravel()`

- **Backlash-Kompensation (Spiel) in XY**
  - Wenn die Bewegungsrichtung in X/Y kippt, wird kompensiert:
    - `backlashXmm`
    - `backlashYmm`
  - Ziel: Genauere Wiederholbarkeit trotz mechanischem Spiel.

- **Corner-Factor (dynamische Eck-Reduktion)**
  - Nutzt den letzten Segmentvektor:
    - `lastSegmentDX`
    - `lastSegmentDY`
  - Reduziert Geschwindigkeit an “echten” Ecken abhängig vom Richtungswechsel.

- **Micro-Segment Speed Limiter (Mini-Segmente nicht “zu schnell”)**
  - Wenn Segmentlänge < `microSlowLenMM`, wird die Geschwindigkeit Richtung `microMinFactor` gedrückt.
  - Ziel: Kleine Kreise/feine Details laufen kontrolliert und nicht “schießend” ab.

- **Min-Segment-Zeit Clamp**
  - `minSegmentTimeMs` verhindert, dass winzige Moves mit unrealistisch hoher Geschwindigkeit ausgeführt werden.
  - Effekt: Gleichmäßiger, weniger Ruck, weniger Resonanzen.

- **Pseudo S-Curve (lokal reduzierte Acceleration)**
  - Reduziert lokal die Beschleunigung um Ecken:
    - `sCurveFactor`
  - Ziel: Weniger Ruck, weniger mechanisches Schwingen.

- **FastAccelStepper Backend**
  - Bewegung läuft über `FastStepperBackend` (kein AccelStepper in `Movement`).
  - `run()` nutzt die eingestellte Beschleunigungsrampe des Backends.

---

## B) Gesamt-Funktionsliste der Software (Firmware + Web-UI)

### 1) Setup / Workflow (UI “Slides”)

- **Referenzieren / Riemen einziehen**
  - Linker/Rechter Motor manuell ansteuern (Toggle).

- **Top-Distance setzen**
  - Abstand der Aufhängungen definieren.

- **Zur Grundstellung fahren**
  - “Extend to Home”.

- **Stift/Servo kalibrieren**
  - Slider + `+/-` Buttons.

- **Bild laden**
  - SVG und PNG (`accept=".svg,.png"`)

- **Renderer wählen**
  - „Pfad-Nachverfolgung“
  - „Vektor → Raster → Vektor“

- **Zeichenvorschau + Simulation**
  - Preview-Bild
  - Progressbar
  - Distanzanzeige
  - Canvas-Simulation (Play/Pause etc.)

---

### 2) PNG-Funktionen (Upload-Bereich)

- **PNG-Modi**
  - Schwarz/Weiß (alt)
  - Farblayer (neu)
  - Raster-Direkt (ohne Vektor)

- **Farbtoleranz-Regler**
  - Zuordnung ähnlicher Farben.

- **Flecken entfernen**
  - Min-Pixel-Schwellwert.

- **Raster-Auflösung**
  - Step-Pixel (`step px`) im Raster-Modus.

- **Farblayer-Workflow**
  - Erkannten Farblayer anzeigen
  - Button: „Farblayer nacheinander malen“

---

### 3) SVG-Serien / Batch

- Button: **„SVG-SERIE (SD-Ordner)“**
  - Ordner auswählen
  - Jobs seriell abarbeiten (Frontend-Seite)

---

### 4) Laufsteuerung (Firmware API)

- `/run` – Job starten  
- `/resume` – Weiterlaufen nach Pause  
- `/status` – Status/Progress  
- `/command` – Kommandos senden  
- `/setSpeeds` – Print/Move Speed setzen  
- `/extendToHome` – Homing/Grundstellung  
- `/setServo` – Servo direkt setzen  
- `/setPenDistance` – Stift-Parameter setzen  
- `/setTopDistance` – Top-Distance setzen  
- `/getState` – aktuellen Zustand abrufen  

---

### 5) Diagnose, Logs, System

- `/logs` – Ringbuffer/Weblog  
- `/sysinfo` – Systeminfos  
- `/diag` – Diagnose-Endpoint  
- `/reboot` – Reboot  

---

### 6) Dateisystem / Datei-Manager (SD + LittleFS)

- `/fs/info`  
- `/sd/remount`  
- `/fs/list`  
- `/fs/read`  
- `/fs/download`  
- `/fs/delete`  
- `/fs/mkdir`  
- `/fs/rename`  
- `/fs/copy`  
- `/fs/move`  

**Commands-Datei Handling**
- `/uploadCommands` – Commands hochladen  
- `/downloadCommands` – Commands herunterladen  

---

### 7) Treiber / Step-Signal Themen

- `/pulseWidths` (GET)  
- `/setPulseWidths` (POST)  
- `/gpio/driverEnable` (GET/POST)  