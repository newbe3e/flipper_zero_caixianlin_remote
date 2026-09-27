#include "caixianlin_types.h"

const char* mode_names[] = {NULL, "Shock", "Vibrate", "Beep"};

void caixianlin_timing_set_default(CaixianlinTiming* timing) {
    timing->sync_high_us = SYNC_HIGH_US;
    timing->sync_low_us = SYNC_LOW_US;
    timing->one_high_us = BIT_1_HIGH_US;
    timing->one_low_us = BIT_1_LOW_US;
    timing->zero_high_us = BIT_0_HIGH_US;
    timing->zero_low_us = BIT_0_LOW_US;
    timing->end_bits = PACKET_END_BITS;
}

static bool caixianlin_timing_duration_ok(uint16_t us) {
    return us >= TIMING_MIN_US && us <= TIMING_MAX_US;
}

bool caixianlin_timing_is_valid(const CaixianlinTiming* timing) {
    return caixianlin_timing_duration_ok(timing->sync_high_us) &&
           caixianlin_timing_duration_ok(timing->sync_low_us) &&
           caixianlin_timing_duration_ok(timing->one_high_us) &&
           caixianlin_timing_duration_ok(timing->one_low_us) &&
           caixianlin_timing_duration_ok(timing->zero_high_us) &&
           caixianlin_timing_duration_ok(timing->zero_low_us) &&
           timing->end_bits <= PACKET_END_BITS_MAX;
}

void app_init(CaixianlinRemoteApp* app) {
    app->station_id = 0;
    app->channel = 0;
    app->mode = 2;
    app->strength = 5;
    caixianlin_timing_set_default(&app->timing);
    caixianlin_timing_set_default(&app->rx_capture.captured_timing);
    app->running = true;
}
