# FireSport Stopwatch Wireless

A DIY wireless stopwatch for FireSport training and running competitions. The system starts timing from a base station and stops when a target sensor or laser gate is triggered on a wireless endpoint, that sends signal back to the base station. It is intended for courses where running long signal cables would be inconvenient, expose them to damage or cables are inconvetions.

> **Project status:** The v6 DIY hardware and firmware are nearing a first full testing version. Timing accuracy, radio range, battery life, and water resistance have not been independently measured or certified. Treat the values and environmental protection described here as design goals, not guarantees.

## System overview

- **Base station:** An ESP32-CAM module, two MAX7219-driven 7-segment displays with 8 digits each, operator buttons, and a buzzer. The displays show the first two lanes; times from lanes 3 and 4 are available to a connected PC.
- **Endpoints:** ESP32-CAM modules with a sensor input and transistor-switched outputs for a 3 W LED and active piezoelectric buzzer. The base assigns each endpoint to a lane and segment.
- **Wireless link:** ESP-NOW, with no router or internet connection required.
- **Sensors:** Magnetic target switches or laser-gate receivers, depending on the course setup. The endpoint firmware is shared across endpoints.
- **Power:** Battery powered; the current design uses 2-cell (2S) Li-ion packs. Battery monitoring and practical runtime testing are still in progress.

The base supports **1–4 lanes**. The current firmware supports **multiple segments (up to 9) for a single lane**; multi-segment timing across several lanes is not currently supported. The physical base display shows two lanes, with additional lane results sent to the PC interface.

## How a run works

1. The operator starts the stopwatch with the base's **START** button.
2. Endpoint sensor events are sent to the base over ESP-NOW. Events are acknowledged and retried until received.
3. The base records the stop time (and segment split times when configured), updates the displays, and reports results over USB serial for PC software.
4. The base's **RESTART** button resets a run. Holding it also displays available battery readings.

The endpoint's LED gives visual feedback when a sensor event is being sent and when it has been acknowledged. Endpoint and base buzzer behavior can be configured at the base.

## V6 DIY hardware

### Base station

The base is not intended to be submerged. Its enclosure is designed to offer some splash resistance, with particular care needed around the three operator buttons, the GX12 connectors (**2-pin START, 3-pin POWER, 5-pin display output**), and the panel-mounted USB-C female connector.

Design reference: [Base and endpoint enclosure](Hardware/Endpoint-BaseBox.pdf)

<!-- Add your v6 photos under photos/DIY-v6/ and update these image paths. -->
![V6 base — outside](photos/DIY-v6/base-outside.jpg)
![V6 base — inside](photos/DIY-v6/base-inside.jpg)

[More base photos: all sides and connector details](photos/DIY-v6/base-details/)

### Endpoints

Each endpoint has one sensor input. The magnetic-switch setup is intended for two target lines; laser-gate setups can use one to four lanes, with up to nine segments on a single lane in the current firmware. Assign each endpoint to its lane and segment during base setup.

The endpoint enclosure design is described in the [base and endpoint enclosure document](Hardware/Endpoint-BaseBox.pdf). Protect the GX12 3-pin connections carefully when assembling the endpoint; the enclosure and connectors should not be assumed waterproof.

![V6 endpoint — outside](photos/DIY-v6/endpoint-outside.jpg)
![V6 endpoint — inside](photos/DIY-v6/endpoint-inside.jpg)

[More endpoint photos: all sides and connector details](photos/DIY-v6/endpoint-details/)

### Signal box

The separate signal box connects through a GX12 3-pin connector and carries common **+BATT**, a **3 W LED**, and an **active piezoelectric buzzer**. The LED and buzzer need only +BATT and GND at the signal box; they are switched by transistors in the endpoint. The signal box is designed to resist splashing, but its water resistance has not been verified and should not be treated as a waterproof rating.

Design reference: [Signal box](Hardware/SignalBox.pdf)

![V6 signal box — outside](photos/DIY-v6/signal-box-outside.jpg)
![V6 signal box — inside](photos/DIY-v6/signal-box-inside.jpg)

### System photos

![Complete v6 stopwatch setup](photos/DIY-v6/complete-system.jpg)
![V6 setup on a course](photos/DIY-v6/course-setup.jpg)

## Firmware

The v6 Arduino sketches are in [`Codes/Hardware_codes/Base_v6`](Codes/Hardware_codes/Base_v6) and [`Codes/Hardware_codes/Endpoint_v6`](Codes/Hardware_codes/Endpoint_v6). Use the matching v6 firmware on the base and every endpoint.

Both sketches use a shared `protocol.h` for ESP-NOW message IDs, event acknowledgements, configuration, and battery messages. The two copies must remain identical. Each sketch also includes a local `config.h` for shared radio settings; provide the same settings to the base and endpoints and do not publish private keys.

The endpoint uses GPIO 16 for its sensor, which conflicts with ESP32-CAM PSRAM; disable PSRAM in the Arduino board settings as noted in the endpoint sketch. GPIO 13 is used for endpoint battery measurement. Because it is an ADC2 pin, measurement requires the radio/Wi-Fi to pause; reliable endpoint battery measurement is still being refined.

The base supports setup and endpoint pairing from its buttons, battery status display, serial commands, and USB serial output for PC integration. See the comments at the top of each sketch for the current pin assignments, setup flow, commands, and serial message formats.

## Wireless range and testing

The intended operating range is approximately **100–1000 m in favorable conditions**, depending on line of sight, interference, antenna placement, and ESP-NOW coverage. This range has not yet been validated in field testing. Relays may be explored in the future for longer courses such as marathons.

Before relying on the stopwatch in a competition, test the complete setup on the intended course. In particular, verify event reception and timing, endpoint assignment, battery readings and runtime, radio range, and enclosure resistance to the expected weather. This is a DIY project and is not a certified timing instrument.

## Project history

The project began with Arduino Nano boards and nRF24L01 radios. It later moved to ESP8266 and ESP-NOW, and then to ESP32-CAM modules to gain more processing speed and usable pins for the evolving design. The v6 base and endpoint firmware use ESP-NOW and the shared `protocol.h` message protocol.

## Contact and customization

Questions, orders, or requests for a specialized configuration can be discussed by email: [krivanekroman76@gmail.com](mailto:krivanekroman76@gmail.com).

## License

See the repository's [LICENSE](LICENSE) for the applicable license terms.
