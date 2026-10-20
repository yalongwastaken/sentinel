# Architecture

Implementation detail for Sentinel. See the [README](../README.md) for what the
system is and how to build it.

## Task model

### Main Hub

| Task              | Priority | Period   | Stack | Role                     |
| ----------------- | -------- | -------- | ----- | ------------------------ |
| `pir_task`        | 5        | 100 ms   | 4096  | Motion detection         |
| `ultrasonic_task` | 4        | 200 ms   | 4096  | Distance measurement     |
| `dht11_task`      | 3        | 3 s      | 4096  | Temperature and humidity |
| `ble_client_task` | 3        | reactive | 4096  | Remote node link         |
| `lcd_task`        | 2        | 1 s      | 4096  | Display refresh          |

Priority follows how fast the signal changes: PIR edges are the most perishable,
the display the least. `ble_client_task` is the exception — it is reactive
rather than periodic, retrying its connection on a 1 s / 5 s backoff.

### Remote Node

| Task          | Period | Role                         |
| ------------- | ------ | ---------------------------- |
| `sensor_task` | 5 s    | Sample PIR, publish over BLE |

The 5 s period is a power decision, not a responsiveness one — the node is meant
to run on battery.

## Scheduling

Periodic tasks use `vTaskDelayUntil` against an `xTaskGetTickCount()` baseline,
not `vTaskDelay`. The wake time is computed from the previous wake rather than
from "now," so execution time inside the loop does not accumulate as period
drift.

## Shared state and concurrency

All cross-task sensor state lives in a single `sensor_data` struct guarded by
one FreeRTOS mutex. **The mutex protects that struct — it is not an I²C bus
lock.**

Readers copy under the lock and release before doing slow work:

```c
if (xSemaphoreTake(sensor_data.mutex, portMAX_DELAY) == pdTRUE) {
    motion   = sensor_data.motion_detected;
    distance = sensor_data.distance_cm;
    /* ... */
    xSemaphoreGive(sensor_data.mutex);
}
/* LCD I/O happens here, outside the critical section */
```

This keeps the critical section bounded by a handful of field copies instead of
by an I²C transaction, so the 100 ms PIR task is never blocked behind a display
update.

The hub is currently the only I²C master and the LCD its only device, so no bus
lock is needed. Adding a second I²C device would change that.

## Buses and protocols

| Interface          | Where                        | Notes                                  |
| ------------------ | ---------------------------- | -------------------------------------- |
| I²C @ 100 kHz      | LCD1602 via PCF8574 backpack | standard mode                          |
| 1-Wire, bit-banged | DHT11                        | no peripheral support; software timing |
| GPIO pulse timing  | HC-SR04                      | microsecond echo measurement           |
| BLE GATT (NimBLE)  | hub ↔ remote node            | hub is client, node is server          |

## Component layout

Drivers are ESP-IDF components written against the datasheet rather than
wrapping a library. Placement follows use:

- `components/` at the repository root — drivers used by **both** nodes.
  Currently `hcsr501`. Each project reaches it through `EXTRA_COMPONENT_DIRS`.
- `main_hub/components/` — drivers used only by the hub: `dht11`, `hcsr04`,
  `hd44780_pcf8574`.

Drivers do not import other drivers; composition happens in the application
layer.
