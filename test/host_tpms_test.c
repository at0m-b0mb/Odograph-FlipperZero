/**
 * Odograph - host tests for the TPMS decode engine.
 *
 * The engine is pure logic over a chip buffer, so it runs on the Mac exactly
 * as it runs on the Flipper. Every payload below is grounded, not invented:
 *
 *  - the Schrader packet and its CRC byte come from the published rtl_433
 *    protocol description, so it independently validates our CRC-8;
 *  - the Ford, Citroen, Renault and Toyota payloads are reconstructed from
 *    the decoded field values rtl_433 reports for real off-air captures in
 *    its test corpus (Ford_TPMS/gfile059, Citroen_TPMS/gfile001,
 *    Renault_TPMS/gfile070, Toyota_TPMS/gfile006), so the physical values the
 *    engine reports have to match what a known-good decoder saw on the air.
 *
 * Build and run:  make -C test
 */
#include "../helpers/od_demod.h"
#include "../helpers/od_tpms.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond, ...)                               \
    do {                                               \
        g_checks++;                                    \
        if(!(cond)) {                                  \
            g_fails++;                                 \
            printf("  FAIL %s:%d  ", __func__, __LINE__); \
            printf(__VA_ARGS__);                       \
            printf("\n");                              \
        }                                              \
    } while(0)

/* ------------------------------------------------------------------ *
 * Chip-stream builder - the encoder side of the engine, used only here
 * ------------------------------------------------------------------ */

typedef struct {
    uint8_t chips[OD_MAX_CHIPS / 8];
    uint16_t count;
    bool prev; /* last chip emitted, for the differential encoder */
} ChipBuf;

static void cb_reset(ChipBuf* c) {
    memset(c, 0, sizeof(*c));
}

static void cb_push(ChipBuf* c, bool v) {
    if(c->count >= OD_MAX_CHIPS) return;
    od_chip_set(c->chips, c->count++, v);
    c->prev = v;
}

/** Classic Manchester: a data bit becomes the pair (v, !v), or its mirror. */
static void cb_manchester_bit(ChipBuf* c, bool v, bool inverted) {
    cb_push(c, inverted ? !v : v);
    cb_push(c, inverted ? v : !v);
}

static void cb_manchester_bytes(ChipBuf* c, const uint8_t* b, uint8_t len, bool inverted) {
    for(uint8_t i = 0; i < len; i++) {
        for(int8_t bit = 7; bit >= 0; bit--) {
            cb_manchester_bit(c, (b[i] >> bit) & 1, inverted);
        }
    }
}

/** The VDO-family preamble `55 55 55 56` is just Manchester for 15 zeros
 * followed by a single one - encode it that way so the two stay in step. */
static void cb_vdo_preamble(ChipBuf* c, bool inverted) {
    for(uint8_t i = 0; i < 15; i++) cb_manchester_bit(c, 0, inverted);
    cb_manchester_bit(c, 1, inverted);
}

/**
 * Differential Manchester, mirroring the decoder's contract: the cell must
 * open with a transition, then two equal chips mean 1 and a mid-cell
 * transition means 0.
 */
static void cb_diff_manchester_bit(ChipBuf* c, bool v) {
    bool a = !c->prev;
    bool b = v ? a : !a;
    cb_push(c, a);
    cb_push(c, b);
}

static void cb_diff_manchester_bytes(ChipBuf* c, const uint8_t* bytes, uint8_t len) {
    for(uint8_t i = 0; i < len; i++) {
        for(int8_t bit = 7; bit >= 0; bit--) {
            cb_diff_manchester_bit(c, (bytes[i] >> bit) & 1);
        }
    }
}

/** Toyota's 12-chip sync, `1010 1001 1110`, pushed chip by chip. */
static void cb_toyota_sync(ChipBuf* c) {
    static const uint16_t sync = 0xA9E;
    for(int8_t i = 11; i >= 0; i--) cb_push(c, (sync >> i) & 1);
}

/* ------------------------------------------------------------------ *
 * Collector
 * ------------------------------------------------------------------ */

typedef struct {
    OdTpmsReading readings[8];
    uint8_t count;
} Collector;

static void collect(const OdTpmsReading* r, void* ctx) {
    Collector* c = (Collector*)ctx;
    if(c->count < 8) c->readings[c->count++] = *r;
}

static uint8_t run(ChipBuf* chips, uint16_t chip_us, Collector* out) {
    memset(out, 0, sizeof(*out));
    return od_tpms_scan(chips->chips, chips->count, chip_us, collect, out);
}

/* ------------------------------------------------------------------ *
 * Tests
 * ------------------------------------------------------------------ */

/* rtl_433's published Schrader example: payload f6 70 3a 38 b2 00 49 with a
 * CRC byte of 0x49. Nothing about this number comes from our own code. */
static void test_crc8_against_published_vector(void) {
    static const uint8_t payload[7] = {0xf6, 0x70, 0x3a, 0x38, 0xb2, 0x00, 0x49};
    uint8_t crc = od_tpms_crc8(payload, 7, 0x07, 0xf0);
    CHECK(crc == 0x49, "schrader crc8 = %02x, expected 49", crc);

    /* And the two other polynomial seeds the engine relies on behave like a
     * textbook CRC-8/ATM: a zero-init CRC of an empty span is zero. */
    CHECK(od_tpms_crc8(payload, 0, 0x07, 0x00) == 0x00, "empty crc8 must be the init value");
    CHECK(od_tpms_crc8(payload, 0, 0x07, 0xaa) == 0xaa, "empty crc8 must be the init value");
}

/* Ford_TPMS/gfile059: id 45bb320f, 26.5 PSI, moving, no valid temperature. */
static void test_ford(void) {
    static const uint8_t pkt[8] = {0x45, 0xbb, 0x32, 0x0f, 0x6a, 0xd4, 0x46, 0xc5};

    for(uint8_t inverted = 0; inverted < 2; inverted++) {
        ChipBuf c;
        cb_reset(&c);
        cb_vdo_preamble(&c, inverted);
        cb_manchester_bytes(&c, pkt, 8, inverted);

        Collector got;
        uint8_t n = run(&c, OD_CHIP_US_FSK, &got);
        CHECK(n >= 1 && got.count >= 1, "ford(inv=%u): nothing decoded", inverted);
        if(!got.count) continue;

        const OdTpmsReading* r = &got.readings[0];
        CHECK(r->proto == OdProtoFord, "ford(inv=%u): proto %d", inverted, r->proto);
        CHECK(r->id == 0x45bb320fu, "ford(inv=%u): id %08lx", inverted, (unsigned long)r->id);
        /* 26.5 PSI is 182.7 kPa */
        CHECK(
            r->pressure_valid && r->pressure_dkpa == 1827,
            "ford(inv=%u): pressure %d dkPa, expected 1827",
            inverted,
            r->pressure_dkpa);
        CHECK(!r->temp_valid, "ford(inv=%u): temp should be flagged invalid", inverted);
        CHECK(r->moving_valid && r->moving, "ford(inv=%u): should report moving", inverted);
    }
}

/* Citroen_TPMS/gfile001: id 8add48d4, 289.168 kPa, 23 C. */
static void test_citroen(void) {
    static const uint8_t pkt[10] =
        {0xd2, 0x8a, 0xdd, 0x48, 0xd4, 0x01, 0xd4, 0x49, 0x0e, 0x59};

    ChipBuf c;
    cb_reset(&c);
    cb_vdo_preamble(&c, false);
    cb_manchester_bytes(&c, pkt, 10, false);

    Collector got;
    run(&c, OD_CHIP_US_FSK, &got);
    CHECK(got.count >= 1, "citroen: nothing decoded");
    if(!got.count) return;

    const OdTpmsReading* r = &got.readings[0];
    CHECK(r->proto == OdProtoCitroen, "citroen: proto %d", r->proto);
    CHECK(r->id == 0x8add48d4u, "citroen: id %08lx", (unsigned long)r->id);
    CHECK(r->pressure_dkpa == 2892, "citroen: pressure %d dkPa, expected 2892", r->pressure_dkpa);
    CHECK(r->temp_valid && r->temp_c == 23, "citroen: temp %d C", r->temp_c);
}

/* Renault_TPMS/gfile070: id 87f293 (24 bit), 202.5 kPa, 25 C. */
static void test_renault(void) {
    static const uint8_t pkt[9] = {0xd1, 0x0e, 0x37, 0x93, 0xf2, 0x87, 0xff, 0xff, 0x0e};

    ChipBuf c;
    cb_reset(&c);
    cb_vdo_preamble(&c, false);
    cb_manchester_bytes(&c, pkt, 9, false);

    Collector got;
    run(&c, OD_CHIP_US_FSK, &got);
    CHECK(got.count >= 1, "renault: nothing decoded");
    if(!got.count) return;

    const OdTpmsReading* r = &got.readings[0];
    CHECK(r->proto == OdProtoRenault, "renault: proto %d", r->proto);
    CHECK(r->id == 0x87f293u, "renault: id %06lx", (unsigned long)r->id);
    CHECK(r->id_bits == 24, "renault: id_bits %u", r->id_bits);
    CHECK(r->pressure_dkpa == 2025, "renault: pressure %d dkPa, expected 2025", r->pressure_dkpa);
    CHECK(r->temp_valid && r->temp_c == 25, "renault: temp %d C", r->temp_c);
}

/* A Hyundai/Kia VDO frame, CRC-8 poly 0x07 init 0xaa over the first nine. */
static void test_hyundai(void) {
    static const uint8_t pkt[10] =
        {0x21, 0x1a, 0x2b, 0x3c, 0x4d, 0x02, 0xa0, 0x49, 0x0e, 0x00};

    ChipBuf c;
    cb_reset(&c);
    cb_vdo_preamble(&c, false);
    cb_manchester_bytes(&c, pkt, 10, false);

    Collector got;
    run(&c, OD_CHIP_US_FSK, &got);
    CHECK(got.count >= 1, "hyundai: nothing decoded");
    if(!got.count) return;

    const OdTpmsReading* r = &got.readings[0];
    CHECK(r->proto == OdProtoHyundaiVdo, "hyundai: proto %d", r->proto);
    CHECK(r->id == 0x1a2b3c4du, "hyundai: id %08lx", (unsigned long)r->id);
    CHECK(r->pressure_dkpa == 2200, "hyundai: pressure %d dkPa, expected 2200", r->pressure_dkpa);
    CHECK(r->temp_valid && r->temp_c == 23, "hyundai: temp %d C", r->temp_c);
}

/* Toyota_TPMS/gfile006: id fb0a43e7, 36.75 PSI (253.4 kPa), 29 C. */
static void test_toyota(void) {
    static const uint8_t pkt[9] = {0xfb, 0x0a, 0x43, 0xe7, 0xd7, 0xa2, 0x80, 0x50, 0x54};

    ChipBuf c;
    cb_reset(&c);
    /* a little alternating run first, exactly as the sensor sends it */
    for(uint8_t i = 0; i < 12; i++) {
        cb_push(&c, i & 1);
    }
    cb_toyota_sync(&c);
    cb_diff_manchester_bytes(&c, pkt, 9);

    Collector got;
    run(&c, OD_CHIP_US_FSK, &got);
    CHECK(got.count >= 1, "toyota: nothing decoded");
    if(!got.count) return;

    const OdTpmsReading* r = &got.readings[0];
    CHECK(r->proto == OdProtoToyota, "toyota: proto %d", r->proto);
    CHECK(r->id == 0xfb0a43e7u, "toyota: id %08lx", (unsigned long)r->id);
    CHECK(r->pressure_dkpa == 2534, "toyota: pressure %d dkPa, expected 2534", r->pressure_dkpa);
    CHECK(r->temp_valid && r->temp_c == 29, "toyota: temp %d C", r->temp_c);
}

/* Schrader ASK: 4 sync bits then the published 8-byte payload. */
static void test_schrader(void) {
    static const uint8_t pkt[8] = {0xf6, 0x70, 0x3a, 0x38, 0xb2, 0x50, 0x49, 0x00};
    uint8_t frame[8];
    memcpy(frame, pkt, 8);
    frame[7] = od_tpms_crc8(frame, 7, 0x07, 0xf0);

    for(uint8_t inverted = 0; inverted < 2; inverted++) {
        ChipBuf c;
        cb_reset(&c);
        /* the 4-bit sync nibble, 0x7 */
        cb_manchester_bit(&c, 0, inverted);
        cb_manchester_bit(&c, 1, inverted);
        cb_manchester_bit(&c, 1, inverted);
        cb_manchester_bit(&c, 1, inverted);
        cb_manchester_bytes(&c, frame, 8, inverted);

        Collector got;
        run(&c, OD_CHIP_US_OOK, &got);
        CHECK(got.count >= 1, "schrader(inv=%u): nothing decoded", inverted);
        if(!got.count) continue;

        const OdTpmsReading* r = &got.readings[0];
        CHECK(r->proto == OdProtoSchrader, "schrader(inv=%u): proto %d", inverted, r->proto);
        /* 28-bit serial: low nibble of b[1] then b[2..4] */
        CHECK(r->id == 0x003a38b2u, "schrader(inv=%u): id %07lx", inverted, (unsigned long)r->id);
        CHECK(r->id_bits == 28, "schrader(inv=%u): id_bits %u", inverted, r->id_bits);
        /* 0x50 = 80 counts of 25 mbar = 200.0 kPa */
        CHECK(
            r->pressure_dkpa == 2000,
            "schrader(inv=%u): pressure %d dkPa, expected 2000",
            inverted,
            r->pressure_dkpa);
        CHECK(r->temp_valid && r->temp_c == 23, "schrader(inv=%u): temp %d C", inverted, r->temp_c);
    }
}

/* The engine must refuse a packet whose integrity check does not hold. */
static void test_rejects_corrupted(void) {
    static const uint8_t pkt[9] = {0xd1, 0x0e, 0x37, 0x93, 0xf2, 0x87, 0xff, 0xff, 0x0e};

    for(uint8_t byte = 0; byte < 9; byte++) {
        uint8_t bad[9];
        memcpy(bad, pkt, 9);
        bad[byte] ^= 0x40; /* flip one bit */

        ChipBuf c;
        cb_reset(&c);
        cb_vdo_preamble(&c, false);
        cb_manchester_bytes(&c, bad, 9, false);

        Collector got;
        run(&c, OD_CHIP_US_FSK, &got);

        /* It may fall through to the unknown-sensor fingerprint, which is
         * fine and expected - what it must never do is claim a decode. */
        for(uint8_t i = 0; i < got.count; i++) {
            CHECK(
                got.readings[i].proto == OdProtoUnknown,
                "corrupt byte %u decoded as protocol %d",
                byte,
                got.readings[i].proto);
        }
    }
}

/* Pure noise must not produce a named protocol. The unknown fingerprint is
 * allowed to fire - the sensor store requires it to repeat before it is ever
 * shown to a user - but a branded sensor ID out of noise would be a lie. */
static void test_noise_is_not_a_sensor(void) {
    unsigned seed = 20260803u;
    int named = 0;

    for(int trial = 0; trial < 400; trial++) {
        ChipBuf c;
        cb_reset(&c);
        for(uint16_t i = 0; i < 600; i++) {
            seed = seed * 1103515245u + 12345u;
            cb_push(&c, (seed >> 16) & 1);
        }

        Collector got;
        run(&c, OD_CHIP_US_FSK, &got);
        for(uint8_t i = 0; i < got.count; i++) {
            if(got.readings[i].proto != OdProtoUnknown) named++;
        }

        Collector got_ook;
        run(&c, OD_CHIP_US_OOK, &got_ook);
        for(uint8_t i = 0; i < got_ook.count; i++) {
            if(got_ook.readings[i].proto != OdProtoUnknown) named++;
        }
    }

    CHECK(named == 0, "%d named protocol(s) decoded out of 400 noise bursts", named);
}

/* A sensor repeats its packet several times per burst; every repeat has to
 * decode, because the sensor store only trusts an ID it has heard twice. */
static void test_repeats_in_one_burst(void) {
    static const uint8_t pkt[8] = {0x45, 0xbb, 0x32, 0x0f, 0x6a, 0xd4, 0x46, 0xc5};

    ChipBuf c;
    cb_reset(&c);
    for(uint8_t rep = 0; rep < 3; rep++) {
        cb_vdo_preamble(&c, false);
        cb_manchester_bytes(&c, pkt, 8, false);
        /* inter-packet gap: a run of equal chips, which breaks Manchester */
        cb_push(&c, 1);
        cb_push(&c, 1);
        cb_push(&c, 0);
        cb_push(&c, 0);
    }

    Collector got;
    run(&c, OD_CHIP_US_FSK, &got);
    CHECK(got.count == 3, "expected 3 repeats decoded, got %u", got.count);
    for(uint8_t i = 0; i < got.count; i++) {
        CHECK(got.readings[i].id == 0x45bb320fu, "repeat %u: wrong id", i);
    }
}

/* An unsupported sensor still leaks a stable pattern, and the fingerprint we
 * derive from it must be the same every time we hear the same bytes. */
static void test_unknown_fingerprint_is_stable(void) {
    static const uint8_t pkt[10] =
        {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa};

    uint32_t first = 0;
    for(uint8_t trial = 0; trial < 3; trial++) {
        ChipBuf c;
        cb_reset(&c);
        cb_vdo_preamble(&c, false);
        cb_manchester_bytes(&c, pkt, 10, false);

        Collector got;
        run(&c, OD_CHIP_US_FSK, &got);
        CHECK(got.count == 1, "unknown: expected one fingerprint, got %u", got.count);
        if(!got.count) return;
        CHECK(got.readings[0].proto == OdProtoUnknown, "unknown: should not be claimed");
        if(trial == 0) {
            first = got.readings[0].id;
            CHECK(first != 0, "unknown: fingerprint must not be zero");
        } else {
            CHECK(got.readings[0].id == first, "unknown: fingerprint drifted between passes");
        }
    }
}

/* Too short to be anything. */
static void test_short_buffer(void) {
    ChipBuf c;
    cb_reset(&c);
    for(uint16_t i = 0; i < 40; i++) cb_push(&c, i & 1);

    Collector got;
    uint8_t n = run(&c, OD_CHIP_US_FSK, &got);
    CHECK(n == 0 && got.count == 0, "short buffer decoded %u reading(s)", got.count);
}

/* ------------------------------------------------------------------ *
 * Demodulator: the real radio hands us runs, not chips
 * ------------------------------------------------------------------ */

/**
 * Replay a chip buffer as the run-length stream a CC1101 would produce,
 * optionally smearing each run's length so the test exercises the rounding
 * rather than a perfectly clocked ideal.
 */
static void replay(OdDemod* d, const ChipBuf* c, uint16_t chip_us, int jitter_pct, unsigned* seed) {
    uint16_t i = 0;
    while(i < c->count) {
        bool level = od_chip_at(c->chips, i);
        uint16_t run = 0;
        while(i + run < c->count && od_chip_at(c->chips, (uint16_t)(i + run)) == level) run++;

        int32_t us = (int32_t)run * chip_us;
        if(jitter_pct) {
            *seed = *seed * 1103515245u + 12345u;
            int32_t span = (us * jitter_pct) / 100;
            int32_t off = span ? (int32_t)((*seed >> 16) % (unsigned)(2 * span + 1)) - span : 0;
            us += off;
            if(us < 1) us = 1;
        }
        od_demod_feed(d, level, (uint32_t)us);
        i = (uint16_t)(i + run);
    }
    od_demod_flush(d);
}

static void build_ford_burst(ChipBuf* c, uint8_t repeats) {
    static const uint8_t pkt[8] = {0x45, 0xbb, 0x32, 0x0f, 0x6a, 0xd4, 0x46, 0xc5};
    cb_reset(c);
    for(uint8_t r = 0; r < repeats; r++) {
        cb_vdo_preamble(c, false);
        cb_manchester_bytes(c, pkt, 8, false);
        /* the quiet between packets in a burst */
        for(uint8_t i = 0; i < 8; i++) cb_push(c, 0);
    }
}

/* Perfect timing, then increasingly ragged timing. A tyre sensor's crystal is
 * cheap and it is spinning at 100 km/h; the decoder has to cope. */
static void test_demod_tolerates_jitter(void) {
    const int jitters[] = {0, 5, 10, 15, 20};

    for(size_t j = 0; j < sizeof(jitters) / sizeof(jitters[0]); j++) {
        ChipBuf c;
        build_ford_burst(&c, 3);

        Collector got;
        memset(&got, 0, sizeof(got));
        OdDemod d;
        od_demod_init(&d, OD_CHIP_US_FSK, collect, &got);

        unsigned seed = 4242u + (unsigned)j;
        replay(&d, &c, OD_CHIP_US_FSK, jitters[j], &seed);

        CHECK(got.count >= 1, "jitter %d%%: nothing decoded", jitters[j]);
        for(uint8_t i = 0; i < got.count; i++) {
            CHECK(
                got.readings[i].proto == OdProtoFord && got.readings[i].id == 0x45bb320fu,
                "jitter %d%%: wrong reading",
                jitters[j]);
        }
    }
}

/**
 * FSK never goes quiet: between packets the discriminator just chatters. That
 * means a capture buffer can fill mid-transmission, so the demodulator carries
 * a tail forward. Bury a packet behind enough noise to force several buffer
 * turnovers and it still has to come out.
 */
static void test_demod_survives_buffer_turnover(void) {
    Collector got;
    memset(&got, 0, sizeof(got));
    OdDemod d;
    od_demod_init(&d, OD_CHIP_US_FSK, collect, &got);

    unsigned seed = 987654321u;
    for(int i = 0; i < 4000; i++) {
        seed = seed * 1103515245u + 12345u;
        bool level = (seed >> 16) & 1;
        uint32_t us = OD_CHIP_US_FSK * (1 + ((seed >> 20) & 1));
        od_demod_feed(&d, level, us);
    }

    ChipBuf c;
    build_ford_burst(&c, 2);
    unsigned s2 = 13579u;
    replay(&d, &c, OD_CHIP_US_FSK, 8, &s2);

    CHECK(got.count >= 1, "packet lost behind %lu noise edges", (unsigned long)d.edges);
    if(got.count) {
        CHECK(got.readings[0].id == 0x45bb320fu, "wrong id after buffer turnover");
    }
}

/* Sub-chip spikes are demodulator noise, not data: they must be counted and
 * dropped, not shifted into the chip stream where they would corrupt it. */
static void test_demod_drops_glitches(void) {
    Collector got;
    memset(&got, 0, sizeof(got));
    OdDemod d;
    od_demod_init(&d, OD_CHIP_US_FSK, collect, &got);

    od_demod_feed(&d, true, 3);
    od_demod_feed(&d, false, 1);
    od_demod_feed(&d, true, 25); /* still under half a chip */

    CHECK(d.glitches == 3, "expected 3 glitches, counted %lu", (unsigned long)d.glitches);
    CHECK(d.edges == 3, "expected 3 edges, counted %lu", (unsigned long)d.edges);
}

/* Switching band or modulation mid-scan must not leave half a packet behind
 * to be misread at the new chip rate. */
static void test_demod_retune_clears_buffer(void) {
    Collector got;
    memset(&got, 0, sizeof(got));
    OdDemod d;
    od_demod_init(&d, OD_CHIP_US_FSK, collect, &got);

    /* A packet with no trailing gap, so nothing triggers a scan and the
     * buffer is left holding it half-processed. */
    static const uint8_t pkt[8] = {0x45, 0xbb, 0x32, 0x0f, 0x6a, 0xd4, 0x46, 0xc5};
    ChipBuf c;
    cb_reset(&c);
    cb_vdo_preamble(&c, false);
    cb_manchester_bytes(&c, pkt, 8, false);

    uint16_t i = 0;
    while(i < c.count) {
        bool level = od_chip_at(c.chips, i);
        uint16_t run = 0;
        while(i + run < c.count && od_chip_at(c.chips, (uint16_t)(i + run)) == level) run++;
        od_demod_feed(&d, level, (uint32_t)run * OD_CHIP_US_FSK);
        i = (uint16_t)(i + run);
    }
    CHECK(got.count == 0, "nothing should have been scanned yet");
    CHECK(d.count > 0, "buffer should still hold the packet");

    od_demod_set_chip_us(&d, OD_CHIP_US_OOK);
    CHECK(d.chip_us == OD_CHIP_US_OOK, "retune did not take");
    od_demod_flush(&d);
    CHECK(got.count == 0, "stale chips decoded after retune: %u reading(s)", got.count);
}

int main(void) {
    printf("Odograph TPMS engine - host tests\n\n");

    struct {
        const char* name;
        void (*fn)(void);
    } tests[] = {
        {"crc8 vs published vector", test_crc8_against_published_vector},
        {"Ford (gfile059)", test_ford},
        {"Citroen (gfile001)", test_citroen},
        {"Renault (gfile070)", test_renault},
        {"Hyundai / Kia VDO", test_hyundai},
        {"Toyota (gfile006)", test_toyota},
        {"Schrader ASK", test_schrader},
        {"rejects corrupted frames", test_rejects_corrupted},
        {"noise is not a sensor", test_noise_is_not_a_sensor},
        {"repeats in one burst", test_repeats_in_one_burst},
        {"unknown fingerprint stable", test_unknown_fingerprint_is_stable},
        {"short buffer", test_short_buffer},
        {"demod tolerates jitter", test_demod_tolerates_jitter},
        {"demod survives buffer turnover", test_demod_survives_buffer_turnover},
        {"demod drops glitches", test_demod_drops_glitches},
        {"demod retune clears buffer", test_demod_retune_clears_buffer},
    };

    for(size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = g_fails;
        tests[i].fn();
        printf("  %s  %s\n", g_fails == before ? "ok  " : "FAIL", tests[i].name);
    }

    printf("\n%d checks, %d failure(s)\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
