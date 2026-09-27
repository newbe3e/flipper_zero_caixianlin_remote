# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a Flipper Zero external application that transmits and receives CaiXianlin shock collar control signals over 433.92 MHz Sub-GHz radio. The app allows users to send shock/vibrate/beep commands or clone settings from an existing remote controller.

## Build System

This project uses uFBT (Micro Flipper Build Tool):

```bash
# Build the application
ufbt

# Build and deploy to connected Flipper Zero
ufbt launch
```

The build system compiles against the Flipper Zero SDK and produces a `.fap` (Flipper Application Package) that can be loaded onto the device.

## Code Architecture

### Module Structure

The codebase is organized into distinct modules with clear separation of concerns:

- **caixianlin_remote.c**: Application entry point and main event loop
- **caixianlin_types.h/c**: Core data structures, constants, and shared state (`CaixianlinRemoteApp`)
- **caixianlin_protocol.h/c**: RF protocol encoding/decoding and packet construction
- **caixianlin_radio.h/c**: Low-level Sub-GHz radio hardware interface (TX/RX)
- **caixianlin_ui.h/c**: User interface rendering and input handling
- **caixianlin_storage.h/c**: Persistent settings (station ID, channel) using Flipper storage API

### Application Flow

1. **Initialization** (caixianlin_remote.c):
   - Allocate app state (`CaixianlinRemoteApp`)
   - Load persistent settings from storage
   - Initialize Flipper resources (GUI, notifications, message queue)
   - Initialize radio and UI modules
   - Show setup screen on first launch, otherwise main screen

2. **Event Loop**:
   - Process input events from message queue
   - Dispatch to UI handler based on current screen
   - Continuously process RX data when in listening mode
   - Update viewport after each event

3. **Screen States** (defined in `AppScreen` enum):
   - `ScreenSetup`: Configure station ID, channel, or capture from existing remote
   - `ScreenMain`: Send commands (shock/vibrate/beep) with adjustable strength
   - `ScreenListen`: Clone mode - capture station ID and channel from remote

### RF Protocol Implementation

The protocol uses ASK/OOK modulation at 433.92 MHz with Manchester-like encoding:

- **Timing** (defaults, from the OpenShock reference encoder; `CaixianlinTiming` in caixianlin_types.h):
  - Sync: 1400µs high, 750µs low
  - Bit 1: 750µs high, 250µs low
  - Bit 0: 250µs high, 750µs low
  - 3 trailing 0 bits after the checksum
  - Listen mode measures the captured remote's timings (with a correction for the
    demodulator's edge shift) and trailing-bit count into `rx_capture.captured_timing`;
    applying the capture copies them to `app->timing`, which the encoder uses and
    storage persists.

- **Packet Structure** (40 data bits + trailing zeros):
  ```
  [SYNC] [STATION_ID:16] [CHANNEL:4] [MODE:4] [STRENGTH:8] [CHECKSUM:8] [END:n]
  ```
  Checksum = 8-bit sum of the four payload bytes, where channel and mode share one
  byte (`(channel << 4) | mode`). Strength is 0-99 and is sent as 0 for beep.

- **TX Path**: `caixianlin_protocol_encode_message()` builds the signal buffer as `LevelDuration` pairs, then `caixianlin_radio_start_tx()` transmits via async TX callback

- **RX Path**: ISR callback writes raw timing data to stream buffer → `caixianlin_protocol_process_rx()` decodes packets by searching for sync pattern and validating checksum

### State Management

The `CaixianlinRemoteApp` struct (caixianlin_types.h:93-120) is the central state container, holding:
- Radio device handle
- Current transmission parameters (station_id, channel, mode, strength, timing)
- TX state (signal buffer)
- RX capture state (stream buffer, work buffer, decoded values)
- UI state (current screen, selection indices)
- Status flags (is_transmitting, is_listening, running)

All modules receive a pointer to this struct and operate on shared state.

### Settings Persistence

Station ID, channel and the TX timings are saved to `/ext/apps_data/caixianlin_remote/settings.txt` as one comma-separated line using the Flipper storage API (caixianlin_storage.c). Files from older versions that only hold ID and channel still load; the timings then stay at the defaults. Settings are loaded on startup and saved whenever changed in the setup screen.

## Key Implementation Details

- **Radio Hardware**: Uses Flipper's Sub-GHz device API with OOK270Async preset
- **Transmission**: Async transmission mode with state callback that feeds signal buffer one level at a time
- **Reception**: ISR callback captures raw edge timings into stream buffer, main loop processes buffer to decode packets
- **UI**: Single viewport with custom draw callback, renders different screens based on `app->screen` state
- **Input Handling**: Input events queued via message queue, processed in main loop and dispatched to screen-specific handlers

## Application Manifest

`application.fam` defines metadata for the Flipper build system:
- App ID: `caixianlin_remote`
- Category: Sub-GHz
- Entry point: `caixianlin_remote_app()`
- Requires: gui, storage, subghz
- External app type (FAP)
