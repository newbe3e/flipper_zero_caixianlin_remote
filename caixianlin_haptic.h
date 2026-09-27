#ifndef CAIXIANLIN_HAPTIC_H
#define CAIXIANLIN_HAPTIC_H

#include "caixianlin_types.h"

// Haptic feedback and shock cutoff: the Flipper vibrates while a shock is
// transmitted, at the strength set by app->vibro_level (software PWM on the
// motor pin; stealth mode mutes it). Collars (and remotes) cut a continuous
// shock after a few seconds even if the button stays pressed, so after
// app->shock_max_s the vibration stops and app->shock_timed_out is raised; the
// main loop then stops the transmission as well, like the real remote would,
// until OK is pressed again.

// Allocate the cutoff and PWM timers
void caixianlin_haptic_init(CaixianlinRemoteApp* app);

// Stop any vibration and free both timers
void caixianlin_haptic_deinit(CaixianlinRemoteApp* app);

// Called when a transmission has started
void caixianlin_haptic_on_tx_start(CaixianlinRemoteApp* app);

// Called when a transmission has stopped
void caixianlin_haptic_on_tx_stop(CaixianlinRemoteApp* app);

#endif // CAIXIANLIN_HAPTIC_H
