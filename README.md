# ESP32 Controller for Charlton & Jenrick Fireplace

This project uses an **ESP32-WROOM-32** and a **CC1101** 433 MHz transceiver to control a fireplace using a captured 2-FSK RF payload.  
It integrates with **Home Assistant via MQTT** and also exposes a **simple web interface** with ON/OFF buttons.

<div style="display:flex;margin:auto;height:550px;">
    <img src="./images/remote.jpeg" />
</div>

## Information

I have only tested this on my own fireplace which is an `i1800e`, but all of the following ranges below _should_ work as well, but they have not been tested.

| Fireplace | Tested | Working |
| --------- | ------ | ------- |
| i750e     | false  | unknown |
| i1000e    | false  | unknown |
| i1250e    | false  | unknown |
| i1500e    | false  | unknown |
| i1800e    | true   | Yes     |
| i2200e    | false  | unknown |

I plan to add reverse engineer more button features with the aim to evtually map the full controller, when time permits.

## Hardware

- ESP32-WROOM-32 dev board (e.g. ESP32 DevKit)
- CC1101 433 MHz module
- 3.3 V power (CC1101 must **not** be powered from 5 V)

**Note**: This has only been test with an `ESP32-WROOM-32`, but it is more than likely other ESP32 board will work as well, or need just some minor configuration changes to work. Pull requests are welcome.

### Wiring

| CC1101 pin | ESP32 pin | Notes           |
| ---------- | --------- | --------------- |
| VCC        | 3V3       | 3.3 V only      |
| GND        | GND       | Common ground   |
| CSN (CS)   | GPIO5     | Chip select     |
| SCK        | GPIO18    | SPI SCK         |
| SO (MISO)  | GPIO19    | SPI MISO        |
| SI (MOSI)  | GPIO23    | SPI MOSI        |
| GDO0       | GPIO21    | Used by library |
| GDO2       | —         | Not connected   |

## Software

- [PlatformIO](https://platformio.org/)
- Framework: Arduino (ESP32)
- Libraries:
  - `mfurga/cc1101` – CC1101 driver
  - `knolleary/PubSubClient` – MQTT client

### `platformio.ini`

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200

lib_deps =
  mfurga/cc1101@^1.2.0
  knolleary/PubSubClient@^2.8
```

### MQTT & Homeassistant

Mqtt is supported out of the box should work with Homeassistant seamlessly. Simply edit the `main.cpp` file and provide your mqtt user/pass details as well as your broker information. Should you have Mqtt auto discovery on the device will be added automatically in your HA. If not you may need to add it manually.

### Build & Install

Connect your ESP32-WROOM-32 dev board and run the following command:

```bash
platformio run -e esp32dev --target upload
```

Once the upload is complete, you can run the following to see the serial output from your module:

```bash
pio device monitor -b 115200
```

### Usage

Once an IP has been assigned, you can go view and interact with the controller from the web UI, simply visit the IP assigned on port `80` -> `${ASSIGNED_IP}:80`

<div style="display:flex;margin:auto;">
    <img src="./images/webui.png" />
</div>

### Contributing

#### LSP Config

I use `clangd` as my LSP. To get it to be aware of `platformio` libs you need to feed it PlatformIO’s compile flags & include paths. Run the following:

`pio run -t compiledb`

This should create a file called `compile_commands.json` in the root of the project.

#### Secrets

Rename `secrets-sample.h` in `src/` folder to `secrets.h` and add the needed information for wifi/mqtt.

### Contributors

A massive thanks to [@cthuwu_chan](https://www.reddit.com/user/cthuwu_chan/) who helped me decode the signal originally. :bow:
