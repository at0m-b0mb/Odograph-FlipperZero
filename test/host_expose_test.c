/**
 * Odograph - host tests for the exposure arithmetic.
 *
 * These numbers end up on screen under a word like CRITICAL, so they have to
 * be defensible. Build and run: make -C test
 */
#include "../helpers/od_expose.h"

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        g_checks++;                                       \
        if(!(cond)) {                                     \
            g_fails++;                                    \
            printf("  FAIL %s:%d  ", __func__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while(0)

static void test_nothing_heard(void) {
    OdExposure e;
    od_expose_compute(NULL, 0, &e);
    CHECK(e.level == OdExposureNone, "empty set should be NONE, got %d", e.level);
    CHECK(e.sensors == 0 && e.bits == 0, "empty set should be empty");
    CHECK(!e.alone, "nothing heard is not 'alone'");
}

/* One 28-bit Schrader serial: 2^28 is 268 million, so roughly six of the
 * world's cars would answer to it. Identifying, but not yet unique. */
static void test_single_sensor(void) {
    OdExposeSensor s[1] = {{.id_bits = 28, .decoded = true, .returned = false}};
    OdExposure e;
    od_expose_compute(s, 1, &e);

    CHECK(e.sensors == 1 && e.decoded == 1 && e.unknown == 0, "counts wrong");
    CHECK(e.bits == 28, "bits %u, expected 28", e.bits);
    CHECK(e.peers == 4, "peers %lu, expected 4", (unsigned long)e.peers);
    CHECK(!e.alone, "one 28-bit serial is not unique worldwide");
    CHECK(e.level == OdExposureModerate, "level %d, expected MODERATE", e.level);
}

/* Two 32-bit serials is 64 bits: past the point where any population on earth
 * produces a collision. */
static void test_two_sensors_is_unique(void) {
    OdExposeSensor s[2] = {
        {.id_bits = 32, .decoded = true, .returned = false},
        {.id_bits = 32, .decoded = true, .returned = false},
    };
    OdExposure e;
    od_expose_compute(s, 2, &e);

    CHECK(e.bits == 64, "bits %u, expected 64", e.bits);
    CHECK(e.peers == 0 && e.alone, "64 bits should be unique worldwide");
    CHECK(e.level == OdExposureCritical, "level %d, expected CRITICAL", e.level);
}

/* A whole car. */
static void test_full_set(void) {
    OdExposeSensor s[4];
    for(int i = 0; i < 4; i++) {
        s[i].id_bits = 32;
        s[i].decoded = true;
        s[i].returned = false;
    }
    OdExposure e;
    od_expose_compute(s, 4, &e);

    CHECK(e.bits == 128, "bits %u, expected 128", e.bits);
    CHECK(e.level == OdExposureCritical, "four wheels must be CRITICAL");
    CHECK(!e.persistence, "no sensor returned, so persistence is not proven");
}

/* Heuristic fingerprints must not inflate the identity budget. */
static void test_unknown_sensors_contribute_no_bits(void) {
    OdExposeSensor s[3] = {
        {.id_bits = 32, .decoded = false, .returned = false},
        {.id_bits = 32, .decoded = false, .returned = false},
        {.id_bits = 32, .decoded = false, .returned = false},
    };
    OdExposure e;
    od_expose_compute(s, 3, &e);

    CHECK(e.sensors == 3 && e.unknown == 3 && e.decoded == 0, "counts wrong");
    CHECK(e.bits == 0, "unknown fingerprints must contribute 0 bits, got %u", e.bits);
    CHECK(!e.alone, "cannot claim uniqueness from fingerprints alone");
    CHECK(e.level == OdExposureLow, "level %d, expected LOW", e.level);
}

/* Hearing the same serial come back after a gap is the proof, and it moves
 * the needle by a full step. */
static void test_persistence_promotes(void) {
    OdExposeSensor s[1] = {{.id_bits = 28, .decoded = true, .returned = true}};
    OdExposure e;
    od_expose_compute(s, 1, &e);

    CHECK(e.persistence, "returned sensor should set persistence");
    CHECK(e.level == OdExposureHigh, "MODERATE + persistence should be HIGH, got %d", e.level);

    /* ...but it can never push past the top of the scale. */
    OdExposeSensor top[4];
    for(int i = 0; i < 4; i++) {
        top[i].id_bits = 32;
        top[i].decoded = true;
        top[i].returned = true;
    }
    od_expose_compute(top, 4, &e);
    CHECK(e.level == OdExposureCritical, "CRITICAL must stay CRITICAL, got %d", e.level);
}

/* Every level needs a name and a hint short enough for the screen. */
static void test_labels(void) {
    for(int l = OdExposureNone; l <= OdExposureCritical; l++) {
        const char* name = od_expose_level_name((OdExposureLevel)l);
        const char* hint = od_expose_level_hint((OdExposureLevel)l);
        CHECK(name && name[0], "level %d has no name", l);
        CHECK(hint && hint[0], "level %d has no hint", l);
        CHECK(strlen(hint) <= 21, "hint for level %d is %zu cols, max 21", l, strlen(hint));
    }
}

/* Mixed real and heuristic: the real ones carry the verdict. */
static void test_mixed(void) {
    OdExposeSensor s[3] = {
        {.id_bits = 32, .decoded = true, .returned = false},
        {.id_bits = 24, .decoded = true, .returned = false},
        {.id_bits = 32, .decoded = false, .returned = false},
    };
    OdExposure e;
    od_expose_compute(s, 3, &e);

    CHECK(e.sensors == 3 && e.decoded == 2 && e.unknown == 1, "counts wrong");
    CHECK(e.bits == 56, "bits %u, expected 56", e.bits);
    CHECK(e.level == OdExposureHigh, "level %d, expected HIGH", e.level);
    CHECK(e.alone, "56 bits is unique worldwide");
}

int main(void) {
    printf("Odograph exposure model - host tests\n\n");

    struct {
        const char* name;
        void (*fn)(void);
    } tests[] = {
        {"nothing heard", test_nothing_heard},
        {"one 28-bit serial", test_single_sensor},
        {"two serials are unique", test_two_sensors_is_unique},
        {"a full set of four", test_full_set},
        {"fingerprints add no bits", test_unknown_sensors_contribute_no_bits},
        {"persistence promotes", test_persistence_promotes},
        {"mixed real and heuristic", test_mixed},
        {"labels fit the screen", test_labels},
    };

    for(size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = g_fails;
        tests[i].fn();
        printf("  %s  %s\n", g_fails == before ? "ok  " : "FAIL", tests[i].name);
    }

    printf("\n%d checks, %d failure(s)\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
