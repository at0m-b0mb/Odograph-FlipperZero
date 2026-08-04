#include "od_radio.h"
#include "od_demod.h"

#include <furi_hal_subghz.h>
#include <cc1101_regs.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <toolbox/level_duration.h>

#include <string.h>

#define TAG "Odograph"

/* Edges arrive from an interrupt, so they are parked in a stream buffer and
 * drained by our own thread. 512 entries is about 26 ms of dense FSK chatter,
 * far more headroom than the worker needs to keep up. */
#define OD_STREAM_ENTRIES 512
#define OD_WORKER_STACK   2048

/** How often the worker samples RSSI, in ms. */
#define OD_RSSI_PERIOD_MS 100

const OdBand od_bands[OD_BAND_COUNT] = {
    {.frequency = 315000000, .label = "315.00", .region = "US / Japan"},
    {.frequency = 433920000, .label = "433.92", .region = "EU / world"},
    {.frequency = 434100000, .label = "434.10", .region = "some PSA"},
};

const char* const od_profile_names[OdProfileCount] = {
    "FSK",
    "FSK+",
    "ASK",
    "Auto",
};

/**
 * TPMS-tuned 2-FSK receive registers for the CC1101 with its 26 MHz crystal.
 *
 * The stock firmware FSK preset is deliberately wide and clocked for a much
 * slower link. These values are computed for what tyre sensors actually send:
 *
 *   data rate  (256 + 131) * 2^9 / 2^28 * 26 MHz = 19.19 kBaud
 *   RX filter  26 MHz / (8 * (4 + 0) * 2^2)      = 203.1 kHz
 *   deviation  26 MHz / 2^17 * (8 + 4) * 2^4     = 38.09 kHz
 *
 * The filter stays deliberately wider than the signal because a tyre sensor's
 * crystal is cheap, unheated and spinning - a few tens of kHz of carrier
 * offset is normal and must not fall outside the channel.
 *
 * Format is register/value pairs, a 0,0 terminator, then the PA table. The PA
 * table is never used: Odograph does not transmit.
 */
static const uint8_t od_preset_fsk_tuned[] = {
    CC1101_IOCFG0,   0x0D, /* GDO0 = asynchronous serial data output      */
    CC1101_FIFOTHR,  0x07, /* ADC retention on, for the narrower filter   */
    CC1101_PKTCTRL0, 0x32, /* asynchronous serial, no whitening           */
    CC1101_PKTCTRL1, 0x04,
    CC1101_FSCTRL1,  0x06, /* IF 152.34 kHz                               */
    CC1101_MDMCFG0,  0x00,
    CC1101_MDMCFG1,  0x02,
    CC1101_MDMCFG2,  0x04, /* 2-FSK, no preamble or sync handling         */
    CC1101_MDMCFG3,  0x83, /* DRATE_M = 131                               */
    CC1101_MDMCFG4,  0x89, /* CHANBW 203.1 kHz, DRATE_E = 9               */
    CC1101_DEVIATN,  0x44, /* 38.09 kHz                                   */
    CC1101_MCSM0,    0x18, /* auto-calibrate on idle to RX                */
    CC1101_FOCCFG,   0x16,
    CC1101_AGCCTRL0, 0x91,
    CC1101_AGCCTRL1, 0x00,
    CC1101_AGCCTRL2, 0x07,
    CC1101_WORCTRL,  0xFB,
    CC1101_FREND0,   0x10,
    CC1101_FREND1,   0x56,
    0x00,            0x00, /* end of registers                            */
    0xC0,            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* PA table */
};

/** Auto walks these band/profile pairs in order. FSK first and twice over,
 * because the overwhelming majority of sensors on the road are FSK. */
typedef struct {
    uint8_t band;
    uint8_t profile;
} OdHopStep;

static const OdHopStep od_hop_plan[] = {
    {1, OdProfileFsk}, /* 433.92 FSK - most of the world  */
    {0, OdProfileFsk}, /* 315.00 FSK - North America      */
    {1, OdProfileAsk}, /* 433.92 ASK - older Schrader     */
    {0, OdProfileAsk}, /* 315.00 ASK                      */
};
#define OD_HOP_STEPS (sizeof(od_hop_plan) / sizeof(od_hop_plan[0]))

struct OdRadio {
    ViewDispatcher* view_dispatcher;
    uint32_t confirm_event;
    OdStore* store;

    FuriThread* thread;
    FuriStreamBuffer* stream;
    volatile bool running;

    /* requested by the GUI thread, adopted by the worker */
    volatile uint8_t want_band;
    volatile uint8_t want_profile;
    volatile uint8_t min_hits;

    /* what the radio is actually on */
    volatile uint8_t band;
    volatile uint8_t profile;
    volatile uint32_t hop_at;

    volatile float rssi;
    volatile uint32_t edges;
    volatile uint32_t bursts;

    /* worker-thread only */
    OdDemod demod;
    const SubGhzDevice* device;
    uint8_t hop_step;
    int8_t rssi_snapshot;
};

/* --------------------------------------------------------------- radio -- */

static uint16_t profile_chip_us(uint8_t profile) {
    return (profile == OdProfileAsk) ? OD_CHIP_US_OOK : OD_CHIP_US_FSK;
}

static void radio_tune(OdRadio* radio, uint8_t band, uint8_t profile) {
    if(band >= OD_BAND_COUNT) band = OD_BAND_DEFAULT;
    if(profile >= OdProfileAuto) profile = OdProfileFsk;

    subghz_devices_idle(radio->device);

    switch(profile) {
    case OdProfileAsk:
        subghz_devices_load_preset(radio->device, FuriHalSubGhzPresetOok270Async, NULL);
        break;
    case OdProfileFskTuned:
        subghz_devices_load_preset(
            radio->device, FuriHalSubGhzPresetCustom, (uint8_t*)od_preset_fsk_tuned);
        break;
    default:
        subghz_devices_load_preset(radio->device, FuriHalSubGhzPreset2FSKDev476Async, NULL);
        break;
    }

    subghz_devices_set_frequency(radio->device, od_bands[band].frequency);
    subghz_devices_flush_rx(radio->device);
    subghz_devices_set_rx(radio->device);

    radio->band = band;
    radio->profile = profile;
    od_demod_set_chip_us(&radio->demod, profile_chip_us(profile));
}

/* Runs in interrupt context: park the edge and get out. */
static void od_radio_capture(bool level, uint32_t duration, void* context) {
    OdRadio* radio = (OdRadio*)context;
    LevelDuration ld = level_duration_make(level, duration);
    furi_stream_buffer_send(radio->stream, &ld, sizeof(LevelDuration), 0);
}

/* Called from the worker thread by the demodulator, once per decoded packet. */
static void od_radio_on_packet(const OdTpmsReading* reading, void* context) {
    OdRadio* radio = (OdRadio*)context;

    OdStoreResult result = od_store_add(
        radio->store,
        reading,
        radio->rssi_snapshot,
        od_bands[radio->band].frequency,
        radio->min_hits);

    if(result == OdStoreConfirmed && radio->view_dispatcher) {
        view_dispatcher_send_custom_event(radio->view_dispatcher, radio->confirm_event);
    }
}

static int32_t od_radio_worker(void* context) {
    OdRadio* radio = (OdRadio*)context;

    subghz_devices_init();
    radio->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!radio->device) {
        FURI_LOG_E(TAG, "no internal CC1101");
        subghz_devices_deinit();
        radio->running = false;
        return 0;
    }

    subghz_devices_begin(radio->device);
    subghz_devices_reset(radio->device);

    radio->hop_step = 0;
    uint8_t band = radio->want_band;
    uint8_t profile = radio->want_profile;
    if(profile == OdProfileAuto) {
        band = od_hop_plan[0].band;
        profile = od_hop_plan[0].profile;
    }
    radio_tune(radio, band, profile);
    subghz_devices_start_async_rx(radio->device, od_radio_capture, radio);

    uint32_t next_rssi = furi_get_tick();
    radio->hop_at = furi_get_tick() + furi_ms_to_ticks(OD_HOP_DWELL_MS);
    uint8_t applied_band = radio->want_band;
    uint8_t applied_profile = radio->want_profile;

    while(radio->running) {
        LevelDuration ld;
        size_t got = furi_stream_buffer_receive(
            radio->stream, &ld, sizeof(LevelDuration), furi_ms_to_ticks(10));
        if(got == sizeof(LevelDuration)) {
            od_demod_feed(
                &radio->demod, level_duration_get_level(ld), level_duration_get_duration(ld));
            radio->edges = radio->demod.edges;
            radio->bursts = radio->demod.bursts;
            continue; /* drain hard while there is anything to drain */
        }

        uint32_t now = furi_get_tick();

        if(now >= next_rssi) {
            next_rssi = now + furi_ms_to_ticks(OD_RSSI_PERIOD_MS);
            float dbm = subghz_devices_get_rssi(radio->device);
            radio->rssi = dbm;
            radio->rssi_snapshot = (int8_t)dbm;
        }

        /*
         * Retuning tears down the capture timer, so it only ever happens here,
         * on the worker's own thread, between drains - never from the GUI.
         */
        bool settings_changed =
            (radio->want_band != applied_band) || (radio->want_profile != applied_profile);
        bool hop_due = (radio->want_profile == OdProfileAuto) && (now >= radio->hop_at);

        if(settings_changed || hop_due) {
            applied_band = radio->want_band;
            applied_profile = radio->want_profile;

            uint8_t next_band = applied_band;
            uint8_t next_profile = applied_profile;
            if(applied_profile == OdProfileAuto) {
                if(hop_due && !settings_changed) {
                    radio->hop_step = (uint8_t)((radio->hop_step + 1) % OD_HOP_STEPS);
                } else {
                    radio->hop_step = 0;
                }
                next_band = od_hop_plan[radio->hop_step].band;
                next_profile = od_hop_plan[radio->hop_step].profile;
            }

            subghz_devices_stop_async_rx(radio->device);
            furi_stream_buffer_reset(radio->stream);
            radio_tune(radio, next_band, next_profile);
            subghz_devices_start_async_rx(radio->device, od_radio_capture, radio);

            radio->hop_at = furi_get_tick() + furi_ms_to_ticks(OD_HOP_DWELL_MS);
        }
    }

    subghz_devices_stop_async_rx(radio->device);
    subghz_devices_idle(radio->device);
    subghz_devices_sleep(radio->device);
    subghz_devices_end(radio->device);
    subghz_devices_deinit();
    radio->device = NULL;
    return 0;
}

/* ----------------------------------------------------------------- api -- */

OdRadio* od_radio_alloc(ViewDispatcher* view_dispatcher, uint32_t confirm_event, OdStore* store) {
    OdRadio* radio = malloc(sizeof(OdRadio));
    memset(radio, 0, sizeof(OdRadio));

    radio->view_dispatcher = view_dispatcher;
    radio->confirm_event = confirm_event;
    radio->store = store;
    radio->want_band = OD_BAND_DEFAULT;
    radio->want_profile = OdProfileFsk;
    radio->min_hits = 2;
    radio->band = OD_BAND_DEFAULT;
    radio->profile = OdProfileFsk;
    radio->rssi = -127.0f;
    radio->rssi_snapshot = -127;

    radio->stream = furi_stream_buffer_alloc(
        sizeof(LevelDuration) * OD_STREAM_ENTRIES, sizeof(LevelDuration));
    od_demod_init(&radio->demod, OD_CHIP_US_FSK, od_radio_on_packet, radio);

    return radio;
}

void od_radio_free(OdRadio* radio) {
    furi_assert(radio);
    od_radio_stop(radio);
    furi_stream_buffer_free(radio->stream);
    free(radio);
}

void od_radio_configure(OdRadio* radio, uint8_t band_index, uint8_t profile, uint8_t min_hits) {
    furi_assert(radio);
    if(band_index >= OD_BAND_COUNT) band_index = OD_BAND_DEFAULT;
    if(profile >= OdProfileCount) profile = OdProfileFsk;
    radio->want_band = band_index;
    radio->want_profile = profile;
    radio->min_hits = min_hits ? min_hits : 1;
}

void od_radio_start(OdRadio* radio) {
    furi_assert(radio);
    if(radio->running) return;

    od_demod_reset_stats(&radio->demod);
    radio->edges = 0;
    radio->bursts = 0;
    furi_stream_buffer_reset(radio->stream);

    radio->running = true;
    radio->thread = furi_thread_alloc_ex("OdRadio", OD_WORKER_STACK, od_radio_worker, radio);
    furi_thread_start(radio->thread);
}

void od_radio_stop(OdRadio* radio) {
    furi_assert(radio);
    if(!radio->thread) return;

    radio->running = false;
    furi_thread_join(radio->thread);
    furi_thread_free(radio->thread);
    radio->thread = NULL;
}

bool od_radio_is_running(OdRadio* radio) {
    return radio->running && radio->thread;
}

float od_radio_rssi(OdRadio* radio) {
    return radio->rssi;
}

uint32_t od_radio_edges(OdRadio* radio) {
    return radio->edges;
}

uint32_t od_radio_bursts(OdRadio* radio) {
    return radio->bursts;
}

uint8_t od_radio_band(OdRadio* radio) {
    return radio->band;
}

uint8_t od_radio_profile(OdRadio* radio) {
    return radio->profile;
}

uint32_t od_radio_hop_remaining(OdRadio* radio) {
    if(radio->want_profile != OdProfileAuto || !radio->running) return 0;
    uint32_t now = furi_get_tick();
    uint32_t at = radio->hop_at;
    if(now >= at) return 0;

    uint32_t hz = furi_kernel_get_tick_frequency();
    if(hz == 0) hz = 1000;
    return (uint32_t)(((uint64_t)(at - now) * 1000u) / hz);
}
