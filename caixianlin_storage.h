#ifndef CAIXIANLIN_STORAGE_H
#define CAIXIANLIN_STORAGE_H

#include "caixianlin_types.h"

// Load Station ID, channel, TX timings, shock cutoff and vibration settings from persistent storage
// Returns true if at least Station ID and channel were loaded
bool caixianlin_storage_load(CaixianlinRemoteApp* app);

// Save Station ID, channel, TX timings, shock cutoff and vibration settings to persistent storage
void caixianlin_storage_save(CaixianlinRemoteApp* app);

#endif // CAIXIANLIN_STORAGE_H
