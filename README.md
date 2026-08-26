# Gainward LED Off

[![Release](https://github.com/NikitaMGrimm/gainward-led-off/actions/workflows/release.yml/badge.svg)](https://github.com/NikitaMGrimm/gainward-led-off/actions/workflows/release.yml)

Turns off Gainward and Palit GPU LEDs through the same NVAPI paths used by Gainward EXPERTool 11.13. It applies Off once and exits.

## Use

Open the [latest release](https://github.com/NikitaMGrimm/gainward-led-off/releases/latest), click `GainwardLedOff.exe` under **Assets**, then run it.

Build from source with `build.cmd`.

This uses undocumented NVAPI calls and raw I2C writes. Use it at your own risk.
