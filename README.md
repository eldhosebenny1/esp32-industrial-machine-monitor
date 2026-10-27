# Industrial Machine Monitor

An ESP32 watchdog for a workshop machine. It counts vibration, watches the motor
current, checks a temperature sensor and looks for anyone standing in the machine
zone. If something goes wrong it cuts the relay, latches the fault, sounds a buzzer
and stops pretending everything is fine until you restart it.

It runs its own Wi-Fi access point, so there is no router and no internet needed.
Join the network, open the page and you have live readings.

Built as a college exhibition project.

## How it behaves

On boot the board prints a banner, keeps the relay off, and calibrates the current
sensor for about four seconds with the machine stopped. That baseline becomes the
zero-current reference. Then the relay closes and the machine starts, with three
seconds of grace before current is judged and ten seconds before the PIR is
trusted - PIR modules always fire once when they power up.

From there it cycles continuously. Each pass clears the pulse counter, waits
500 ms so the vibration count means something, then reads current and the
temperature line, refreshes the dashboard values and checks the fault conditions.
A real fault is latched: relay off, buzzer on, and the only way out is a restart.

A current reading outside the window on its own is only a warning. The machine
keeps running, because a jammed cutter and a belt running light both look
suspicious without being a reason to stop production.

## Fault conditions

| Fault | Trigger | Stops the machine |
| --- | --- | --- |
| Person in machine zone | PIR stays HIGH for 500 ms | Yes |
| High temperature | Temperature sensor line goes HIGH | Yes |
| Abnormal vibration | More than 20 pulses in a 500 ms window | Yes |
| Current out of range | Below 0.35 A or above 0.70 A | No, warning only |

## Hardware

| Part | Purpose |
| --- | --- |
| ESP32 DevKit | Controller, web server and access point |
| SW-420 (or similar) vibration module | Digital output, counted as pulses |
| ACS712 current sensor, 5 A version | Motor current, analog output |
| NTC temperature module | Digital output that goes HIGH when hot |
| PIR motion sensor | Presence in the machine area |
| 1-channel relay module | Switches the machine, active LOW |
| Active buzzer | Fault alarm |
| Small DC motor + driver | Stand-in for the machine during a demo |

## Wiring

| Signal | ESP32 GPIO | Notes |
| --- | --- | --- |
| Vibration module DO | 27 | Pulled up on the module, counted with a RISING interrupt |
| ACS712 output | 35 | Input-only pin, ADC attenuation set to 11 dB |
| Relay module IN | 33 | Active LOW, so LOW means the machine is powered |
| Buzzer | 25 | |
| PIR output | 32 | |
| NTC module DO | 26 | HIGH means hot |

The ACS712 output sits at half the supply when no current flows, which is around
2.5 V for a 5 V module. That is near the top of the ESP32 range, so feed it from
5 V and keep the attenuation at 11 dB, or power the sensor from 3.3 V and accept a
smaller span. The sketch reads the raw ADC value and converts with the 0.185 V/A
sensitivity of the 5 A part, so change `ACS_SENSITIVITY` if you use a 20 A or 30 A
module.

GPIO 35 is input-only, which is fine for an analog input but means it cannot drive
anything.

## Libraries

Nothing beyond the ESP32 board package is required. `WiFi`, `WebServer` and `math`
cover everything the sketch uses.

```
Boards Manager URL: https://espressif.github.io/arduino-esp32/package_esp32_index.json
Board: ESP32 Dev Module
```

## Setup

1. Change the access point password in `machine_monitor/machine_monitor.ino`. The
   committed placeholder is `YOUR_AP_PASSWORD`, used both in `WiFi.softAP()` and in
   the serial banner. WPA2 needs at least 8 characters.
2. Flash the board with the relay disconnected, or with the machine unplugged.
   Calibration assumes nothing is drawing current.
3. Open the serial monitor at 115200 baud and wait for `Calibration complete.`
4. Join the `machine` Wi-Fi network and open `http://192.168.4.1`.

The access point name is `machine` and the board always sits at `192.168.4.1`,
which is the default of the ESP32 soft AP.

## Dashboard

Six cards, refreshed once a second from `/data`:

| Card | Shows |
| --- | --- |
| Vibration | Pulse count for the last 500 ms window |
| Current | Motor current in amps, always positive |
| Temperature Sensor | `HIGH` or `NORMAL` |
| PIR Motion | `DETECTED` or `NO MOTION` |
| Motor Relay | `ON` or `OFF` |
| Buzzer | `ON` or `OFF` |

The status bar at the top turns green on `NORMAL`, amber on warnings such as
`CURRENT WARNING` or `HIGH TEMPERATURE`, and red on `MACHINE STOPPED`. The message
line underneath carries the fault reason.

## GET /data

```json
{
  "status": "NORMAL",
  "reason": "No fault",
  "vibration": 6,
  "current": 0.47,
  "temperature": 0,
  "motion": "NO MOTION",
  "relay": "ON",
  "buzzer": "OFF"
}
```

`temperature` is `1` for hot and `0` for normal, because the NTC board is a
threshold module rather than a temperature sensor. The page turns that into
`HIGH` or `NORMAL`.

## Tuning

| Setting | Default | What it does |
| --- | --- | --- |
| `ABNORMAL_VIBRATION` | 20 | Pulses allowed in one 500 ms window |
| `CURRENT_MINIMUM` | 0.35 A | Lower bound of the normal current window |
| `CURRENT_MAXIMUM` | 0.70 A | Upper bound of the normal current window |
| `ACS_SENSITIVITY` | 0.185 | Volts per amp, 5 A ACS712 |
| `MOTION_CONFIRM_TIME` | 500 ms | How long the PIR must stay HIGH before it counts |
| `STARTUP_TIME` | 3000 ms | Grace period after the relay closes before current is judged |
| `PIR_STARTUP_IGNORE` | 10000 ms | Time before the PIR is trusted after power-up |

Record the current with the motor off and again with it running to pick
`CURRENT_MINIMUM` and `CURRENT_MAXIMUM` for your motor. The defaults are for a
small 12 V gear motor.

## Things worth knowing

- The fault state is latched on purpose. A machine that restarts itself after
  detecting a person in the zone is worse than one that stays down.
- The combined `ABNORMAL CURRENT + HIGH TEMPERATURE` fault in `loop()` can never
  fire, because a high temperature already returns further up. It is left in as a
  reminder of the intended order if the temperature handling ever changes.
- Calibration runs unconditionally at boot. If you reset the board while the
  machine is running, the zero reference will be wrong and the current readings
  will drift, so always restart with the machine stopped.
- The relay module is active LOW. If yours is active HIGH, swap `RELAY_ON` and
  `RELAY_OFF` rather than rewiring.

## Repo layout

```
machine_monitor/machine_monitor.ino   Firmware and embedded dashboard
```

## License

MIT, see [LICENSE](LICENSE).
