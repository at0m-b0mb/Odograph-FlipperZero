#include "od_settings.h"
#include "od_radio.h" /* OD_BAND_COUNT, OdProfileCount */

#include <furi.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

/* /data is this app's private storage: /ext/apps_data/odograph/, no mkdir. */
#define OD_CONF_PATH    APP_DATA_PATH("odograph.conf")
#define OD_CONF_HEADER  "Odograph settings"
#define OD_CONF_VERSION 1

void od_settings_default(OdSettings* s) {
    furi_assert(s);
    /* 433.92 MHz FSK is what tyre sensors use everywhere outside North
     * America and Japan, and it is where most of the protocols we decode
     * live. Auto exists for when you genuinely do not know. */
    s->band_index = OD_BAND_DEFAULT;
    s->profile = OdProfileFsk;
    s->units = OdUnitsKpa;
    s->temp_f = false;
    s->confirm = OdConfirmTwo;
    s->sound = true;
    s->led = true;
    s->logging = false;
}

uint8_t od_settings_min_hits(const OdSettings* s) {
    switch(s->confirm) {
    case OdConfirmOne:
        return 1;
    case OdConfirmThree:
        return 3;
    default:
        return 2;
    }
}

static uint8_t clamp_u8(uint32_t v, uint8_t hi) {
    return v > hi ? hi : (uint8_t)v;
}

/* Absent keys keep this version's default, so a file written by an older
 * build never leaves a new field at zero. */
static void read_u8(FlipperFormat* ff, const char* key, uint8_t* dst, uint8_t hi) {
    uint32_t v = 0;
    flipper_format_rewind(ff);
    if(flipper_format_read_uint32(ff, key, &v, 1)) *dst = clamp_u8(v, hi);
}

static void read_bool(FlipperFormat* ff, const char* key, bool* dst) {
    uint32_t v = 0;
    flipper_format_rewind(ff);
    if(flipper_format_read_uint32(ff, key, &v, 1)) *dst = v != 0;
}

bool od_settings_load(OdSettings* s) {
    furi_assert(s);
    od_settings_default(s);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool found = false;

    do {
        if(!flipper_format_file_open_existing(ff, OD_CONF_PATH)) break;

        FuriString* header = furi_string_alloc();
        uint32_t version = 0;
        bool header_ok = flipper_format_read_header(ff, header, &version) &&
                         furi_string_equal(header, OD_CONF_HEADER);
        furi_string_free(header);
        if(!header_ok) break; /* not our file - leave the defaults standing */

        read_u8(ff, "Band", &s->band_index, (uint8_t)(OD_BAND_COUNT - 1));
        read_u8(ff, "Profile", &s->profile, (uint8_t)(OdProfileCount - 1));
        read_u8(ff, "Units", &s->units, (uint8_t)(OdUnitsCount - 1));
        read_u8(ff, "Confirm", &s->confirm, (uint8_t)(OdConfirmCount - 1));
        read_bool(ff, "TempF", &s->temp_f);
        read_bool(ff, "Sound", &s->sound);
        read_bool(ff, "Led", &s->led);
        read_bool(ff, "Logging", &s->logging);
        found = true;
    } while(false);

    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return found;
}

bool od_settings_save(const OdSettings* s) {
    furi_assert(s);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;

    do {
        if(!flipper_format_file_open_always(ff, OD_CONF_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, OD_CONF_HEADER, OD_CONF_VERSION)) break;
        flipper_format_write_comment_cstr(ff, "Odograph - edited by hand at your own risk");

        uint32_t v;
        v = s->band_index;
        if(!flipper_format_write_uint32(ff, "Band", &v, 1)) break;
        v = s->profile;
        if(!flipper_format_write_uint32(ff, "Profile", &v, 1)) break;
        v = s->units;
        if(!flipper_format_write_uint32(ff, "Units", &v, 1)) break;
        v = s->confirm;
        if(!flipper_format_write_uint32(ff, "Confirm", &v, 1)) break;
        v = s->temp_f ? 1 : 0;
        if(!flipper_format_write_uint32(ff, "TempF", &v, 1)) break;
        v = s->sound ? 1 : 0;
        if(!flipper_format_write_uint32(ff, "Sound", &v, 1)) break;
        v = s->led ? 1 : 0;
        if(!flipper_format_write_uint32(ff, "Led", &v, 1)) break;
        v = s->logging ? 1 : 0;
        if(!flipper_format_write_uint32(ff, "Logging", &v, 1)) break;
        ok = true;
    } while(false);

    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
