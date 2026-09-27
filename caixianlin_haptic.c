#include "caixianlin_haptic.h"

// The vibration motor is a plain on/off pin, so strength is done by pulsing it:
// four 2 ms ticks form an 8 ms cycle and the motor is on for `vibro_level` of
// them. The eccentric-mass motor cannot follow such short pulses, so it runs
// at a fraction of its speed and buzzes weaker. Level 4 keeps the motor on
// solid (no timer). The motor needs full voltage to start turning at all, so
// every buzz begins with a short solid kick before the pulsing starts.
#define VIBRO_PWM_TICK_MS 2
#define VIBRO_KICK_TICKS  20 // 40 ms solid at the start of a buzz

// Timer service thread: one PWM tick
static void caixianlin_haptic_pwm_tick(void* context) {
    CaixianlinRemoteApp* app = context;
    if(!app->vibro_pwm_active) {
        // A tick that landed after the stop: make sure the motor ends up off
        furi_hal_vibro_on(false);
        return;
    }
    if(app->vibro_kick_ticks) {
        app->vibro_kick_ticks--; // motor stays on solid
        return;
    }
    app->vibro_pwm_phase = (uint8_t)((app->vibro_pwm_phase + 1) % VIBRO_LEVEL_MAX);
    furi_hal_vibro_on(app->vibro_pwm_phase < app->vibro_level);
}

// Motor on at the configured strength; returns false if it stays off
static bool caixianlin_haptic_motor_start(CaixianlinRemoteApp* app) {
    if(app->vibro_level == 0) return false;
    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode)) return false;

    app->vibro_pwm_phase = 0;
    app->vibro_kick_ticks = VIBRO_KICK_TICKS;
    furi_hal_vibro_on(true);
    if(app->vibro_level < VIBRO_LEVEL_MAX) {
        app->vibro_pwm_active = true;
        furi_timer_start(app->vibro_pwm_timer, furi_ms_to_ticks(VIBRO_PWM_TICK_MS));
    }
    return true;
}

// Motor off. `wait` also waits for a PWM tick that may already be running;
// that is not allowed from a timer callback (it would block the timer service
// on itself), which instead relies on the tick seeing vibro_pwm_active == false.
static void caixianlin_haptic_motor_stop(CaixianlinRemoteApp* app, bool wait) {
    app->vibro_pwm_active = false;
    furi_timer_stop(app->vibro_pwm_timer);
    if(wait) furi_timer_flush();
    furi_hal_vibro_on(false);
}

// Timer service thread: the collar's cutoff time has passed
static void caixianlin_haptic_timeout(void* context) {
    CaixianlinRemoteApp* app = context;

    // furi_timer_stop is asynchronous, so a callback armed by a previous
    // transmission can still land here after a new one started. Only act if
    // the transmission that is running now has really reached the cutoff.
    // The app thread outranks the timer service thread and may stop the
    // transmission between the check and the flag update, so do both atomically.
    uint32_t cutoff = furi_ms_to_ticks((uint32_t)app->shock_max_s * 1000);
    uint32_t slack = furi_ms_to_ticks(50);
    bool timed_out;
    FURI_CRITICAL_ENTER();
    uint32_t elapsed = furi_get_tick() - app->tx_start_tick;
    timed_out = app->is_transmitting && elapsed + slack >= cutoff;
    if(timed_out) {
        // The collar has stopped shocking (or would have on the real remote):
        // stop the vibration now and let the main loop stop the transmission.
        app->haptic_active = false;
        app->shock_timed_out = true;
    }
    FURI_CRITICAL_EXIT();
    if(!timed_out) return;

    caixianlin_haptic_motor_stop(app, false);
    view_port_update(app->view_port);
}

void caixianlin_haptic_init(CaixianlinRemoteApp* app) {
    app->haptic_timer = furi_timer_alloc(caixianlin_haptic_timeout, FuriTimerTypeOnce, app);
    app->vibro_pwm_timer =
        furi_timer_alloc(caixianlin_haptic_pwm_tick, FuriTimerTypePeriodic, app);
}

void caixianlin_haptic_deinit(CaixianlinRemoteApp* app) {
    caixianlin_haptic_on_tx_stop(app);
    caixianlin_haptic_motor_stop(app, true);
    furi_timer_free(app->vibro_pwm_timer);
    furi_timer_free(app->haptic_timer);
    app->vibro_pwm_timer = NULL;
    app->haptic_timer = NULL;
}

void caixianlin_haptic_on_tx_start(CaixianlinRemoteApp* app) {
    app->shock_timed_out = false;
    if(app->mode != MODE_SHOCK) return;

    app->haptic_active = caixianlin_haptic_motor_start(app);

    if(app->shock_max_s > 0) {
        furi_timer_start(app->haptic_timer, furi_ms_to_ticks((uint32_t)app->shock_max_s * 1000));
    }
}

void caixianlin_haptic_on_tx_stop(CaixianlinRemoteApp* app) {
    if(app->haptic_timer) furi_timer_stop(app->haptic_timer);
    if(app->haptic_active) {
        app->haptic_active = false;
        caixianlin_haptic_motor_stop(app, true);
    }
    // shock_timed_out stays set so the screen keeps saying why the shock
    // ended until OK is released
}
