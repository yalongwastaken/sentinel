# Sentinel

A two-node wireless motion monitor on ESP32. A main hub reads its own motion,
distance, temperature, and humidity sensors, links over BLE to a remote node
that reports motion from a second spot, and shows everything on a local display.

> **Status: complete.**

## How It Works

The **main hub** samples an HC-SR501 PIR motion sensor, an HC-SR04 ultrasonic
distance sensor, and a DHT11 temperature and humidity sensor, each in its own
FreeRTOS task, and refreshes an LCD1602 with the latest readings. It finds the
remote node by its BLE service UUID, subscribes to its motion, and shows remote
motion beside its own.

The **remote node** samples its own HC-SR501 and notifies the hub whenever
motion starts or stops.

Both boards log each state change as a `key=value` line, retry a failed startup
by rebooting, and stop a firmware build that keeps crashing the same way until a
new build is flashed. [docs/architecture.md](docs/architecture.md) explains why
each piece is built the way it is.

## Repository Structure

```
sentinel/
├── components/       # drivers and libraries both nodes build
├── main_hub/
│   ├── components/   # hub-only drivers
│   ├── main/         # hub application, BLE client, pin configuration
│   └── test_apps/    # on-target sensor tests
├── remote_node/
│   └── main/         # remote application, pin configuration
├── host_test/        # host tests for the logic that needs no hardware
└── docs/
```

Each node builds as its own ESP-IDF project and reaches the shared `components/`
through its `EXTRA_COMPONENT_DIRS`.

## Build and Flash

### Requirements

- ESP-IDF v6.1, plus `libbsd-dev` on Linux for the host tests
- **Main hub:** ESP32-WROOM-32, HC-SR501, HC-SR04, DHT11, LCD1602 with a PCF8574
  I²C backpack
- **Remote node:** ESP32-WROOM-32, HC-SR501, USB or battery supply

Each node's pins are set in its configuration header:
`main_hub/main/include/main_hub_system_config.h` and
`remote_node/main/include/remote_node_system_config.h`.

### Flash Both Nodes

From the repository root, with ESP-IDF's environment loaded:

```bash
idf.py -C main_hub -p /dev/ttyUSB0 flash monitor
idf.py -C remote_node -p /dev/ttyUSB1 flash monitor
```

### Run the Tests

```bash
# the hardware-free logic, on this machine
(cd host_test && idf.py build && pytest --target linux)

# the hub's sensors on their wiring: point the HC-SR04 at something within 4 m
# and wave at the PIR when asked
(cd main_hub/test_apps && idf.py build && pytest --target esp32 --port /dev/ttyUSB0)
```

## Author

**Anthony Yalong**

- Email: yalong.anthony123@gmail.com
- GitHub: [@yalongwastaken](https://github.com/yalongwastaken)

## License

MIT. See [LICENSE](LICENSE).
