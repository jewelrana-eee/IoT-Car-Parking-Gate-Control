# IoT Based Car Parking System with Automated Gate Control



![Smart parking full view](smart-parking-full-view.jpg)



An ESP32 based smart parking prototype with 4 slots and one automated gate. It validates every vehicle against an actual gate crossing, so it can tell a legitimately parked car from an object placed directly in a slot. Status is shown on an OLED display and on a local web dashboard hosted by the ESP32 itself (no internet needed).

## Features
- Automatic gate open/close with ultrasonic sensor and SG90 servo
- 4 slots monitored by IR sensors
- Validated entry counting (availability comes from verified entries, not raw sensor readings)
- Illegal entry detection with buzzer alert, OLED warning and dashboard highlight
- Reserved and Maintenance slot modes from the dashboard
- Left/Right LED indicators show the direction to the assigned slot
- Live OLED screens: home, system info, welcome, exiting, illegal entry, parking full
- Local web dashboard in WiFi Access Point mode (192.168.4.1)
- Manual gate control with automatic return to auto mode after a timeout
- Recent activity log with relative timestamps
- Battery powered (3S Li-ion) with power switch

## Components
| Component | Qty | Purpose |
|---|---|---|
| ESP32 development board | 1 | Main controller + WiFi AP |
| IR sensor module | 5 | 4 slot sensors + inner gate sensor |
| HC-SR04 ultrasonic sensor | 1 | Outer gate vehicle detection |
| SG90 servo motor | 1 | Gate barrier |
| SSD1306 OLED display | 1 | Driver status screen |
| Buzzer | 1 | Audible alerts |
| LED | 2 | Left/Right direction indicators |
| Buck converter | 1 | Voltage regulation |
| 3S battery holder + Li-ion cells | 1 | Power supply |
| Capacitors, breadboards, wires, PVC board | - | Circuit and model body |

Estimated prototype cost: about 2756 BDT.

## Pin Configuration
| Function | GPIO |
|---|---|
| Slot 1 / 2 / 3 / 4 IR | 34 / 35 / 33 / 25 |
| Inner gate IR | 32 |
| Ultrasonic TRIG / ECHO | 16 / 17 |
| Gate servo | 27 |
| OLED SDA / SCL | 21 / 22 |
| Left / Right LED | 26 / 13 |
| Buzzer | 23 |

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
A slot occupied without a validated entry, or a Reserved/Maintenance slot being used, triggers the buzzer, OLED warning and a highlighted slot on the dashboard.

## Software
- Arduino IDE with ESP32 board package
- Libraries: WiFi, WebServer, Wire, Adafruit_GFX, Adafruit_SSD1306, ESP32Servo, esp_task_wdt
- Gate state machine: IDLE, OPEN_WAIT, PASS_WAIT, CLOSING

## Key Design Decisions
- Availability is counted from validated entries, so lifting a car out by hand cannot free a slot.
- Non-blocking, interrupt-driven ultrasonic reading keeps the web server responsive.
- Debounce and grace periods filter sensor noise.
- Servo signal detach timing removes motor jitter.

## Test Results
All 7 test cases passed: entry, exit, illegal entry, silent removal after illegal entry, parking full, manual gate control, and reserved slot violation.

## Limitations
- One gate and 4 slots only
- No license plate identification
- Dashboard works only within ESP32 WiFi range (no cloud or remote access)
- Activity log is lost on power reset
- Thresholds are calibrated for this hardware

## Future Scope
Cloud logging, license plate recognition, mobile app, multiple gates, payment system.

## Demo Video
[Watch the demo here](YOUR_VIDEO_LINK)

## Setup
1. Install Arduino IDE and add ESP32 board support.
2. Install the libraries listed above.
3. Upload the code to the ESP32.
4. Connect your phone to the ESP32 WiFi network.
5. Open 192.168.4.1 in a browser to use the dashboard.

## Gallery


![Parking layout overview](parking-layout-overview.jpg)




![Automated gate with ESP32](automated-gate-esp32.jpg)




![Barrier gate close-up](barrier-gate-closeup.jpg)




![OLED slot status display](oled-slot-status-display.jpg)



## Circuit


![ESP32 and servo circuit](esp32-servo-circuit.jpg)




![Ultrasonic sensor at gate](ultrasonic-sensor-gate.jpg)




![Buzzer and IR circuit](buzzer-ir-circuit.jpg)



## Web Dashboard


![Dashboard all slots free](dashboard-all-slots-free.png)




![Dashboard illegal entry alert](dashboard-illegal-entry-alert.png)



## Author
Md. Jewel Rana
EEE, Daffodil International University
