# BlackWire v6.1

### RF Intelligence & Site Survey Tool

BlackWire is a portable RF survey tool built with an **Arduino UNO R4 WiFi** and **DFRobot LCD Keypad Shield**.

It can scan nearby Wi-Fi networks, monitor signal strength, analyze channels, scan BLE devices, save network information, and provide a local web dashboard.

BlackWire is intended for **authorized security research, network administration, RF experimentation, and education**.

---

## Features

* Wi-Fi network scanning
* Signal strength tracking
* 2.4 GHz channel analysis
* Automatic network scanning
* BLE device scanning
* EEPROM network storage
* Local web dashboard
* CSV scan export
* System information
* EEPROM data clearing
* Built-in LCD mini game

### Wi-Fi Scanner

Scans nearby networks and displays:

* SSID
* Signal strength (RSSI)
* Signal quality
* Channel
* Encryption
* New network status

Up to **30 networks** can be stored in the active scan cache.

### Signal Tracker

Select a network and monitor it for about 60 seconds.

Displays:

* SSID
* RSSI
* Signal quality
* Signal history

### Channel Advisor

Analyzes Wi-Fi channel usage and recommends between:

```text
1
6
11
```

It shows network count, strongest RSSI, and the recommended channel.

### Auto Scan

Automatically scans the area every:

```text
30 seconds
60 seconds
5 minutes
```

After each scan, BlackWire reports the total and newly discovered networks.

### BLE Scanner

BLE support can be enabled with:

```cpp
#define ENABLE_BLE
```

The scanner can display:

* Device name
* Bluetooth address
* RSSI

Up to **20 BLE devices** are stored.

### Network Storage

BlackWire can save discovered networks to EEPROM.

Maximum:

```text
150 networks
```

Stored data includes:

* SSID
* Encryption type

The data can be viewed from the dashboard or deleted from the device.

### Web Dashboard

BlackWire can create its own Wi-Fi access point and host a local web interface.

Default settings:

```text
SSID: BlackWire-AP
Password: survey1234
```

The dashboard provides:

* Wi-Fi scans
* Zone information
* Channel map
* BLE information
* System information
* Saved networks
* EEPROM clearing
* CSV export

The LCD displays the dashboard's IP address.

### CSV Export

The dashboard can export scan results containing:

```text
SSID
Channel
RSSI
Signal Quality
Encryption
```

File name:

```text
blackwire_scan.csv
```

### Mini Game

BlackWire also includes a small Space-Invaders-style game for the 16×2 LCD.

Features include:

* Player ship
* Enemies
* Projectiles
* Levels
* Score
* Pause
* Increasing difficulty

Because apparently an RF scanner needed entertainment.

---

# Hardware

Required:

* **Arduino UNO R4 WiFi**
* **DFRobot LCD Keypad Shield**
* USB-C cable

The LCD shield provides the 16×2 display and physical buttons.

---

# Controls

| Button | Function         |
| ------ | ---------------- |
| UP     | Move up          |
| DOWN   | Move down        |
| LEFT   | Back / Cancel    |
| RIGHT  | Move right       |
| SELECT | Select / Confirm |

The exact function depends on the current menu.

---

# Hardware Configuration

LCD:

```cpp
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
```

Keypad input:

```text
A0
```

Approximate button values:

|    Input | Button |
| -------: | ------ |
|   `< 60` | RIGHT  |
|  `< 200` | UP     |
|  `< 400` | DOWN   |
|  `< 600` | LEFT   |
|  `< 800` | SELECT |
| `>= 800` | NONE   |

Menu input uses approximately **200 ms debounce**.

---

# Software

BlackWire uses the Arduino ecosystem and supports:

```text
Arduino UNO R4 WiFi
```

Libraries:

```cpp
#include <Arduino.h>
#include <WiFiS3.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <ArduinoBLE.h>
```

`ArduinoBLE` is only needed when BLE is enabled.

---

# Installation

### 1. Install Arduino IDE

Install the Arduino IDE.

### 2. Install UNO R4 Support

Install the Arduino UNO R4 board package through the Board Manager.

Select:

```text
Arduino UNO R4 WiFi
```

### 3. Connect the Hardware

Connect the LCD shield to the UNO R4 WiFi and connect the Arduino to your computer using USB-C.

### 4. Open BlackWire

Open:

```text
BlackWire.ino
```

in Arduino IDE.

### 5. Enable BLE

Add:

```cpp
#define ENABLE_BLE
```

to enable BLE scanning.

### 6. Upload

Select the correct Arduino port and upload the firmware.

After startup, BlackWire displays:

```text
BLACKWIRE v6.1
RF SURVEY TOOL
```

Saved network information is then loaded from EEPROM.

---

# Main Menu

```text
SCAN NETWORKS
SIGNAL TRACKER
BLE SCAN
CHANNEL ADVISE
AUTO SCAN
WEB DASHBOARD
SYSTEM INFO
CLEAR DATA
PLAY GAME
```

Use the LCD keypad to navigate.

---

# Web Dashboard

Selecting **WEB DASHBOARD** starts the BlackWire Wi-Fi access point.

Default:

```text
BlackWire-AP
```

The HTTP server runs on:

```text
Port 80
```

The LCD shows the device IP address.

Connect to the BlackWire access point and open that IP address in a browser.

### Routes

```text
/scan
/zone
/chanmap
/ble
/sysinfo
/clear
/csv
```

The dashboard refreshes approximately every 30 seconds.

---

# EEPROM Storage

BlackWire uses EEPROM to store network information.

Basic layout:

```text
[0-1]  Network count
[2-3]  Reserved
[4+]   Network records
```

Each record contains:

```text
32 bytes  SSID
1 byte    NULL terminator
1 byte    Encryption type
```

Maximum:

```text
150 networks
```

---

# Serial Output

Serial debugging runs at:

```text
9600 baud
```

Output categories include:

```text
[BW]
[BW-TRACK]
[BW-CHAN]
[BW-BLE]
[BW-GAME]
[BW-SYS]
[BW-WEB]
```

Useful for debugging and development.

---

# Configuration

Important settings are near the top of the source code.

### Version

```cpp
#define FW_VERSION "v6.1"
```

### Access Point

```cpp
#define AP_SSID "BlackWire-AP"
#define AP_PASS "survey1234"
```

### Limits

```cpp
constexpr int MAX_KNOWN = 150;
constexpr int MAX_SCAN  = 30;
constexpr int MAX_BLE   = 20;
```

### BLE

```cpp
#define ENABLE_BLE
```

---

# Security

BlackWire should only be used on networks and wireless environments where you have permission to scan or test.

### Change the Default Password

The default AP password is:

```text
survey1234
```

Change it before using BlackWire in an environment where other people could access the device.

### EEPROM

Saved network information is **not encrypted**.

Do not use the EEPROM as secure storage.

### Web Dashboard

The dashboard does not use authentication.

Treat it as a **local trusted interface** and do not expose it directly to the Internet.

---

# Project Structure

```text
BlackWire/
├── README.md
├── LICENSE
└── BlackWire.ino
```

---

# License

BlackWire is licensed under the:

**GNU General Public License v3.0 (GPLv3)**

See the `LICENSE` file for the full license.

GPLv3 allows the software to be used, studied, modified, and redistributed under its license terms.

---

# Third-Party Libraries

BlackWire uses Arduino libraries including:

* Arduino Core
* WiFiS3
* EEPROM
* LiquidCrystal
* ArduinoBLE

Each library is subject to its own license.

---

# Hardware Credits

### Arduino UNO R4 WiFi

Developed by **Arduino**.

Provides the main microcontroller and wireless hardware.

### DFRobot LCD Keypad Shield

Developed by **DFRobot**.

Provides the 16×2 LCD and keypad interface.

---

# Responsible Use

BlackWire is intended for:

* Network administration
* RF site surveys
* Security research
* Authorized penetration testing
* Wireless troubleshooting
* Education
* Embedded development
* RF experimentation

Only scan or test wireless networks and devices when you have appropriate authorization.

---

## Author

**Piper Lovejoy - Mr.PMOSH**

**BlackWire v6.1**
