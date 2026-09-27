#include "caixianlin_protocol.h"

// How many HIGH/LOW pairs after a packet to inspect for its trailing 0 bits
#define TRAILING_SCAN_MAX_PAIRS 40

static uint16_t caixianlin_protocol_clamp_us(int32_t value) {
    if(value < TIMING_MIN_US) return TIMING_MIN_US;
    if(value > TIMING_MAX_US) return TIMING_MAX_US;
    return (uint16_t)value;
}

// Does this sample pair look like a sync pulse?
// t1 is the HIGH duration (positive), t2 the LOW duration (stored negative).
static bool caixianlin_protocol_is_sync(int32_t t1, int32_t t2) {
    return t1 > SYNC_HIGH_MIN_US && t1 < SYNC_HIGH_MAX_US && t2 < -SYNC_LOW_MIN_US &&
           t2 > -SYNC_LOW_MAX_US;
}

// Build the signal transmission buffer
void caixianlin_protocol_encode_message(CaixianlinRemoteApp* app) {
    TxState* tx = &app->tx_state;
    const CaixianlinTiming* t = &app->timing;
    size_t idx = 0;

    // Beep carries no intensity, and the protocol intensity range is 0-99
    uint8_t strength = (app->mode == MODE_BEEP) ? 0 : app->strength;
    if(strength > MAX_STRENGTH) strength = MAX_STRENGTH;

    uint8_t checksum =
        caixianlin_protocol_checksum(app->station_id, app->channel, app->mode, strength);

    uint8_t bits[PACKET_DATA_BITS];
    size_t bit_idx = 0;

    for(int i = 15; i >= 0; i--)
        bits[bit_idx++] = (app->station_id >> i) & 1;
    for(int i = 3; i >= 0; i--)
        bits[bit_idx++] = (app->channel >> i) & 1;
    for(int i = 3; i >= 0; i--)
        bits[bit_idx++] = (app->mode >> i) & 1;
    for(int i = 7; i >= 0; i--)
        bits[bit_idx++] = (strength >> i) & 1;
    for(int i = 7; i >= 0; i--)
        bits[bit_idx++] = (checksum >> i) & 1;

    uint8_t end_bits = t->end_bits;
    if(end_bits > PACKET_END_BITS_MAX) end_bits = PACKET_END_BITS_MAX;
    size_t total_bits = PACKET_DATA_BITS + end_bits;

    // Encode message
    tx->buffer[idx++] = level_duration_make(true, t->sync_high_us);
    tx->buffer[idx++] = level_duration_make(false, t->sync_low_us);

    for(size_t i = 0; i < total_bits; i++) {
        bool one = (i < PACKET_DATA_BITS) && bits[i];
        if(one) {
            tx->buffer[idx++] = level_duration_make(true, t->one_high_us);
            tx->buffer[idx++] = level_duration_make(false, t->one_low_us);
        } else {
            tx->buffer[idx++] = level_duration_make(true, t->zero_high_us);
            tx->buffer[idx++] = level_duration_make(false, t->zero_low_us);
        }
    }

    // Reset the TX state
    tx->buffer_len = idx;
    tx->buffer_index = 0;
}

// Measure the pulse timings of a decoded packet whose sync pair starts at
// rx->work_buffer[sync_index] and store them in rx->captured_timing, so that
// transmissions can reproduce what this particular remote sends.
static void caixianlin_protocol_measure_timing(
    RxCapture* rx,
    size_t sync_index,
    const uint8_t* bits) {
    const int32_t* wb = rx->work_buffer;
    CaixianlinTiming* t = &rx->captured_timing;

    int32_t sync_high = wb[sync_index];
    int32_t sync_low = -wb[sync_index + 1];

    int32_t one_high = 0, one_low = 0, zero_high = 0, zero_low = 0;
    int32_t ones = 0, zeros = 0;
    for(size_t j = 0; j < PACKET_DATA_BITS; j++) {
        size_t p = sync_index + 2 + 2 * j;
        if(bits[j]) {
            one_high += wb[p];
            one_low -= wb[p + 1];
            ones++;
        } else {
            zero_high += wb[p];
            zero_low -= wb[p + 1];
            zeros++;
        }
    }
    if(ones) {
        one_high /= ones;
        one_low /= ones;
    } else {
        one_high = BIT_1_HIGH_US;
        one_low = BIT_1_LOW_US;
    }
    if(zeros) {
        zero_high /= zeros;
        zero_low /= zeros;
    } else {
        zero_high = BIT_0_HIGH_US;
        zero_low = BIT_0_LOW_US;
    }

    // The OOK demodulator delays rising and falling edges by different amounts,
    // so every received HIGH is shorter and every LOW longer than transmitted,
    // by the same shift. The protocol mirrors its bits (1 = long HIGH/short LOW,
    // 0 = short HIGH/long LOW), so half the difference between the long LOW of
    // a 0 and the long HIGH of a 1 is that shift. Estimate it from the packet
    // itself and undo it.
    int32_t bias = 0;
    if(ones && zeros) {
        bias = ((zero_low - one_high) + (one_low - zero_high)) / 4;
        if(bias > TIMING_BIAS_MAX_US) bias = TIMING_BIAS_MAX_US;
        if(bias < -TIMING_BIAS_MAX_US) bias = -TIMING_BIAS_MAX_US;
    }

    t->sync_high_us = caixianlin_protocol_clamp_us(sync_high + bias);
    t->sync_low_us = caixianlin_protocol_clamp_us(sync_low - bias);
    t->one_high_us = caixianlin_protocol_clamp_us(one_high + bias);
    t->one_low_us = caixianlin_protocol_clamp_us(one_low - bias);
    t->zero_high_us = caixianlin_protocol_clamp_us(zero_high + bias);
    t->zero_low_us = caixianlin_protocol_clamp_us(zero_low - bias);

    // Count the trailing 0 bits between the checksum and the next sync pulse.
    // Only possible when the next packet is already in the buffer; otherwise
    // the previous value is kept. A 0 bit must look like this packet's own 0
    // bits (both pulses at least half as long); much shorter pulses are idle
    // receiver glitches and are skipped, a real 1-like pulse gives up.
    size_t p = sync_index + PACKET_MIN_SAMPLES;
    uint8_t end_bits = 0;
    for(unsigned scanned = 0; scanned < TRAILING_SCAN_MAX_PAIRS && p + 1 < rx->work_buffer_len;
        scanned++, p += 2) {
        int32_t high = wb[p];
        int32_t low = wb[p + 1];
        if(caixianlin_protocol_is_sync(high, low)) {
            t->end_bits = end_bits;
            break;
        }
        if(high <= 0 || low >= 0) break; // polarity broken (dropped sample)
        bool zero_like = high <= -low && high * 2 >= zero_high && -low * 2 >= zero_low;
        if(zero_like) {
            if(end_bits >= PACKET_END_BITS_MAX) break;
            end_bits++;
        } else if(high > -low && high * 2 >= zero_high) {
            break; // a 1-like pulse: this is not a trailing gap
        }
    }

    FURI_LOG_I(
        TAG,
        "Timing: sync %u/%u one %u/%u zero %u/%u end %u (bias %ld)",
        (unsigned)t->sync_high_us,
        (unsigned)t->sync_low_us,
        (unsigned)t->one_high_us,
        (unsigned)t->one_low_us,
        (unsigned)t->zero_high_us,
        (unsigned)t->zero_low_us,
        (unsigned)t->end_bits,
        (long)bias);
}

// Decode message from work buffer
static bool caixianlin_protocol_decode_from_work_buffer(CaixianlinRemoteApp* app) {
    RxCapture* rx = &app->rx_capture;
    bool found = false;

    // A sync pulse is a HIGH/LOW pair, so nothing can be decoded from fewer than
    // two samples. This also guards the loop bounds below: work_buffer_len is a
    // size_t, so an expression like `work_buffer_len - 1` would wrap to SIZE_MAX
    // when the buffer is empty (e.g. right after RX starts and no edges arrived
    // before the receive timeout, or after a decoded packet consumed the whole
    // buffer) and the search would read far past the end of work_buffer.
    if(rx->work_buffer_len < 2) {
        return false;
    }

    // Search for sync pulse in work buffer
    size_t i;
    for(i = 0; i + 1 < rx->work_buffer_len; i++) {
        if(!caixianlin_protocol_is_sync(rx->work_buffer[i], rx->work_buffer[i + 1])) {
            // This isn't a sync pulse, keep looking
            continue;
        }

        // Found potential sync, check if we have enough samples for full message
        if(i + PACKET_MIN_SAMPLES > rx->work_buffer_len) {
            // Not enough samples yet, try again after we receive a new batch
            break;
        }

        // Decode bits
        uint8_t bits[PACKET_DATA_BITS];
        size_t bit_count = 0;
        size_t pos = i + 2;
        bool well_formed = true;

        while(bit_count < PACKET_DATA_BITS && pos + 1 < rx->work_buffer_len) {
            int32_t high = rx->work_buffer[pos];
            int32_t low = rx->work_buffer[pos + 1];

            // Every bit is a HIGH pulse followed by a LOW pulse. Anything else
            // (e.g. a dropped sample flipping the polarity) is not a packet.
            if(high <= 0 || low >= 0) {
                well_formed = false;
                break;
            }

            // Bit 1 if HIGH > |LOW|, Bit 0 if HIGH < |LOW|
            bits[bit_count++] = (high > -low) ? 1 : 0;
            pos += 2;
        }

        if(!well_formed || bit_count < PACKET_DATA_BITS) {
            // Not a packet at this sync position, try the next one
            continue;
        }

        // Parse packet
        uint16_t station_id = 0;
        for(int j = 0; j < 16; j++)
            station_id = (station_id << 1) | bits[j];

        uint8_t channel = 0;
        for(int j = 16; j < 20; j++)
            channel = (channel << 1) | bits[j];

        uint8_t mode = 0;
        for(int j = 20; j < 24; j++)
            mode = (mode << 1) | bits[j];

        uint8_t strength = 0;
        for(int j = 24; j < 32; j++)
            strength = (strength << 1) | bits[j];

        uint8_t checksum = 0;
        for(int j = 32; j < 40; j++)
            checksum = (checksum << 1) | bits[j];

        // Verify checksum
        uint8_t real_checksum = caixianlin_protocol_checksum(station_id, channel, mode, strength);

        if(checksum != real_checksum) {
            // Invalid message at this sync position, try next position
            continue;
        }
        if(mode < MODE_SHOCK || mode > MODE_MAX) {
            // Real remotes only send modes 1-4; anything else is noise that
            // happened to pass the 8-bit checksum (an all-zero run would even
            // "decode" as ID 0 with checksum 0)
            continue;
        }
        // Valid message decoded!
        FURI_LOG_I(TAG, "Decoded: ID=%d CH=%d M=%d S=%d", station_id, channel, mode, strength);

        // Only report findings if the message is different from the last one we decoded
        found = found || !rx->capture_valid || station_id != rx->captured_station_id ||
                channel != rx->captured_channel;

        // Store captured values
        rx->captured_station_id = station_id;
        rx->captured_channel = channel;
        rx->capture_valid = true;
        caixianlin_protocol_measure_timing(rx, i, bits);

        // Skip over the decoded message
        i = pos - 1;
    }

    // Update processed samples counter
    rx->processed += i;

    // Remove processed samples from work buffer
    if(i > 0 && i <= rx->work_buffer_len) {
        rx->work_buffer_len -= i;
        if(rx->work_buffer_len > 0) {
            memmove(
                &rx->work_buffer[0], &rx->work_buffer[i], rx->work_buffer_len * sizeof(int32_t));
        }
    }

    return found;
}

// Process RX buffer and attempt to decode one message
bool caixianlin_protocol_process_rx(CaixianlinRemoteApp* app) {
    RxCapture* rx = &app->rx_capture;

    // Transfer samples from stream buffer to work buffer
    size_t space_available = WORK_BUFFER_SIZE - rx->work_buffer_len;
    if(space_available > 0) {
        size_t bytes = furi_stream_buffer_receive(
            rx->stream_buffer,
            &rx->work_buffer[rx->work_buffer_len],
            space_available * sizeof(int32_t),
            100);
        rx->work_buffer_len += bytes / sizeof(int32_t);
    }

    // Try to decode
    return caixianlin_protocol_decode_from_work_buffer(app);
}

// Calculate checksum: 8-bit sum of the four payload bytes. Channel and mode
// share one byte (channel in the high nibble), as in the OpenShock reference
// encoder and in captures of real remotes.
uint8_t caixianlin_protocol_checksum(
    uint16_t station_id,
    uint8_t channel,
    uint8_t mode,
    uint8_t strength) {
    uint8_t station_high = (station_id >> 8) & 0xFF;
    uint8_t station_low = station_id & 0xFF;
    uint8_t channel_mode = (uint8_t)(((channel & 0x0F) << 4) | (mode & 0x0F));
    return (uint8_t)(station_high + station_low + channel_mode + strength);
}

uint16_t digit_multipliers[] = {10000, 1000, 100, 10, 1};

// Get digit from Station ID
uint8_t caixianlin_protocol_get_station_digit(uint16_t station_id, int pos) {
    if(pos < 0 || pos >= 5) return 0;
    return (station_id / digit_multipliers[pos]) % 10;
}

// Set digit in Station ID
uint16_t caixianlin_protocol_set_station_digit(uint16_t station_id, int pos, uint8_t digit) {
    uint8_t old_digit = caixianlin_protocol_get_station_digit(station_id, pos);
    int32_t new_id = (int32_t)station_id - (old_digit * digit_multipliers[pos]) +
                     (digit * digit_multipliers[pos]);
    if(new_id > 65535) new_id = 65535;
    if(new_id < 0) new_id = 0;
    return (uint16_t)new_id;
}
