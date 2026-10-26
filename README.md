# IoT Based Car Parking and Automated Gate Control



![Smart parking full view](smart-parking-full-view.jpg)



A smart parking system prototype built with an ESP32. It detects incoming cars at the gate, opens the gate automatically, and monitors 4 parking slots in real time. Slot status is shown on an OLED display and on a built-in web dashboard hosted by the ESP32 (accessible at 192.168.4.1).

## Features
- Automatic gate open/close using an ultrasonic sensor and servo motor
- 4 parking slots monitored by IR sensors
- Live slot status on OLED display
- Built-in web dashboard (no internet needed), served by the ESP32 in WiFi AP mode
- Slot reservation and maintenance mode from the dashboard
- Manual gate control and auto mode toggle
- Illegal entry detection with live activity log
- Buzzer alert at the gate
- Battery powered (3x 18650 Li-ion) with a power switch

## Components
| Component | Purpose |
|---|---|
| ESP32 | Main controller + WiFi |
| HC-SR04 ultrasonic sensor | Detects car at the gate |
| SG90 servo motor | Gate barrier |
| IR obstacle sensor x4 | Slot 1 to 4 occupancy |
| 0.96" OLED (I2C) | Shows slot status |
| Buzzer | Alert sound |
| Buck converter | Steps battery voltage down to 5V |
| 3x 18650 Li-ion + holder | Power supply |
| Capacitors, LEDs, breadboard, jumper wires | Supporting circuit |

## How It Works
1. A car arrives at the gate and the ultrasonic sensor detects it.
2. If a slot is free, the servo lifts the gate.
3. IR sensors check each slot and mark it Occupied or Free.
4. The OLED and the web dashboard show the live status.
5. The gate closes after the car passes.

## Demo Video
[Watch the demo here](YOUR_VIDEO_LINK)

## Setup
1. Install Arduino IDE and add ESP32 board support.
2. Install the required libraries.
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
Md. Jewel Rana, EEE, Daffodil International University
