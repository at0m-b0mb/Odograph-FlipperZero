#include "od_demod.h"

#include <string.h>

static void demod_count_packet(const OdTpmsReading* reading, void* context) {
    OdDemod* d = (OdDemod*)context;
    d->packets++;
    if(d->callback) d->callback(reading, d->context);
}

void od_demod_init(OdDemod* d, uint16_t chip_us, OdTpmsFoundCb callback, void* context) {
    memset(d, 0, sizeof(*d));
    d->callback = callback;
    d->context = context;
    od_demod_set_chip_us(d, chip_us);
}

void od_demod_set_chip_us(OdDemod* d, uint16_t chip_us) {
    if(chip_us < 8) chip_us = 8;
    d->chip_us = chip_us;
    d->half_chip_us = (uint16_t)(chip_us / 2);
    d->count = 0;
}

void od_demod_reset(OdDemod* d) {
    d->count = 0;
}

void od_demod_reset_stats(OdDemod* d) {
    d->edges = 0;
    d->glitches = 0;
    d->bursts = 0;
    d->packets = 0;
    d->count = 0;
}

void od_demod_flush(OdDemod* d) {
    if(d->count >= 64) {
        d->bursts++;
        od_tpms_scan(d->chips, d->count, d->chip_us, demod_count_packet, d);
    }

    /*
     * Keep the tail rather than starting clean. An FSK carrier never actually
     * goes quiet between packets - the discriminator just chatters - so a
     * buffer can fill in the middle of a transmission. Carrying the last
     * OD_CARRY_CHIPS forward means the next scan still sees a whole packet.
     */
    if(d->count > OD_CARRY_CHIPS) {
        uint16_t drop = (uint16_t)(d->count - OD_CARRY_CHIPS);
        for(uint16_t i = 0; i < OD_CARRY_CHIPS; i++) {
            od_chip_set(d->chips, i, od_chip_at(d->chips, (uint16_t)(drop + i)));
        }
        d->count = OD_CARRY_CHIPS;
    }
}

void od_demod_feed(OdDemod* d, bool level, uint32_t duration_us) {
    d->edges++;

    /*
     * Round the run to whole chips. Anything shorter than half a chip is a
     * demodulator glitch: dropping it merges the neighbours, which is the
     * least-wrong repair, and counting it gives the UI an honest noise
     * readout instead of a blank screen.
     */
    if(duration_us < d->half_chip_us) {
        d->glitches++;
        return;
    }

    uint32_t chips = (duration_us + d->half_chip_us) / d->chip_us;
    if(chips == 0) {
        d->glitches++;
        return;
    }

    /*
     * Neither Manchester nor differential Manchester can hold a level for
     * more than two chips, so a longer run is the gap around a packet. Scan
     * what we have and start the next burst here.
     *
     * The first chip of that gap is not gap at all: when a packet's last bit
     * ends low, its closing chip is indistinguishable from the silence that
     * follows and arrives merged into this run. Keeping one chip completes
     * that final cell - without it every packet decodes one bit short, which
     * is one bit short of its checksum.
     */
    if(chips > OD_MAX_RUN_CHIPS) {
        if(d->count < OD_MAX_CHIPS) {
            od_chip_set(d->chips, d->count++, level);
        }
        if(d->count >= 64) {
            d->bursts++;
            od_tpms_scan(d->chips, d->count, d->chip_us, demod_count_packet, d);
        }
        d->count = 0;
        return;
    }

    for(uint32_t i = 0; i < chips; i++) {
        if(d->count >= OD_MAX_CHIPS) {
            od_demod_flush(d);
            /* flush() carries a tail forward; if it somehow could not, bail
             * rather than write past the buffer. */
            if(d->count >= OD_MAX_CHIPS) {
                d->count = 0;
            }
        }
        od_chip_set(d->chips, d->count++, level);
    }
}
