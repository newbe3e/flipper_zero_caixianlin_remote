#include "caixianlin_storage.h"
#include <storage/storage.h>

// settings.txt holds one line:
//   station_id,channel[,sync_high,sync_low,one_high,one_low,zero_high,zero_low,end_bits]
// Files written by older versions only have the first two fields; the timing
// then stays at the protocol defaults.
#define SETTINGS_BUF_SIZE 96

bool caixianlin_storage_load(CaixianlinRemoteApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    File* file = storage_file_alloc(storage);
    bool success = false;

    if(storage_file_open(file, SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[SETTINGS_BUF_SIZE];
        size_t bytes_read = storage_file_read(file, buf, sizeof(buf) - 1);
        if(bytes_read > 0) {
            buf[bytes_read] = '\0';
            int station_id, channel;
            unsigned t[7];
            int fields = sscanf(
                buf,
                "%d,%d,%u,%u,%u,%u,%u,%u,%u",
                &station_id,
                &channel,
                &t[0],
                &t[1],
                &t[2],
                &t[3],
                &t[4],
                &t[5],
                &t[6]);
            if(fields >= 2 && station_id >= 0 && station_id <= 65535 && channel >= 0 &&
               channel <= 15) {
                app->station_id = (uint16_t)station_id;
                app->channel = (uint8_t)channel;
                success = true;
            }
            if(success && fields == 9) {
                bool in_range = t[6] <= PACKET_END_BITS_MAX;
                for(int k = 0; k < 6; k++) {
                    if(t[k] > TIMING_MAX_US) in_range = false;
                }
                CaixianlinTiming timing = {
                    .sync_high_us = (uint16_t)t[0],
                    .sync_low_us = (uint16_t)t[1],
                    .one_high_us = (uint16_t)t[2],
                    .one_low_us = (uint16_t)t[3],
                    .zero_high_us = (uint16_t)t[4],
                    .zero_low_us = (uint16_t)t[5],
                    .end_bits = (uint8_t)t[6],
                };
                if(in_range && caixianlin_timing_is_valid(&timing)) {
                    app->timing = timing;
                }
            }
        }
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return success;
}

void caixianlin_storage_save(CaixianlinRemoteApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    File* file = storage_file_alloc(storage);

    if(storage_file_open(file, SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[SETTINGS_BUF_SIZE];
        const CaixianlinTiming* t = &app->timing;
        int len = snprintf(
            buf,
            sizeof(buf),
            "%d,%d,%u,%u,%u,%u,%u,%u,%u",
            app->station_id,
            app->channel,
            (unsigned)t->sync_high_us,
            (unsigned)t->sync_low_us,
            (unsigned)t->one_high_us,
            (unsigned)t->one_low_us,
            (unsigned)t->zero_high_us,
            (unsigned)t->zero_low_us,
            (unsigned)t->end_bits);
        if(len > 0 && (size_t)len < sizeof(buf)) {
            storage_file_write(file, buf, (size_t)len);
        }
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}
