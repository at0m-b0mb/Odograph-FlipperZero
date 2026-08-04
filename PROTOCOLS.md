# Protocols

What Odograph decodes, and where the description came from.

Field layouts, checksum polynomials and symbol timings below are taken from the
public protocol documentation in the [rtl_433](https://github.com/merbanan/rtl_433)
project — the header comments in `src/devices/`, which describe the wire format
in prose. Those descriptions are facts about a radio protocol, not code. The
implementation in `helpers/od_tpms.c` is original and MIT licensed.

Test vectors are named against files in
[rtl_433_tests](https://github.com/merbanan/rtl_433_tests), reconstructed from
the field values rtl_433 reports for those real off-air captures.

---

## Physical layer

Two families, and the chip period is what separates them.

| Family | Modulation | Chip period | Chip rate | Encoding |
|---|---|---:|---:|---|
| Continental / VDO + Toyota | 2-FSK | 52 µs | 19.2 kchip/s | Manchester, or differential Manchester for Toyota |
| Schrader | ASK / OOK | 120 µs | 8.3 kchip/s | Manchester |

Every data bit is two chips, so a run at one level is never longer than two
chips. Odograph uses that as its burst boundary: a run longer than four chips
is the gap around a packet, not part of one.

### Manchester convention

rtl_433 decodes the VDO family off an **inverted** buffer and emits the second
chip of each pair. Whether a CC1101's frequency discriminator hands us that
polarity or its complement is not knowable in advance, so Odograph runs the
decode both ways and lets the packet's own CRC decide. In practice:

- search for `55 55 55 56` → the data bit is the **first** chip of each pair
- search for `AA AA AA A9` → the data bit is the **second** chip of each pair

Differential Manchester (Toyota) carries its data in the transitions, so it is
immune to polarity — only the phase has to be tried both ways.

### Receive presets

Odograph offers two FSK profiles for the internal CC1101.

**FSK** is the firmware's stock `2FSKDev476Async` preset: wide filter, 47.6 kHz
deviation. This is the preset the Flipper community captures raw FSK with, so
it is the conservative choice.

**FSK+** is a TPMS-tuned register set computed for what these sensors actually
send, with a 26 MHz crystal:

| Register | Value | Meaning |
|---|---|---|
| `MDMCFG3` | `0x83` | `DRATE_M` = 131 |
| `MDMCFG4` | `0x89` | channel filter 203.1 kHz, `DRATE_E` = 9 |
| `DEVIATN` | `0x44` | 38.09 kHz |

giving `(256 + 131) × 2⁹ / 2²⁸ × 26 MHz` = **19.19 kBaud**. The filter is kept
deliberately wider than the signal: a tyre sensor's crystal is cheap, unheated
and spinning, and a few tens of kHz of carrier offset is normal.

---

## Continental / VDO family

All four share the `55 55 55 56` chip preamble and 52 µs Manchester. Only the
payload length and the integrity check tell them apart, so Odograph tries the
strongest check first and stops at the first protocol that accepts.

### Ford

8 bytes. `II II II II PP TT FF CC`

| Field | Meaning |
|---|---|
| `b[0..3]` | 32-bit sensor serial |
| `b[4]` | pressure, quarter-PSI (bit 8 lives in `b[6] & 0x20`) |
| `b[5]` | temperature + 56 °C, **invalid when bit 7 is set** |
| `b[6]` | flags: `0x44` moving, `0x04` at rest, `0x08` learn |
| `b[7]` | checksum: sum of `b[0..6]`, mod 256 |

Odograph additionally rejects flag combinations the protocol is not understood
to use, because an 8-bit sum is a weak gate on its own.

*Vector:* `Ford_TPMS/gfile059` — serial `45bb320f`, 26.5 PSI, moving, no valid
temperature.

### Renault

9 bytes. `FF PP PT II II II ?? ?? CC`

| Field | Meaning |
|---|---|
| `b[0] >> 2` | flags |
| `(b[0] & 0x03) << 8 \| b[1]` | pressure, 0.75 kPa steps |
| `b[2]` | temperature − 30 °C |
| `b[5] << 16 \| b[4] << 8 \| b[3]` | 24-bit serial, **little-endian** |
| `b[6..7]` | unknown, usually `0xffff` |
| `b[8]` | CRC-8, poly `0x07`, init `0x00`, over `b[0..7]` |

*Vector:* `Renault_TPMS/gfile070` — serial `87f293`, 202.5 kPa, 25 °C.

### Citroën / Peugeot

10 bytes. `UU II II II II FR PP TT BB CC`

| Field | Meaning |
|---|---|
| `b[0]` | state, **not covered by the checksum** |
| `b[1..4]` | 32-bit serial |
| `b[5]` | flags and repeat counter |
| `b[6]` | pressure × 1.364 kPa |
| `b[7]` | temperature − 50 °C |
| `b[8]` | battery? |
| `b[9]` | XOR of `b[1..9]` is zero |

*Vector:* `Citroen_TPMS/gfile001` — serial `8add48d4`, 289.168 kPa, 23 °C.

### Hyundai / Kia (VDO)

10 bytes, same layout as Citroën, different check and scale.

| Field | Meaning |
|---|---|
| `b[6]` | pressure × 1.375 kPa |
| `b[9]` | CRC-8, poly `0x07`, init `0xaa`, over `b[0..8]` — including the state byte |

---

## Toyota (Pacific Industrial / TRW)

FSK, 52 µs **differential** Manchester, 12-chip sync `1010 1001 1110`.

9 bytes.

| Field | Meaning |
|---|---|
| `b[0..3]` | 32-bit serial |
| `(b[4] & 0x7f) << 1 \| b[5] >> 7` | pressure, quarter-PSI offset by −7 PSI |
| `(b[5] & 0x7f) << 1 \| b[6] >> 7` | temperature − 40 °C |
| `b[7]` | pressure again, inverted — must agree |
| `b[8]` | CRC-8, poly `0x07`, init `0x80`, over `b[0..7]` |

The duplicated, inverted pressure field makes this the best-gated protocol in
the set: a false positive has to satisfy a CRC-8 *and* a byte-level agreement.

*Vector:* `Toyota_TPMS/gfile006` — serial `fb0a43e7`, 36.75 PSI, 29 °C.

---

## Schrader (FCC MRXGG4)

ASK, 120 µs Manchester. Bursts come out of silence with no chip preamble to
lock onto, so Odograph decodes the whole burst at both phases and both
polarities and slides a 64-bit window over the first few bit positions.

17 nibbles: a 4-bit sync, then 8 bytes.

| Field | Meaning |
|---|---|
| `b[0] >> 4` | fixed `0xF` |
| `(b[1] & 0x0F) << 24 \| b[2..4]` | 28-bit serial |
| `b[5]` | pressure, 25 mbar per count |
| `b[6]` | temperature − 50 °C |
| `b[7]` | CRC-8, poly `0x07`, init `0xf0`, over `b[0..6]` |

Because this one is found by sliding rather than by a preamble match, the fixed
`0xF` nibble and a plausibility check on temperature back up the CRC.

*Vector:* the payload `f6 70 3a 38 b2 00 49` with CRC `0x49`, published in the
rtl_433 protocol description. This is the only vector in the suite whose CRC
byte comes from outside Odograph, so it validates the CRC-8 implementation
itself.

---

## Unrecognised sensors

A burst that sits behind a real preamble and decodes as a long clean Manchester
run, but that no parser claims, is reported as an **unrecognised fingerprint**:
an FNV-1a hash of the first four decoded bytes, which is where TPMS protocols
overwhelmingly keep their serial.

This is a heuristic, and Odograph says so everywhere it appears:

- it is labelled "Pattern, not a decode" on the sensor screen;
- it contributes **zero** bits to the exposure score;
- like every sensor, it must be heard twice before it is listed.

If a protocol keeps varying state in those four bytes the fingerprint will
drift, and Odograph will show it as several sensors rather than one. That is
the correct failure mode for a privacy tool: it under-claims.

---

## False positives

Every protocol here is gated on an 8-bit check, which is 1-in-256 per candidate
in the worst case. Three things keep that from producing phantom sensors:

1. a 16-chip preamble match (or, for Schrader, a fixed nibble plus a
   plausibility check) before the check is even attempted;
2. serial sanity — all-zero and all-ones serials are rejected;
3. **confirmation**: a serial has to decode cleanly twice before Odograph will
   name it. Tyre sensors repeat their packet several times per burst, so this
   is nearly free for a real sensor and ruinous for a collision.

The host suite includes 400 bursts of random edges at both chip rates. Zero
named protocols is the pass condition.
