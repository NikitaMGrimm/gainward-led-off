# Protocol notes

Mapped from Gainward EXPERTool 11.13.0.1 (`TBPanel.exe`, SHA-256 `8EDD6485D011AE544A2F4F7876FA52B73C9CE74AF451047A430D563806408010`).

Palit ThunderMaster 4.17.0.4 uses the same NVAPI wrappers, controller discovery, routing, raw backends, and illumination path (`ThPanel.exe`, SHA-256 `B4126D9A02ECE69475616A5C1329E0B42DD4A11812FD4359FBA7CF242586B6CA`).

## NVAPI

| Operation | Interface ID |
| --- | ---: |
| Enumerate GPUs | `0xE5AC921F` |
| I2C read | `0x4D7B0709` |
| I2C write | `0x283AC65A` |
| Illumination get | `0x3DBF5764` |
| Illumination set | `0x197D065E` |

I2C calls use the 64-byte v3 structure (`0x00030040`), port 1, and auxiliary values `{1, 0}`.

## Routing

EXPERTool probes the controller instead of using a PCI allowlist:

- Device `0x92`, register `0xF0`: `PALIT` identifies the raw controller and supplies its profile.
- NVIDIA Client Illumination: used when the returned zone layout matches profile 10 or 11.
- Device `0x10`: legacy fallback when Client Illumination discovery fails.

Six PCI subsystem values alter nonzero colors for profiles 10 and 11. They do not affect Off, which already uses RGB zero and brightness 100.

## Off

Raw profiles use these mode-9 writes:

- Profiles `1000..2999` ending in 0 or 1: `60 <- 01`, `6C <- 00 00 00 64`
- Profiles `1200..2999` ending in 2: add `E0 <- 00`
- Profiles `2000..2999` ending in 3 or 7: add `E0 <- 00`
- Profiles `2400..2999` ending in 6: add `E0 <- 00`

Client Illumination uses a `0x194C`-byte control buffer (version `0x0001194C`). Zones start at `0x4C` with stride `0xC8`. EXPERTool clears each zone's control field at `+0x70`, updates types 1, 3, and 4, then submits the complete buffer.

The legacy probe reads registers `00..0F` from device `0x10`. If `PALIT` is missing at `07..0B`, EXPERTool initializes `00`, `04`, and `08` with:

```text
7F 7F 7F 00
FF 00 FF 50
41 4C 49 54
```

It rereads the controller, accepts versions `" 1."`, `"2.0"`, or `"2.1"`, then writes `03 <- 00 00 00 FF`. The initialization is not PCI-gated and may write to an unidentified device at address `0x10`.

## Verified card

The tested Gainward RTX 5070 Ti returned `FF FF FF FF FF 06 "PALIT 250" 00`, which resolves to profile 2506. Its Off sequence was `E0`, `60`, then `6C` as listed above.

Normal Off is runtime state; persistence depends on the card. Do not redistribute EXPERTool or NVIDIA binaries.
