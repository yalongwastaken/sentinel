# HC-SR501 PIR Motion Sensor Driver

ESP-IDF component for the HC-SR501 passive infrared motion sensor. It configures
the sensor's output pin as an input and reports whether the output is high; the
application decides what counts as a motion event.

## Hardware

| Sensor Pin | Connection                             |
| ---------- | -------------------------------------- |
| VCC        | 5 V                                    |
| GND        | ESP32 ground                           |
| OUT        | The GPIO configured by the application |

OUT is 3.3 V logic, so it connects to the GPIO directly.

The module has two potentiometers and a jumper:

- **Time delay** sets how long OUT stays high after motion, from a few seconds
  to a few minutes. [The BLE link](../../docs/architecture.md#the-ble-link)
  section of the architecture doc says how to set it.
- **Sensitivity** sets how far away motion is detected, up to several meters.
- **Trigger jumper:** at H, the repeatable trigger, OUT stays high while motion
  continues. At L, the single trigger, OUT goes low when the delay ends even if
  motion continues.

For about a minute after power-up the sensor settles, and OUT can go high with
no motion, so readings in that time mean nothing.

Each node's GPIO is `HCSR501_GPIO_PIN` in its configuration header:
[`main_hub_system_config.h`](../../main_hub/main/include/main_hub_system_config.h)
and
[`remote_node_system_config.h`](../../remote_node/main/include/remote_node_system_config.h).
