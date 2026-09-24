# PGP-Badge ESP32 multi-client firmware

This directory adapts the original [PGP-Badge](https://github.com/bentomo/PGP-Badge) firmware for up to three simultaneous Pokémon GO BLE connections on an ESP32-WROOM-32. It uses ESP-IDF v4.4.8 and Bluedroid. The original project is distributed under the BSD 2-Clause license; retain the repository's [LICENSE](../LICENSE) and copyright notice when redistributing it.

The code has been built for ESP32. Simultaneous iOS and Android connection, authentication, and reconnection still require hardware testing.

## Local device data

`main/secrets.c` contains device-specific MAC, DEVICE_KEY, and BLOB values. It is ignored by Git. Never commit it, a firmware binary built with it, or a build directory to a public repository. To prepare a local build, copy `main/secrets.example.c` to `main/secrets.c`, then replace the placeholder values and remove its `#error` line. Use values from a device you own.

If this repository already has a local `main/secrets.c`, keep it local and do not replace it with the example.

## Build

In PowerShell:

```powershell
& "C:\esp\v4.4.8\esp-idf\export.ps1" # adjust to your IDF installation
cd pgpemu-esp32-multi
idf.py fullclean
idf.py build
```

The project expects `CONFIG_BTDM_CTRL_BLE_MAX_CONN >= 3`; `sdkconfig.defaults` requests 3.

## GPIO2 status LED

The firmware drives the development board's single-color GPIO2 LED with an active-high status pattern:

| BLE connections | LED pattern |
| --- | --- |
| 0 | Solid on while advertising |
| 1 | One short flash, then one long flash, repeated |
| 2 | Two short flashes, then one long flash, repeated |
| 3 | Three short flashes, then one long flash, repeated |

On the original PGP-Badge schematic GPIO2 is unconnected. This indicator is for an ESP32 development board with a GPIO2 LED or an external LED wired to GPIO2. For an active-low LED, change `STATUS_LED_ACTIVE_LEVEL` in `main/pgpemu.c` from `1` to `0`. The development board's red LED indicates power and is not controlled by this firmware. Game-event indication is not implemented yet because the Pokémon versus Pokéstop LED command patterns have not been verified on the target devices.

## Hardware validation

After flashing, connect an iPhone first and confirm authentication and resumed advertising. Connect an Android device, confirm authentication on both, then connect a third device. Disconnect one, verify the others stay connected, then reconnect it. The serial log reports `conn_id`, client slot, active count, authentication transitions, advertising restarts, and prepare/execute writes. A successful build alone does not establish successful simultaneous connections.
