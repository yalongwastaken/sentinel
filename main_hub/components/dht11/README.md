# DHT11 Temperature and Humidity Sensor Driver

ESP-IDF component for the DHT11. It drives the sensor's single-wire protocol in
software and returns temperature and relative humidity, refusing a read sooner
than the sensor allows.

## Hardware

| Sensor Pin | Connection                                                 |
| ---------- | ---------------------------------------------------------- |
| VCC        | 3.3 V                                                      |
| GND        | ESP32 ground                                               |
| DATA       | The GPIO configured by the application, pulled up to 3.3 V |

DATA needs an external pull-up resistor of 4.7 kΩ to 10 kΩ. The driver leaves
the ESP32's internal pull-up off, so without one every read fails. Most
three-pin DHT11 modules carry the pull-up on their board; a bare four-pin sensor
needs one added, and its third pin stays unconnected.

Power the sensor from 3.3 V, not 5 V. The pull-up lifts DATA to the supply, and
the ESP32's inputs are not 5 V tolerant.

For the hub's GPIO, see `DHT11_GPIO_PIN` in
[`main_hub_system_config.h`](../../main/include/main_hub_system_config.h).
