#include "od_expose.h"

#include <string.h>

void od_expose_compute(const OdExposeSensor* sensors, uint8_t count, OdExposure* out) {
    memset(out, 0, sizeof(*out));
    if(!sensors) count = 0;

    for(uint8_t i = 0; i < count; i++) {
        out->sensors++;
        if(sensors[i].returned) out->persistence = true;

        if(sensors[i].decoded) {
            out->decoded++;
            /*
             * Only a serial we actually decoded counts towards the identity
             * budget. A heuristic fingerprint is derived from payload bytes
             * that may carry pressure or state as well as identity, so
             * treating it as 32 clean bits would overstate the case - and
             * overstating it is exactly what this app must not do.
             */
            out->bits = (uint16_t)(out->bits + sensors[i].id_bits);
        } else {
            out->unknown++;
        }
    }

    /*
     * How many other cars in the world would answer to this same set of
     * serials. Past 31 bits the answer is below one and the fingerprint is
     * effectively unique, so we stop pretending to a fraction and say so.
     */
    if(out->bits >= 31) {
        out->peers = 0;
        out->alone = (out->decoded > 0);
    } else {
        out->peers = OD_WORLD_VEHICLES >> out->bits;
        if(out->peers > 0) out->peers--; /* other vehicles, not counting this one */
        out->alone = (out->decoded > 0) && (out->peers == 0);
    }

    if(out->sensors == 0) {
        out->level = OdExposureNone;
    } else if(out->decoded == 0) {
        out->level = OdExposureLow;
    } else if(out->bits < 32) {
        out->level = OdExposureModerate;
    } else if(out->bits < 64) {
        out->level = OdExposureHigh;
    } else {
        out->level = OdExposureCritical;
    }

    /*
     * Hearing the same serial come back after a gap is the whole thesis
     * demonstrated: this is not a one-off reading, it is a handle that keeps
     * working. That is worth a full step.
     */
    if(out->persistence && out->level > OdExposureNone && out->level < OdExposureCritical) {
        out->level = (OdExposureLevel)(out->level + 1);
    }
}

const char* od_expose_level_name(OdExposureLevel level) {
    switch(level) {
    case OdExposureLow:
        return "LOW";
    case OdExposureModerate:
        return "MODERATE";
    case OdExposureHigh:
        return "HIGH";
    case OdExposureCritical:
        return "CRITICAL";
    default:
        return "NONE";
    }
}

const char* od_expose_level_hint(OdExposureLevel level) {
    switch(level) {
    case OdExposureLow:
        return "Heard, not identified";
    case OdExposureModerate:
        return "Down to a few cars";
    case OdExposureHigh:
        return "Unique in any city";
    case OdExposureCritical:
        return "Unique and repeating";
    default:
        return "Nothing on the air";
    }
}
