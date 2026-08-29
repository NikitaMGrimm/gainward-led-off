# Gainward LED Off

[![Release](https://github.com/NikitaMGrimm/gainward-led-off/actions/workflows/release.yml/badge.svg)](https://github.com/NikitaMGrimm/gainward-led-off/actions/workflows/release.yml)

Turns off Gainward and Palit GPU LEDs through the same NVAPI paths used by EXPERTool 11.13 and ThunderMaster 4.17. It applies Off once and exits.

## Use

Open the [latest release](https://github.com/NikitaMGrimm/gainward-led-off/releases/latest), click `GainwardLedOff.exe` under **Assets**, then run it.

Build from source with `build.cmd`.

## Antivirus notice

Microsoft Defender incorrectly detects v0.1.0 as
`Trojan:Win32/Wacatac.C!ml`. The program contains no networking, persistence,
or background components. The false positive was submitted to Microsoft for
correction on August 29, 2026 and is pending review.

This uses undocumented NVAPI calls and raw I2C writes. Use it at your own risk.
