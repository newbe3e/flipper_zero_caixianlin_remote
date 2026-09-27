#ifndef CAIXIANLIN_TYPES_H
#define CAIXIANLIN_TYPES_H

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <notification/notification_messages.h>
#include <lib/subghz/devices/devices.h>

#define TAG "CaixianlinRemote"

// Settings file path
#define SETTINGS_PATH APP_DATA_PATH("settings.txt")

// Default protocol timings (microseconds). These follow the OpenShock reference
// encoder and logic-analyzer captures of real remotes. Listen mode measures the
// timings of the captured remote and those are used for transmission instead.
#define TE_US         250
#define SYNC_HIGH_US  1400
#define SYNC_LOW_US   (3 * TE_US) // 750
#define BIT_1_HIGH_US (3 * TE_US) // 750
#define BIT_1_LOW_US  (1 * TE_US) // 250
#define BIT_0_HIGH_US (1 * TE_US) // 250
#define BIT_0_LOW_US  (3 * TE_US) // 750

// Sanity bounds for learned / stored timings
#define TIMING_MIN_US      50
#define TIMING_MAX_US      5000
#define TIMING_BIAS_MAX_US 150 // largest demodulator edge shift we try to undo

// Packet layout:
// [SYNC] [STATION_ID:16] [CHANNEL:4] [MODE:4] [STRENGTH:8] [CHECKSUM:8] [END:n]
#define PACKET_DATA_BITS    40
#define PACKET_END_BITS     3 // trailing 0 bits sent by the reference encoder
#define PACKET_END_BITS_MAX 8
#define PACKET_MIN_SAMPLES  (2 + PACKET_DATA_BITS * 2) // sync pair + one HIGH/LOW pair per data bit
#define SIGNAL_BUFFER_SIZE  128 // >= 2 + 2 * (PACKET_DATA_BITS + PACKET_END_BITS_MAX)
#define WORK_BUFFER_SIZE    512

#define MAX_STRENGTH 99 // the protocol intensity range is 0-99

// Collars cut a continuous shock after a few seconds even if the remote keeps
// transmitting. The Flipper's vibration mirrors that; set it to what your
// collar does (0 = never cuts off).
#define SHOCK_MAX_S_DEFAULT 10
#define SHOCK_MAX_S_LIMIT   60

// Vibration strength: 0 = off, 1..4 = 25/50/75/100 % (on-ticks per PWM cycle)
#define VIBRO_LEVEL_MAX     4
#define VIBRO_LEVEL_DEFAULT VIBRO_LEVEL_MAX

// For RX capture. A sync pulse is recognised by its long HIGH; the LOW window is
// wide because remotes use 500-800us here and the demodulator shifts edges.
#define RX_BUFFER_SIZE   2048
#define SYNC_HIGH_MIN_US 1000
#define SYNC_HIGH_MAX_US 1800
#define SYNC_LOW_MIN_US  350
#define SYNC_LOW_MAX_US  1200

// Modes
#define MODE_SHOCK   1
#define MODE_VIBRATE 2
#define MODE_BEEP    3
#define MODE_LIGHT   4 // sent by some remotes; not selectable in this app
#define MODE_MAX     MODE_LIGHT

// Keep transmitting at least this long so a quick tap still puts a few complete
// packets on air (one packet is ~45 ms; the reference remotes repeat it ~5 times)
#define TX_MIN_BURST_MS 250

// App screens
typedef enum {
    ScreenSetup,
    ScreenMain,
    ScreenListen,
} AppScreen;

// Setup menu items (in display order)
typedef enum {
    SetupItemStationId,
    SetupItemChannel,
    SetupItemShockMax,
    SetupItemVibration,
    SetupItemListen,
    SetupItemDone,
    SetupItemCount,
} SetupItem;

#define SETUP_VISIBLE_ITEMS 4 // menu rows that fit under the title
_Static_assert(SetupItemCount > SETUP_VISIBLE_ITEMS, "Setup list scrolling assumes more items than rows");

// Pulse timings used to build a packet
typedef struct {
    uint16_t sync_high_us;
    uint16_t sync_low_us;
    uint16_t one_high_us;
    uint16_t one_low_us;
    uint16_t zero_high_us;
    uint16_t zero_low_us;
    uint8_t end_bits; // trailing 0 bits after the checksum
} CaixianlinTiming;

// Transmission state
typedef struct {
    LevelDuration buffer[SIGNAL_BUFFER_SIZE];
    size_t buffer_len;
    size_t buffer_index;
} TxState;

// RX capture state
typedef struct {
    FuriStreamBuffer* stream_buffer; // Ring buffer for ISR to write to
    int32_t work_buffer[WORK_BUFFER_SIZE]; // Working buffer for decode processing
    size_t work_buffer_len; // Number of samples in work buffer
    size_t processed; // Number of samples processed
    uint16_t captured_station_id;
    uint8_t captured_channel;
    CaixianlinTiming captured_timing; // Timings measured from the last decoded packet
    bool capture_valid;
} RxCapture;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    NotificationApp* notifications;
    FuriMessageQueue* event_queue;

    const SubGhzDevice* radio_device;

    uint16_t station_id;
    uint8_t channel;
    uint8_t mode;
    uint8_t strength;
    uint8_t shock_max_s; // Collar's continuous-shock cutoff in seconds (0 = never)
    uint8_t vibro_level; // Vibration strength 0..VIBRO_LEVEL_MAX
    CaixianlinTiming timing; // Timings used for transmission

    TxState tx_state;
    RxCapture rx_capture;

    bool is_transmitting;
    bool tx_failed; // last TX start was refused (no radio / region lock)
    uint32_t tx_start_tick;
    FuriTimer* haptic_timer; // Ends the vibration when the collar cuts the shock
    FuriTimer* vibro_pwm_timer; // Pulses the motor for strengths below 100 %
    bool vibro_pwm_active; // PWM ticks may drive the motor
    uint8_t vibro_pwm_phase; // Position in the PWM cycle
    uint8_t vibro_kick_ticks; // Remaining ticks of the solid kick at the start of a buzz
    bool haptic_active; // Flipper is vibrating
    bool shock_timed_out; // Collar cut the shock at shock_max_s; stays set after TX stopped until OK is released or Back is pressed
    bool is_listening;
    bool running;

    AppScreen screen;
    int setup_selected; // SetupItem
    int setup_first_visible; // First Setup item drawn (the list scrolls)
    bool setup_dirty; // A held Left/Right changed a value that is not saved yet
    int station_id_digit; // Which digit being edited (0-4)
    bool editing_station_id;
    uint16_t station_id_backup; // Restored when a Station ID edit is cancelled

    uint32_t frame_counter; // For UI animations
} CaixianlinRemoteApp;

extern const char* mode_names[4];

void app_init(CaixianlinRemoteApp* app);

// Reset timings to the protocol defaults
void caixianlin_timing_set_default(CaixianlinTiming* timing);

// Check that every timing is inside the sanity bounds (safe to transmit)
bool caixianlin_timing_is_valid(const CaixianlinTiming* timing);

#endif // CAIXIANLIN_TYPES_H
