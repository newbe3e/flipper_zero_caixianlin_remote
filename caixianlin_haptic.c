#include "caixianlin_haptic.h"

// The vibration motor is a plain on/off pin, so strength is done by pulsing
// it: every millisecond a sigma-delta accumulator decides whether the motor is
// powered, so a duty of d % powers it d ms out of every 100 with the pulses
// spread as evenly as possible (short off-gaps, which the eccentric-mass motor
// coasts through at reduced speed, i.e. a weaker buzz). 100 % keeps the motor
// on solid (no timer). The motor needs full voltage to start turning at all,
// so every buzz begins with a short solid kick before the pulsing starts.
#define VIBRO_PWM_TICK_MS 1
#define VIBRO_KICK_TICKS  40 // 40 ms solid at the start of a buzz
#define VIBRO_DUTY_FLOOR  25 // below this the motor would stall

// Perceived shock grows roughly with the logarithm of the strength level, so
// when the vibration follows the strength it uses this curve:
// 100 * ln(1 + strength) / ln(100), in percent, for strength 0..99.
static const uint8_t vibro_log_curve[MAX_STRENGTH + 1] = {
      0,  15,  24,  30,  35,  39,  42,  45,  48,  50,
     52,  54,  56,  57,  59,  60,  62,  63,  64,  65,
     66,  67,  68,  69,  70,  71,  72,  72,  73,  74,
     75,  75,  76,  77,  77,  78,  78,  79,  80,  80,
     81,  81,  82,  82,  83,  83,  84,  84,  85,  85,
     85,  86,  86,  87,  87,  87,  88,  88,  89,  89,
     89,  90,  90,  90,  91,  91,  91,  92,  92,  92,
     93,  93,  93,  93,  94,  94,  94,  95,  95,  95,
     95,  96,  96,  96,  96,  97,  97,  97,  97,  98,
     98,  98,  98,  99,  99,  99,  99, 100, 100, 100,
};

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
    app->vibro_acc += app->vibro_duty;
    if(app->vibro_acc >= 100) {
        app->vibro_acc -= 100;
        furi_hal_vibro_on(true);
    } else {
        furi_hal_vibro_on(false);
    }
}

// Duty (percent of time powered) for the shock about to be transmitted
static uint8_t caixianlin_haptic_duty(const CaixianlinRemoteApp* app) {
    uint8_t max_duty = (uint8_t)(app->vibro_level * 100 / VIBRO_LEVEL_MAX);
    if(!app->vibro_scale || max_duty == 0) return max_duty; // Vibration off stays off

    uint8_t strength = app->strength > MAX_STRENGTH ? MAX_STRENGTH : app->strength;
    if(strength == 0) return 0; // a strength-0 shock does nothing on the collar
    // The curve spans the usable band between the stall floor and the
    // Vibration setting; at Vibration 25 % that band is empty, so the buzz
    // cannot vary and stays at the floor.
    if(max_duty <= VIBRO_DUTY_FLOOR) return max_duty;
    unsigned span = max_duty - VIBRO_DUTY_FLOOR;
    return (uint8_t)(VIBRO_DUTY_FLOOR + span * vibro_log_curve[strength] / 100);
}

// Motor on at the given duty; returns false if it stays off
static bool caixianlin_haptic_motor_start(CaixianlinRemoteApp* app, uint8_t duty) {
    if(duty == 0) return false;
    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode)) return false;

    app->vibro_duty = duty;
    app->vibro_acc = 0;
    app->vibro_kick_ticks = VIBRO_KICK_TICKS;
    furi_hal_vibro_on(true);
    if(duty < 100) {
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

    app->haptic_active = caixianlin_haptic_motor_start(app, caixianlin_haptic_duty(app));

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
