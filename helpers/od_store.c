#include "od_store.h"

#include <furi_hal_rtc.h>
#include <datetime/datetime.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#include <string.h>

#define TAG "Odograph"

#define OD_GARAGE_PATH    APP_DATA_PATH("garage.txt")
#define OD_GARAGE_HEADER  "Odograph garage"
#define OD_GARAGE_VERSION 1

struct OdStore {
    FuriMutex* mutex;

    OdSensor sensors[OD_MAX_SENSORS];
    uint8_t count;
    uint32_t packets;

    OdGarageEntry garage[OD_GARAGE_MAX];
    uint8_t garage_count;
};

static uint32_t now_timestamp(void) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    return datetime_datetime_to_timestamp(&dt);
}

OdStore* od_store_alloc(void) {
    OdStore* store = malloc(sizeof(OdStore));
    memset(store, 0, sizeof(OdStore));
    store->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    return store;
}

void od_store_free(OdStore* store) {
    furi_assert(store);
    furi_mutex_free(store->mutex);
    free(store);
}

void od_store_clear(OdStore* store) {
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    memset(store->sensors, 0, sizeof(store->sensors));
    store->count = 0;
    store->packets = 0;
    furi_mutex_release(store->mutex);
}

/* Caller holds the lock. */
static int16_t garage_find(OdStore* store, uint32_t id, OdProto proto) {
    for(uint8_t i = 0; i < store->garage_count; i++) {
        if(store->garage[i].id == id && store->garage[i].proto == proto) return (int16_t)i;
    }
    return -1;
}

/* Caller holds the lock. */
static void track_push(OdSensor* s, int8_t rssi) {
    /* Store RSSI as an unsigned 0..255 so the timeline can be drawn without
     * worrying about sign; -128 dBm maps to 0. */
    uint8_t sample = (uint8_t)(rssi + 128);
    if(s->track_count < OD_TRACK_POINTS) {
        s->track[s->track_count++] = sample;
    } else {
        memmove(s->track, s->track + 1, OD_TRACK_POINTS - 1);
        s->track[OD_TRACK_POINTS - 1] = sample;
    }
}

OdStoreResult od_store_add(
    OdStore* store,
    const OdTpmsReading* reading,
    int8_t rssi,
    uint32_t frequency,
    uint8_t min_hits) {
    furi_assert(store);
    furi_assert(reading);
    if(min_hits == 0) min_hits = 1;

    furi_mutex_acquire(store->mutex, FuriWaitForever);
    store->packets++;

    uint32_t tick = furi_get_tick();
    OdSensor* s = NULL;

    for(uint8_t i = 0; i < store->count; i++) {
        if(store->sensors[i].id == reading->id && store->sensors[i].proto == reading->proto) {
            s = &store->sensors[i];
            break;
        }
    }

    if(!s) {
        if(store->count >= OD_MAX_SENSORS) {
            furi_mutex_release(store->mutex);
            return OdStoreIgnored;
        }
        s = &store->sensors[store->count++];
        memset(s, 0, sizeof(*s));
        s->id = reading->id;
        s->proto = reading->proto;
        s->id_bits = reading->id_bits;
        s->first_tick = tick;
        s->last_tick = tick;
        s->rssi_best = rssi;

        int16_t g = garage_find(store, reading->id, reading->proto);
        if(g >= 0) {
            s->known = true;
            s->known_since = store->garage[g].first_seen;
            store->garage[g].last_seen = now_timestamp();
            /*
             * A remembered serial turning up in a fresh session is the whole
             * argument, made on the device: this handle still works. Seed the
             * return counter so the exposure report says so even if the
             * sensor never goes quiet again while we watch.
             */
            s->returns = 1;
        }
    } else {
        /* A long silence, then the same serial again: a separate visit. */
        if(tick - s->last_tick > furi_ms_to_ticks(OD_RETURN_GAP_MS) && s->returns < 255) {
            s->returns++;
        }
        s->last_tick = tick;
    }

    uint16_t before = s->hits;
    if(s->hits < 0xFFFF) s->hits++;

    s->pressure_dkpa = reading->pressure_dkpa;
    s->pressure_valid = reading->pressure_valid;
    s->temp_c = reading->temp_c;
    s->temp_valid = reading->temp_valid;
    s->moving = reading->moving;
    s->moving_valid = reading->moving_valid;
    s->rssi = rssi;
    if(rssi > s->rssi_best) s->rssi_best = rssi;
    s->frequency = frequency;
    s->raw_len = reading->raw_len;
    memcpy(s->raw, reading->raw, sizeof(s->raw));
    track_push(s, rssi);

    OdStoreResult result = OdStoreUpdated;
    if(before < min_hits && s->hits >= min_hits) result = OdStoreConfirmed;

    furi_mutex_release(store->mutex);
    return result;
}

uint8_t od_store_count(OdStore* store) {
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    uint8_t n = store->count;
    furi_mutex_release(store->mutex);
    return n;
}

uint32_t od_store_packets(OdStore* store) {
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    uint32_t n = store->packets;
    furi_mutex_release(store->mutex);
    return n;
}

uint8_t od_store_confirmed_count(OdStore* store, uint8_t min_hits) {
    if(min_hits == 0) min_hits = 1;
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    uint8_t n = 0;
    for(uint8_t i = 0; i < store->count; i++) {
        if(store->sensors[i].hits >= min_hits) n++;
    }
    furi_mutex_release(store->mutex);
    return n;
}

bool od_store_get_confirmed(OdStore* store, uint8_t index, uint8_t min_hits, OdSensor* out) {
    if(min_hits == 0) min_hits = 1;
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    bool ok = false;
    uint8_t seen = 0;
    for(uint8_t i = 0; i < store->count; i++) {
        if(store->sensors[i].hits < min_hits) continue;
        if(seen == index) {
            *out = store->sensors[i];
            ok = true;
            break;
        }
        seen++;
    }
    furi_mutex_release(store->mutex);
    return ok;
}

bool od_store_get(OdStore* store, uint8_t index, OdSensor* out) {
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    bool ok = false;
    if(index < store->count) {
        *out = store->sensors[index];
        ok = true;
    }
    furi_mutex_release(store->mutex);
    return ok;
}

/* ------------------------------------------------------------- garage --- */

/* Caller holds the lock. */
static void garage_save_locked(OdStore* store) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    do {
        if(!flipper_format_file_open_always(ff, OD_GARAGE_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, OD_GARAGE_HEADER, OD_GARAGE_VERSION)) break;
        flipper_format_write_comment_cstr(
            ff, "Tyre sensor serials Odograph should recognise on sight.");

        uint32_t count = store->garage_count;
        if(!flipper_format_write_uint32(ff, "Count", &count, 1)) break;

        for(uint8_t i = 0; i < store->garage_count; i++) {
            char key[16];
            uint32_t v[4] = {
                store->garage[i].id,
                (uint32_t)store->garage[i].proto,
                store->garage[i].first_seen,
                store->garage[i].last_seen,
            };
            snprintf(key, sizeof(key), "S%u", i);
            if(!flipper_format_write_uint32(ff, key, v, 4)) break;

            snprintf(key, sizeof(key), "L%u", i);
            flipper_format_write_string_cstr(ff, key, store->garage[i].label);
        }
    } while(false);

    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

void od_store_garage_load(OdStore* store) {
    furi_assert(store);
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    store->garage_count = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    do {
        if(!flipper_format_file_open_existing(ff, OD_GARAGE_PATH)) break;

        FuriString* header = furi_string_alloc();
        uint32_t version = 0;
        bool header_ok = flipper_format_read_header(ff, header, &version) &&
                         furi_string_equal(header, OD_GARAGE_HEADER);
        furi_string_free(header);
        if(!header_ok) break;

        uint32_t count = 0;
        flipper_format_rewind(ff);
        if(!flipper_format_read_uint32(ff, "Count", &count, 1)) break;
        if(count > OD_GARAGE_MAX) count = OD_GARAGE_MAX;

        FuriString* label = furi_string_alloc();
        for(uint32_t i = 0; i < count; i++) {
            char key[16];
            uint32_t v[4] = {0};

            snprintf(key, sizeof(key), "S%lu", (unsigned long)i);
            flipper_format_rewind(ff);
            if(!flipper_format_read_uint32(ff, key, v, 4)) continue;

            OdGarageEntry* e = &store->garage[store->garage_count];
            memset(e, 0, sizeof(*e));
            e->id = v[0];
            e->proto = (v[1] < OdProtoCount) ? (OdProto)v[1] : OdProtoUnknown;
            e->first_seen = v[2];
            e->last_seen = v[3];

            snprintf(key, sizeof(key), "L%lu", (unsigned long)i);
            flipper_format_rewind(ff);
            if(flipper_format_read_string(ff, key, label)) {
                strncpy(e->label, furi_string_get_cstr(label), sizeof(e->label) - 1);
            }
            store->garage_count++;
        }
        furi_string_free(label);
    } while(false);

    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_release(store->mutex);
}

uint8_t od_store_garage_count(OdStore* store) {
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    uint8_t n = store->garage_count;
    furi_mutex_release(store->mutex);
    return n;
}

bool od_store_garage_get(OdStore* store, uint8_t index, OdGarageEntry* out) {
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    bool ok = false;
    if(index < store->garage_count) {
        *out = store->garage[index];
        ok = true;
    }
    furi_mutex_release(store->mutex);
    return ok;
}

bool od_store_garage_toggle(OdStore* store, uint32_t id, OdProto proto, const char* label) {
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    bool remembered;

    int16_t existing = garage_find(store, id, proto);
    if(existing >= 0) {
        for(uint8_t i = (uint8_t)existing; i + 1 < store->garage_count; i++) {
            store->garage[i] = store->garage[i + 1];
        }
        store->garage_count--;
        remembered = false;
    } else if(store->garage_count >= OD_GARAGE_MAX) {
        remembered = false; /* full - say so by not remembering */
    } else {
        OdGarageEntry* e = &store->garage[store->garage_count++];
        memset(e, 0, sizeof(*e));
        e->id = id;
        e->proto = proto;
        e->first_seen = now_timestamp();
        e->last_seen = e->first_seen;
        if(label) strncpy(e->label, label, sizeof(e->label) - 1);
        remembered = true;
    }

    /* Mirror the change onto any live sensor rows for the same serial. */
    for(uint8_t i = 0; i < store->count; i++) {
        if(store->sensors[i].id == id && store->sensors[i].proto == proto) {
            store->sensors[i].known = remembered;
            store->sensors[i].known_since = remembered ? now_timestamp() : 0;
        }
    }

    garage_save_locked(store);
    furi_mutex_release(store->mutex);
    return remembered;
}

void od_store_garage_clear(OdStore* store) {
    furi_mutex_acquire(store->mutex, FuriWaitForever);
    store->garage_count = 0;
    for(uint8_t i = 0; i < store->count; i++) {
        store->sensors[i].known = false;
        store->sensors[i].known_since = 0;
    }
    garage_save_locked(store);
    furi_mutex_release(store->mutex);
}
