# Architecture

Why Sentinel is built the way it is. Values live in the code, and each section
names where.

## Two Firmware Images, One Contract

The hub and the remote node are separate ESP-IDF projects that must agree on how
they talk. The BLE device name, the two 128-bit UUIDs, and the one-byte motion
payload are defined once, in `components/remote_motion_ble/`, and both projects
build from it, so the two sides cannot drift apart. The UUIDs are generated
rather than 16-bit, because the 16-bit space belongs to services the Bluetooth
SIG assigns.

## Drivers Report, Applications Decide

Each driver configures its part and reports what it measures: the HC-SR501
driver says whether its output is high, the HC-SR04 driver returns a distance,
the DHT11 driver fills a reading. No driver caches a result or decides what an
event is; the applications do. The one rule a driver keeps is its part's own:
the DHT11 driver refuses a read sooner than its datasheet allows.

Drivers never include other drivers; composition happens in each node's `main/`.
The logic that needs no hardware, DHT11 frame decoding, HC-SR04 echo-to-distance
conversion, and the BLE payload, sits in files with no GPIO dependency so
`host_test/` can run it off the board.

## Hub Tasks

Each sensor has its own FreeRTOS task, and a display task refreshes the LCD.
Priority follows how quickly a signal goes stale: motion first, then distance,
then temperature and humidity, the display last. Periods and priorities are in
`main_hub/main/include/main_hub_system_config.h`.

Periodic tasks wake with `vTaskDelayUntil` against their previous wake, so time
spent inside a loop does not accumulate as drift.

## Shared State

Every reading shared across hub tasks lives in `latest_readings` in
`main_hub/main/main.c`, guarded by one mutex. Tasks copy under the lock and
release it before any sensor or I²C I/O, so the critical section stays a handful
of field copies and the motion task never waits behind a display update.

The mutex protects that struct, not the I²C bus. The LCD is the bus's only
device, so the bus needs no lock of its own; a second I²C device would change
that.

## The BLE Link

The remote node notifies on change rather than being polled, because motion
changes rarely and the hub should hear about a change at once.

The hub's client, `main_hub/main/remote_link.c`, runs entirely on NimBLE's host
task: it scans for the service UUID, connects, discovers the motion
characteristic and its notification descriptor, subscribes, and reads the value
once, because notifications arrive only on change. A failure on the remote's
side drops the connection and scans again. It never restarts the hub, whose
local sensors keep running, and the LCD shows `R:-` whenever the hub has no
current value from the remote.

A motion notification that fails to send makes the remote drop the hub.
Notifications carry only changes, so reconnecting is what makes the hub read the
current value instead of waiting for the next change.

The remote samples its PIR on a longer period than the hub, set in
`remote_node/main/include/remote_node_system_config.h`, to save power on a
battery. The HC-SR501 holds its output high for a delay set on the module
itself; set that delay to at least the sample period, or brief motion can fall
between samples.

## Display

`display_refresh_task` in `main_hub/main/main.c` formats the LCD's two lines:
local motion, remote motion, and distance on the first, temperature and humidity
on the second.

## Failure Policy

Every startup step goes through `ESP_ERROR_CHECK`, so a failed startup reboots
and retries rather than leaving the board idle. `components/crash_limit/` counts
each crash by its reset reason in NVS against the running image's SHA-256. Once
one crash type reaches `CRASH_LIMIT_MAX_CRASHES_PER_TYPE` on a build, the board
logs which and starts nothing until a different build is flashed. A brownout
counts like any other crash: a build or a setup that keeps crashing should stop
and say so.

While running, the hub degrades rather than crashes on sensor and BLE failures.
A bad DHT11 frame or a dropped link costs one reading or one connection, is
logged, and the next attempt carries on.

## Buses and Protocols

| Interface                     | Where                          | Why                                                                     |
| ----------------------------- | ------------------------------ | ----------------------------------------------------------------------- |
| I²C                           | LCD1602 via a PCF8574 backpack | the backpack drives the display's parallel pins from two wires          |
| DHT11 single-wire, bit-banged | DHT11                          | no ESP32 peripheral speaks it, so the driver times each bit in software |
| GPIO pulse timing             | HC-SR04                        | the echo pulse's width is the measurement                               |
| BLE GATT over NimBLE          | hub and remote node            | the hub is the client and subscribes; the remote is the server          |
