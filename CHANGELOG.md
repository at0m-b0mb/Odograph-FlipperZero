# Changelog

## v1.0 — 2026-08-04

First release.

### Decoding
- Six TPMS protocol families: Ford, Renault, Citroën/Peugeot, Hyundai/Kia (VDO),
  Toyota and Schrader.
- Both FSK polarities tried on every burst; every candidate gated on the
  protocol's own CRC or checksum.
- Unrecognised tyre sensors reported as a labelled fingerprint that contributes
  no bits to the exposure score.
- A serial must decode cleanly twice before it is named (configurable, 1–3).

### Radio
- Internal CC1101, listen-only. No transmit path is compiled in.
- 315 / 433.92 / 434.10 MHz, FSK and ASK.
- Two FSK profiles: the firmware's stock wideband preset, and a TPMS-tuned
  register set at 19.2 kBaud / 203 kHz / 38 kHz deviation.
- Auto mode rotates band and modulation on a 4-second dwell.

### Screens
- **Listen** — live sensor list, or a listening screen with an honest readout of
  what the radio is hearing when nothing has decoded yet.
- **Sensor** — the serial rendered as a seven-segment number plate, plus
  readings, a sightings timeline, and the raw decoded bytes.
- **Exposure report** — identity bits on the air, and how many of the world's
  vehicles would answer to the same fingerprint. Switchable between everything
  heard and your own car.
- **Garage** — serials saved to the SD card and recognised on later runs.
- **How this works** — five animated frames on TPMS tracking.

### Tests
- 140 host checks across the decode engine and the exposure model.
- Vectors grounded in real off-air captures from the rtl_433 test corpus, plus a
  published Schrader CRC vector that validates the CRC-8 implementation itself.
- 400 random-noise bursts at both chip rates must yield zero named protocols.
