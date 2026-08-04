#include "od_tpms.h"

#include <string.h>

/* ------------------------------------------------------------------------
 * Bit plumbing
 * --------------------------------------------------------------------- */

uint8_t od_tpms_crc8(const uint8_t* data, size_t len, uint8_t poly, uint8_t init) {
    uint8_t rem = init;
    for(size_t i = 0; i < len; i++) {
        rem ^= data[i];
        for(uint8_t bit = 0; bit < 8; bit++) {
            rem = (rem & 0x80) ? (uint8_t)((rem << 1) ^ poly) : (uint8_t)(rem << 1);
        }
    }
    return rem;
}

static inline void bits_put(uint8_t* dst, uint16_t index, bool value) {
    uint8_t mask = (uint8_t)(1u << (7 - (index & 7)));
    if(value) {
        dst[index >> 3] |= mask;
    } else {
        dst[index >> 3] &= (uint8_t)~mask;
    }
}

static inline bool bits_get(const uint8_t* src, uint16_t index) {
    return (src[index >> 3] >> (7 - (index & 7))) & 1;
}

/** Copy @p nbits out of @p src starting at bit @p off, MSB-first, into @p dst. */
static void bits_extract(const uint8_t* src, uint16_t off, uint8_t* dst, uint16_t nbits) {
    memset(dst, 0, (size_t)((nbits + 7) / 8));
    for(uint16_t i = 0; i < nbits; i++) {
        bits_put(dst, i, bits_get(src, (uint16_t)(off + i)));
    }
}

/**
 * Classic Manchester: every data bit is a pair of chips that must differ.
 *
 * rtl_433 decodes these protocols off an inverted buffer and emits the second
 * chip of each pair; whether our CC1101 hands us that polarity or its
 * complement is not knowable in advance, so the caller runs us both ways and
 * lets the packet's own CRC settle it. @p emit_second picks which chip of the
 * pair carries the data.
 *
 * Stops at the first pair that does not transition - that is the end of the
 * Manchester region, and how we learn a packet's real length.
 *
 * @return number of data bits recovered
 */
static uint16_t mc_decode(
    const uint8_t* chips,
    uint16_t chip_count,
    uint16_t start,
    bool emit_second,
    uint8_t* out,
    uint16_t max_bits) {
    uint16_t bits = 0;
    uint16_t i = start;
    while(i + 1 < chip_count && bits < max_bits) {
        bool c1 = od_chip_at(chips, i);
        bool c2 = od_chip_at(chips, (uint16_t)(i + 1));
        if(c1 == c2) break; /* no mid-cell transition: Manchester is over */
        bits_put(out, bits++, emit_second ? c2 : c1);
        i = (uint16_t)(i + 2);
    }
    return bits;
}

/**
 * Differential Manchester, rtl_433 convention.
 *
 * Every data bit is a pair of chips (a, b). There must be a transition at the
 * cell boundary, so `a` has to differ from the previous cell's `b`. Inside the
 * cell: no transition (a == b) means 1, a transition means 0.
 *
 * Because the data lives in the *transitions*, this encoding is immune to
 * polarity - which is why Toyota needs only the phase tried both ways, not the
 * polarity.
 *
 * @param prev_chip the chip immediately before @p start, the clock reference
 * @return number of data bits recovered
 */
static uint16_t dm_decode(
    const uint8_t* chips,
    uint16_t chip_count,
    uint16_t start,
    bool prev_chip,
    uint8_t* out,
    uint16_t max_bits) {
    uint16_t bits = 0;
    uint16_t i = start;
    bool prev = prev_chip;
    while(i + 1 < chip_count && bits < max_bits) {
        bool a = od_chip_at(chips, i);
        if(a == prev) break; /* cell boundary did not transition: clock lost */
        bool b = od_chip_at(chips, (uint16_t)(i + 1));
        bits_put(out, bits++, a == b);
        prev = b;
        i = (uint16_t)(i + 2);
    }
    return bits;
}

/* A sensor ID of all-zeros or all-ones is a stuck decode, never a real part. */
static bool id_is_sane(uint32_t id, uint8_t id_bits) {
    uint32_t mask = (id_bits >= 32) ? 0xFFFFFFFFu : ((1u << id_bits) - 1u);
    return id != 0 && (id & mask) != mask;
}

static void reading_init(OdTpmsReading* r, OdProto proto, const uint8_t* raw, uint8_t raw_len) {
    memset(r, 0, sizeof(*r));
    r->proto = proto;
    r->raw_len = raw_len;
    memcpy(r->raw, raw, raw_len);
}

/* ------------------------------------------------------------------------
 * Protocol parsers
 *
 * Each one owns its own integrity check and refuses to emit anything that
 * fails it. Field layouts are documented in PROTOCOLS.md.
 * --------------------------------------------------------------------- */

/** Ford / Lincoln - 8 bytes, checksum is the sum of the first seven. */
static bool parse_ford(const uint8_t* b, OdTpmsReading* r) {
    uint8_t sum = 0;
    for(uint8_t i = 0; i < 7; i++) sum = (uint8_t)(sum + b[i]);
    if(sum != b[7]) return false;

    /* Flag bits the protocol is understood to use. Anything else set means we
     * are looking at a checksum collision, not a Ford packet. */
    if(b[6] & 0x90) return false;
    uint8_t state = b[6] & 0x4c;
    if(state != 0x08 && state != 0x04 && state != 0x44) return false;

    uint32_t id = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    if(!id_is_sane(id, 32)) return false;

    reading_init(r, OdProtoFord, b, 8);
    r->id = id;
    r->id_bits = 32;

    /* Pressure is quarter-PSI, with a ninth bit parked in the flags byte.
     * 0.25 PSI is 1.72369 kPa, so one count is 17.237 in our kPa x 10 units. */
    uint16_t psi_q = (uint16_t)(((b[6] & 0x20) << 3) | b[4]);
    r->pressure_dkpa = (int16_t)(((uint32_t)psi_q * 17237u + 500u) / 1000u);
    r->pressure_valid = true;

    /* Bit 7 of the temperature byte means "this byte is not a temperature". */
    if((b[5] & 0x80) == 0) {
        r->temp_c = (int16_t)((b[5] & 0x7f) - 56);
        r->temp_valid = true;
    }

    r->moving = (state == 0x44);
    r->moving_valid = true;
    return true;
}

/** Renault / Dacia - 9 bytes, CRC-8 poly 0x07 init 0x00. ID is little-endian. */
static bool parse_renault(const uint8_t* b, OdTpmsReading* r) {
    if(od_tpms_crc8(b, 8, 0x07, 0x00) != b[8]) return false;

    uint32_t id = ((uint32_t)b[5] << 16) | ((uint32_t)b[4] << 8) | b[3];
    if(!id_is_sane(id, 24)) return false;

    reading_init(r, OdProtoRenault, b, 9);
    r->id = id;
    r->id_bits = 24;

    uint16_t raw = (uint16_t)(((b[0] & 0x03) << 8) | b[1]); /* 0.75 kPa steps */
    r->pressure_dkpa = (int16_t)((raw * 15u) / 2u);
    r->pressure_valid = true;

    r->temp_c = (int16_t)(b[2] - 30);
    r->temp_valid = true;
    return true;
}

/** Citroen / Peugeot / Fiat - 10 bytes, XOR of bytes 1..9 is zero. */
static bool parse_citroen(const uint8_t* b, OdTpmsReading* r) {
    uint8_t x = 0;
    for(uint8_t i = 1; i < 10; i++) x ^= b[i];
    if(x != 0) return false;
    if(b[6] == 0 || b[7] == 0) return false; /* no sensor reads 0 kPa at -50 C */

    uint32_t id = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 8) | b[4];
    if(!id_is_sane(id, 32)) return false;

    reading_init(r, OdProtoCitroen, b, 10);
    r->id = id;
    r->id_bits = 32;
    r->pressure_dkpa = (int16_t)(((uint32_t)b[6] * 1364u + 50u) / 100u);
    r->pressure_valid = true;
    r->temp_c = (int16_t)(b[7] - 50);
    r->temp_valid = true;
    return true;
}

/** Hyundai / Kia (VDO) - 10 bytes, CRC-8 poly 0x07 init 0xaa over all nine. */
static bool parse_hyundai(const uint8_t* b, OdTpmsReading* r) {
    if(od_tpms_crc8(b, 9, 0x07, 0xaa) != b[9]) return false;

    uint32_t id = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 8) | b[4];
    if(!id_is_sane(id, 32)) return false;

    reading_init(r, OdProtoHyundaiVdo, b, 10);
    r->id = id;
    r->id_bits = 32;
    r->pressure_dkpa = (int16_t)(((uint32_t)b[6] * 1375u + 50u) / 100u);
    r->pressure_valid = true;
    r->temp_c = (int16_t)(b[7] - 50);
    r->temp_valid = true;
    return true;
}

/** Toyota (Pacific / TRW) - 9 bytes, CRC-8 poly 0x07 init 0x80, plus the
 * packet carries its own pressure twice, the second copy inverted. */
static bool parse_toyota(const uint8_t* b, OdTpmsReading* r) {
    if(od_tpms_crc8(b, 8, 0x07, 0x80) != b[8]) return false;

    uint16_t p1 = (uint16_t)(((b[4] & 0x7f) << 1) | (b[5] >> 7));
    uint16_t p2 = (uint16_t)(b[7] ^ 0xff);
    if(p1 != p2) return false;

    uint32_t id = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    if(!id_is_sane(id, 32)) return false;

    reading_init(r, OdProtoToyota, b, 9);
    r->id = id;
    r->id_bits = 32;

    /* quarter-PSI offset by -7 PSI (482.633 in kPa x 10), one count = 17.237 */
    int32_t dkpa = ((int32_t)p1 * 17237 - 482633 + 500) / 1000;
    if(dkpa < 0) dkpa = 0;
    r->pressure_dkpa = (int16_t)dkpa;
    r->pressure_valid = true;

    uint16_t traw = (uint16_t)(((b[5] & 0x7f) << 1) | (b[6] >> 7));
    r->temp_c = (int16_t)(traw - 40);
    r->temp_valid = true;
    return true;
}

/** Schrader (FCC MRXGG4) - 8 bytes after a 4-bit sync, CRC-8 poly 0x07 init
 * 0xf0. The high nibble of byte 0 is a fixed 0xF, which we use as a second
 * gate because this one is found by sliding rather than by a preamble match. */
static bool parse_schrader(const uint8_t* b, OdTpmsReading* r) {
    if((b[0] >> 4) != 0x0F) return false;
    if(od_tpms_crc8(b, 7, 0x07, 0xf0) != b[7]) return false;

    uint32_t id = ((uint32_t)(b[1] & 0x0F) << 24) | ((uint32_t)b[2] << 16) |
                  ((uint32_t)b[3] << 8) | b[4];
    if(!id_is_sane(id, 28)) return false;

    /* Temperature is offset by 50 and the sensor spec tops out at 205 C; a
     * byte outside that is a collision. */
    int16_t temp = (int16_t)(b[6] - 50);
    if(temp < -50 || temp > 150) return false;

    reading_init(r, OdProtoSchrader, b, 8);
    r->id = id;
    r->id_bits = 28;
    r->pressure_dkpa = (int16_t)((uint16_t)b[5] * 25u); /* 25 mbar/bit == 2.5 kPa */
    r->pressure_valid = true;
    r->temp_c = temp;
    r->temp_valid = true;
    return true;
}

/* ------------------------------------------------------------------------
 * Scanners
 * --------------------------------------------------------------------- */

/**
 * Slide a k-chip pattern over the chip buffer.
 * @return index of the chip *after* each match, via @p on_match
 */
typedef void (*OdSyncHit)(uint16_t after, void* ctx);

static void find_sync(
    const uint8_t* chips,
    uint16_t chip_count,
    uint32_t pattern,
    uint8_t pattern_len,
    OdSyncHit on_match,
    void* ctx) {
    if(chip_count < pattern_len) return;
    uint32_t mask = (pattern_len >= 32) ? 0xFFFFFFFFu : ((1u << pattern_len) - 1u);
    uint32_t sr = 0;
    for(uint16_t i = 0; i < chip_count; i++) {
        sr = (sr << 1) | (od_chip_at(chips, i) ? 1u : 0u);
        if(i + 1 >= pattern_len && (sr & mask) == pattern) {
            on_match((uint16_t)(i + 1), ctx);
        }
    }
}

typedef struct {
    const uint8_t* chips;
    uint16_t chip_count;
    bool emit_second;
    OdTpmsFoundCb callback;
    void* context;
    uint8_t found;
    /* longest clean Manchester run seen, for the unknown-sensor fallback */
    uint16_t best_bits;
    uint8_t best_head[4];
} OdScanCtx;

/**
 * The Continental/VDO family: Ford, Renault, Citroen and Hyundai-Kia all sit
 * behind the same `55 55 55 56` chip preamble and the same 52 us Manchester.
 * Only their payload length and integrity check tell them apart, so we try
 * the strongest check first and stop at the first protocol that accepts.
 */
static void vdo_on_sync(uint16_t after, void* ctx) {
    OdScanCtx* s = (OdScanCtx*)ctx;
    uint8_t buf[10];
    uint16_t bits = mc_decode(s->chips, s->chip_count, after, s->emit_second, buf, 80);

    if(bits > s->best_bits) {
        s->best_bits = bits;
        memcpy(s->best_head, buf, 4);
    }
    if(bits < 64) return;

    OdTpmsReading r;
    bool ok = false;
    if(bits >= 80 && parse_hyundai(buf, &r)) {
        ok = true;
    } else if(bits >= 80 && parse_citroen(buf, &r)) {
        ok = true;
    } else if(bits >= 72 && parse_renault(buf, &r)) {
        ok = true;
    } else if(parse_ford(buf, &r)) {
        ok = true;
    }

    if(ok) {
        s->found++;
        if(s->callback) s->callback(&r, s->context);
    }
}

static void toyota_on_sync(uint16_t after, void* ctx) {
    OdScanCtx* s = (OdScanCtx*)ctx;
    if(after < 2) return;

    /* The sync's trailing chip doubles as the differential clock reference.
     * Which chip that is depends on where the burst really started, so try
     * both plausible phases and let the CRC pick. */
    for(uint8_t phase = 0; phase < 2; phase++) {
        uint16_t start = (uint16_t)(after - 1 + phase);
        if(start == 0 || start + 1 >= s->chip_count) continue;
        bool prev = od_chip_at(s->chips, (uint16_t)(start - 1));

        uint8_t buf[9];
        uint16_t bits = dm_decode(s->chips, s->chip_count, start, prev, buf, 72);
        if(bits < 72) continue;

        OdTpmsReading r;
        if(parse_toyota(buf, &r)) {
            s->found++;
            if(s->callback) s->callback(&r, s->context);
            return;
        }
    }
}

/**
 * Schrader is ASK, so its bursts come out of silence with no chip preamble to
 * lock onto. We Manchester-decode the whole burst at both phases and both
 * polarities and slide a 64-bit window over the first few bit positions. The
 * fixed 0xF nibble plus the CRC-8 keep that brute force honest.
 */
static uint8_t scan_schrader(
    const uint8_t* chips,
    uint16_t chip_count,
    OdTpmsFoundCb callback,
    void* context) {
    uint8_t found = 0;
    uint8_t bitbuf[24];

    for(uint8_t phase = 0; phase < 2; phase++) {
        for(uint8_t pol = 0; pol < 2; pol++) {
            uint16_t bits = mc_decode(chips, chip_count, phase, pol != 0, bitbuf, 176);
            if(bits < 64) continue;

            uint16_t last = (uint16_t)(bits - 64);
            if(last > 12) last = 12; /* a real burst starts at the burst start */

            for(uint16_t off = 0; off <= last; off++) {
                uint8_t b[8];
                bits_extract(bitbuf, off, b, 64);

                OdTpmsReading r;
                if(parse_schrader(b, &r)) {
                    found++;
                    if(callback) callback(&r, context);
                    return found; /* one packet per burst is enough */
                }
            }
        }
    }
    return found;
}

/* FNV-1a, the same hash Odograph uses everywhere it needs a stable handle. */
static uint32_t fnv1a(const uint8_t* data, uint8_t len) {
    uint32_t h = 2166136261u;
    for(uint8_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

uint8_t od_tpms_scan(
    const uint8_t* chips,
    uint16_t chip_count,
    uint16_t chip_us,
    OdTpmsFoundCb callback,
    void* context) {
    if(!chips || chip_count < 64) return 0;

    /* Schrader's ASK sensors run at less than half the FSK chip rate, so the
     * sampling period the burst was captured at already tells us which family
     * could possibly be in it. */
    if(chip_us > 80) {
        return scan_schrader(chips, chip_count, callback, context);
    }

    OdScanCtx s = {
        .chips = chips,
        .chip_count = chip_count,
        .emit_second = false,
        .callback = callback,
        .context = context,
        .found = 0,
        .best_bits = 0,
        .best_head = {0},
    };

    /* `55 55 55 56` as we hear it, and the same preamble heard upside down. */
    s.emit_second = false;
    find_sync(chips, chip_count, 0x5556u, 16, vdo_on_sync, &s);
    s.emit_second = true;
    find_sync(chips, chip_count, 0xAAA9u, 16, vdo_on_sync, &s);

    /* Toyota's 12-chip sync, both polarities. */
    find_sync(chips, chip_count, 0xA9Eu, 12, toyota_on_sync, &s);
    find_sync(chips, chip_count, 0x561u, 12, toyota_on_sync, &s);

    /*
     * Nothing claimed it, but the burst was still a long clean Manchester run
     * behind a real preamble - which means a tyre sensor we do not have a
     * parser for. It is still leaking a stable pattern, and saying so is the
     * whole point of this app, so we hand back a fingerprint of the bytes
     * where TPMS protocols overwhelmingly keep their serial number.
     *
     * This is a heuristic, not a decode: the caller must confirm it by hearing
     * the same fingerprint more than once before showing it to anyone.
     */
    if(s.found == 0 && s.best_bits >= 64 && callback) {
        OdTpmsReading r;
        memset(&r, 0, sizeof(r));
        r.proto = OdProtoUnknown;
        r.id = fnv1a(s.best_head, 4);
        r.id_bits = 32;
        r.raw_len = 4;
        memcpy(r.raw, s.best_head, 4);
        callback(&r, context);
        s.found = 1;
    }

    return s.found;
}

/* ------------------------------------------------------------------------
 * Naming
 * --------------------------------------------------------------------- */

const char* od_tpms_proto_name(OdProto proto) {
    switch(proto) {
    case OdProtoFord:
        return "Ford";
    case OdProtoRenault:
        return "Renault";
    case OdProtoCitroen:
        return "Citroen";
    case OdProtoHyundaiVdo:
        return "Hyundai";
    case OdProtoToyota:
        return "Toyota";
    case OdProtoSchrader:
        return "Schrader";
    default:
        return "Unknown";
    }
}

uint8_t od_tpms_proto_id_bits(OdProto proto) {
    switch(proto) {
    case OdProtoRenault:
        return 24;
    case OdProtoSchrader:
        return 28;
    default:
        return 32;
    }
}

const char* od_tpms_proto_makes(OdProto proto) {
    switch(proto) {
    case OdProtoFord:
        return "Ford, Lincoln";
    case OdProtoRenault:
        return "Renault, Dacia";
    case OdProtoCitroen:
        return "Citroen, Peugeot, Fiat";
    case OdProtoHyundaiVdo:
        return "Hyundai, Kia, VDO";
    case OdProtoToyota:
        return "Toyota, Lexus";
    case OdProtoSchrader:
        return "GM, Opel, Nissan";
    default:
        return "unrecognised sensor";
    }
}
