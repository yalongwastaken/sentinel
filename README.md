# Sentinel

A multi-node wireless sensor network on ESP32. A main hub aggregates motion,
distance, and environmental readings from its own sensors and from a
battery-powered remote node over BLE, and surfaces them on a local display.

The goal is a real distributed embedded system rather than a single-board demo:
two independent firmware images, sensor drivers written from datasheets,
FreeRTOS task scheduling with mutex-protected shared state, and a BLE GATT link
between nodes.

## Architecture

### Main Hub — ESP32 #1

Central monitoring node.

- **PIR motion sensor** — edge detection with debouncing
- **HC-SR04 ultrasonic** — proximity detection, 2–400 cm
- **DHT11** — temperature and humidity via bit-banged 1-Wire
- **LCD1602 over I²C** — live sensor display
- **BLE client** — receives readings from the remote node

### Remote Node — ESP32 #2

Battery-powered satellite sensor.

- **PIR motion sensor** — remote motion detection
- **BLE server** — transmits readings over NimBLE

## Repository Structure

```
sentinel/
├── components/              # Drivers shared by both nodes
│   └── hcsr501/             # HC-SR501 PIR motion sensor
├── main_hub/                # Main hub firmware (ESP32 #1)
│   ├── components/          # Hub-only drivers
│   │   ├── dht11/           # Temperature/humidity sensor
│   │   ├── hcsr04/          # Ultrasonic distance sensor
│   │   └── hd44780_pcf8574/ # LCD1602 over a PCF8574 I2C backpack
│   ├── main/
│   │   ├── include/         # Node configuration
│   │   └── main.c
│   ├── test/manual/         # Per-sensor manual tests
│   └── CMakeLists.txt
├── remote_node/             # Remote node firmware (ESP32 #2)
│   ├── main/
│   │   ├── include/         # Node configuration
│   │   └── main.c
│   ├── test/manual/
│   └── CMakeLists.txt
├── docs/
│   └── architecture.md
└── README.md
```

Each node builds as an independent ESP-IDF project. Drivers used by both nodes
live in the root `components/`, pulled in through each project's
`EXTRA_COMPONENT_DIRS`; drivers used by one node stay under that node.

## Documentation

- [docs/architecture.md](docs/architecture.md) — task model, scheduling,
  concurrency, and bus details.

## Requirements

**Main hub** — ESP32-WROOM-32 · HC-SR501 PIR · HC-SR04 ultrasonic · DHT11 ·
LCD1602 with PCF8574 I²C backpack · breadboard and jumpers · 5V/3.3V supply

**Remote node** — ESP32-WROOM-32 · HC-SR501 PIR · USB or battery supply

## Pin Configuration

### Main Hub

| Component       | Direction | GPIO |
| --------------- | --------- | ---- |
| PIR sensor      | IN        | 13   |
| Ultrasonic TRIG | OUT       | 12   |
| Ultrasonic ECHO | IN        | 14   |
| DHT11 data      | I/O       | 15   |
| I²C SDA         | I/O       | 21   |
| I²C SCL         | I/O       | 22   |
| Status LED      | OUT       | 2    |

### Remote Node

| Component  | Direction | GPIO |
| ---------- | --------- | ---- |
| PIR sensor | IN        | 13   |

## Build and Flash

```bash
# Main hub
cd main_hub
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

# Remote node
cd remote_node
idf.py build
idf.py -p /dev/ttyUSB1 flash monitor
```

## Author

**Anthony Yalong**

- Email: yalong.anthony123@gmail.com
- GitHub: [@yalongwastaken](https://github.com/yalongwastaken)
