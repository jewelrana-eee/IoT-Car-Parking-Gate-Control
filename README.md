   # IoT Based Car Parking System with Automated Gate Control

<p align="center">
<img src="images/smart-parking-full-view.jpg" alt="Smart parking full view" width="500">
</p>

<p align="justify">
An ESP32 based IoT smart parking prototype with 4 slots and one automated gate. It validates every vehicle against an actual gate crossing, so it can tell a legitimately parked car from an object placed directly in a slot. Status is shown on an OLED display and on a web dashboard hosted by the ESP32. The ESP32 connects to WiFi in STA mode for IoT access and automatically falls back to its own Access Point (AP) mode if the connection fails, so the dashboard is always reachable.
</p>

[Features](#features) | [System Overview](#system-overview) | [Components](#components) | [Pin Configuration](#pin-configuration) | [How It Works](#how-it-works) | [Demo Video](#demo-video) | [Setup](#setup) | [Gallery](#gallery) | [Web Dashboard](#web-dashboard) | [Author](#author)

## Features
- **Fully IoT based:** ESP32 connects to WiFi (STA mode) and serves the web dashboard
- **Dual WiFi mode:** STA and AP, each acts as a backup if the other fails
- **OTA update:** no USB cable needed after the first upload
- **Automatic gate** with ultrasonic sensor and SG90 servo
- **Validated entry counting:** availability comes from verified entries, not raw sensor readings
- **Illegal entry detection** with buzzer alert, OLED warning and dashboard highlight
- **Reserved and Maintenance** slot modes from the dashboard
- **Left/Right LED indicators** show the direction to the assigned slot
- **Live OLED screens:** home, system info, welcome, exiting, illegal entry, parking full
- **Manual gate control** with automatic return to auto mode after a timeout
- **Recent activity log** with relative timestamps

## System Overview
<p align="center">
<img src="assets/block-diagram.svg" alt="System block diagram" width="650">
</p>

## Components
<div align="center">
<table width="650">
<tr><th align="left">Component</th><th>Qty</th><th align="left">Purpose</th></tr>
<tr><td>ESP32 development board</td><td align="center">1</td><td>Main controller + WiFi</td></tr>
<tr><td>IR sensor module</td><td align="center">5</td><td>4 slot sensors + inner gate sensor</td></tr>
<tr><td>HC-SR04 ultrasonic sensor</td><td align="center">1</td><td>Outer gate vehicle detection</td></tr>
<tr><td>SG90 servo motor</td><td align="center">1</td><td>Gate barrier</td></tr>
<tr><td>SSD1306 OLED display</td><td align="center">1</td><td>Driver status screen</td></tr>
<tr><td>Buzzer</td><td align="center">1</td><td>Audible alerts</td></tr>
<tr><td>LED</td><td align="center">2</td><td>Left/Right direction indicators</td></tr>
<tr><td>Buck converter</td><td align="center">1</td><td>Voltage regulation</td></tr>
<tr><td>3S battery holder + Li-ion cells</td><td align="center">1</td><td>Power supply</td></tr>
<tr><td>Capacitors, breadboards, wires, PVC board</td><td align="center">-</td><td>Circuit and model body</td></tr>
</table>
</div>

<p align="center">Estimated prototype cost: about 2756 BDT</p>

## Pin Configuration
<div align="center">
<table width="650">
<tr><th align="left">Function</th><th align="left">GPIO</th></tr>
<tr><td>Slot 1 / 2 / 3 / 4 IR</td><td>34 / 35 / 33 / 25</td></tr>
<tr><td>Inner gate IR</td><td>32</td></tr>
<tr><td>Ultrasonic TRIG / ECHO</td><td>16 / 17</td></tr>
<tr><td>Gate servo</td><td>27</td></tr>
<tr><td>OLED SDA / SCL</td><td>21 / 22</td></tr>
<tr><td>Left / Right LED</td><td>26 / 13</td></tr>
<tr><td>Buzzer</td><td>23</td></tr>
</table>
</div>

## How It Works
**Entry**
1. The ultrasonic sensor detects a car at the outer gate.
2. If a slot is free, the gate opens and the buzzer beeps.
3. Crossing the inner IR sensor registers a validated entry.
4. The nearest free slot is assigned and the LED shows left or right.
5. The slot sensor confirms parking and the OLED shows a thank you message.

**Exit**
1. When a validated car leaves its slot, the exit sequence is armed.
2. The gate opens and the OLED shows the exiting slot number.
3. The count decreases after the car crosses the outer sensor, then the gate closes.

**Illegal entry**

<p align="justify">
A slot occupied without a validated entry, or a Reserved/Maintenance slot being used, triggers the buzzer, OLED warning and a highlighted slot on the dashboard.
</p>

<details>
<summary><b>Key design decisions</b></summary>

- Availability is counted from validated entries, so lifting a car out by hand cannot free a slot.
- Non-blocking, interrupt-driven ultrasonic reading keeps the web server responsive.
- Debounce and grace periods filter sensor noise.
- Servo signal detach timing removes motor jitter.
- STA and AP dual mode keeps the dashboard reachable even if the WiFi network is unavailable.
- OTA support allows firmware updates without opening the prototype.
</details>

<details>
<summary><b>Software and libraries</b></summary>

- Arduino IDE with ESP32 board package
- Libraries: WiFi, WebServer, Wire, Adafruit_GFX, Adafruit_SSD1306, ESP32Servo, ArduinoOTA, esp_task_wdt
- Gate state machine: IDLE, OPEN_WAIT, PASS_WAIT, CLOSING
</details>

## Test Results
<p align="justify">
All 7 test cases passed: entry, exit, illegal entry, silent removal after illegal entry, parking full, manual gate control, and reserved slot violation.
</p>

## Demo Video
[Watch the demo here](https://youtu.be/0OySd1ujqDo?si=D1jraEN2lLzzpTo3)

## Setup
1. Install Arduino IDE and add ESP32 board support.
2. Install the libraries listed above.
3. Add your own WiFi name and password in the code (not included here for security).
4. Upload the code to the ESP32 over USB for the first time.
5. After that, firmware can be updated wirelessly using OTA.
6. **STA mode:** open the ESP32 IP address in a browser on the same WiFi network.
7. **AP mode (fallback):** if the WiFi connection fails, connect your phone to the ESP32 WiFi network and open `192.168.4.1`.

## Gallery
<div align="center">
<table>
<tr>
<td align="center"><img src="images/parking-layout-overview.jpg" alt="Parking layout overview" height="300"><br><sub>Parking layout</sub></td>
<td align="center"><img src="images/automated-gate-esp32.jpg" alt="Automated gate with ESP32" height="300"><br><sub>Gate with ESP32</sub></td>
</tr>
<tr>
<td align="center"><img src="images/barrier-gate-closeup.jpg" alt="Barrier gate close-up" height="300"><br><sub>Barrier gate close-up</sub></td>
<td align="center"><img src="images/oled-slot-status-display.jpg" alt="OLED slot status display" height="300"><br><sub>OLED and slot sensors</sub></td>
</tr>
</table>
</div>

### Circuit
<div align="center">
<table>
<tr>
<td align="center"><img src="images/esp32-servo-circuit.jpg" alt="ESP32 and servo circuit" height="200"><br><sub>ESP32 and servo</sub></td>
<td align="center"><img src="images/ultrasonic-sensor-gate.jpg" alt="Ultrasonic sensor at gate" height="200"><br><sub>Ultrasonic sensor</sub></td>
<td align="center"><img src="images/buzzer-ir-circuit.jpg" alt="Buzzer and IR circuit" height="200"><br><sub>Buzzer and IR</sub></td>
</tr>
</table>
</div>

## Web Dashboard
<div align="center">
<table>
<tr>
<td align="center"><img src="images/dashboard-all-slots-free.png" alt="Dashboard all slots free" width="300"><br><sub>All slots free</sub></td>
<td align="center"><img src="images/dashboard-illegal-entry-alert.png" alt="Dashboard illegal entry alert" width="300"><br><sub>Illegal entry alert</sub></td>
</tr>
</table>
</div>

<details>
<summary><b>Limitations and future scope</b></summary>

**Limitations**
- One gate and 4 slots only
- No license plate identification
- Activity log is lost on power reset
- Thresholds are calibrated for this hardware

**Future scope:** cloud logging, license plate recognition, mobile app, multiple gates, payment system.
</details>

## Author
<p>
<b>Md. Jewel Rana</b><br>
Electrical &amp; Electronic Engineering<br>
Daffodil International University
</p>
