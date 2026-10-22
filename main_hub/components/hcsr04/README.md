# HC-SR04 Ultrasonic Sensor Driver

ESP-IDF component for the HC-SR04 ultrasonic distance sensor.

## Hardware

Use a regulated 5 V supply and two 1% tolerance, ¼-watt resistors: 2.2 kΩ and
3.3 kΩ. Disconnect power before wiring.

| Sensor Pin | Connection                                     |
| ---------- | ---------------------------------------------- |
| VCC        | Regulated 5 V                                  |
| GND        | ESP32 ground and supply ground                 |
| TRIG       | The trigger GPIO configured by the application |
| ECHO       | The resistor divider below                     |

```text
HC-SR04 ECHO ──[ 2.2 kΩ ]──┬── ESP32 echo GPIO
                           │
                        [ 3.3 kΩ ]
                           │
                          GND
```

Both resistors are required. The echo GPIO connects to their junction; the 3.3
kΩ resistor connects that junction to ground. A nominal 5 V echo becomes
`5 V × 3.3 / (2.2 + 3.3) = 3.0 V` at the GPIO.

The sensor's ECHO output uses
[5 V logic](https://www.adafruit.com/product/3942), which exceeds the
[ESP32's input specification](https://documentation.espressif.com/esp32_wroom-32_datasheet_en.pdf).
The divider lowers it to a valid input level. TRIG accepts the ESP32's signal
directly.

For the hub's GPIO assignments, see `HCSR04_TRIGGER_GPIO_PIN` and
`HCSR04_ECHO_GPIO_PIN` in
[`main_hub_system_config.h`](../../main/include/main_hub_system_config.h). If
using a USB-powered development board's 5 V header, verify that header against
the board's documentation. Never connect 5 V to its 3V3 pin.
