# CaiXianlin Shock Collar Remote for Flipper Zero

A Flipper Zero application to control CaiXianlin shock collar.

![Screenshot](screenshots/0-home-vibrate.png)

**WARNING:** This application is intended for **educational and research purposes**, and for use
only with people who can and do give informed consent and can withdraw it at any time.

**Do not use this software, or anything built with it, on animals** or on any being that cannot
consent. This is a condition of the license (see [LICENSE](LICENSE)), not just a request.

## Features

- **Send** shock, vibrate, or beep commands
- **Adjustable strength** (0–99)
- **Channels** (0–2)
- **Clone/Listen mode** – Capture Station ID, channel and the exact pulse timings from an existing remote controller / hub, so transmissions match what that remote sends
- **Haptic feedback with shock cutoff** – The Flipper vibrates while it transmits a shock. Collars cut a continuous shock after a few seconds (typically 10) even if the button stays pressed, so set *Shock max* to your collar's cutoff: when it elapses the vibration stops and the Flipper also stops transmitting, exactly like the remote, until you press OK again (the screen shows *Shock timed out*). The *Vibration* setting picks the strength (off, 25, 50, 75 or 100 %: the share of time the motor is powered, so the felt strength is not exactly linear); it works independently of the Flipper's system *Vibro* setting but is muted in stealth mode
- **Vibro by strength** – Optional: the vibration follows the shock strength, so you feel how hard the collar is being hit. Perceived shock grows roughly with the logarithm of the level, so the curve is logarithmic (the motor duty runs from the 25 % floor that keeps it turning up to the *Vibration* setting as `ln(1+strength)/ln(100)`, so *Vibration* must be above 25 % for the buzz to vary; strength 0 gives no vibration)
- **Persistent settings** – Station ID, channel, learned timings, shock cutoff and vibration settings are saved

## Controls

**Main Screen:**

| Button           | Action                           |
|------------------|----------------------------------|
| **OK (hold)**    | Transmit signal                  |
| **←** / **→**    | Change mode (Shock/Vibrate/Beep) |
| **↑** / **↓**    | Adjust strength (0–99)           |
| **Back (short)** | Exit app                         |
| **Back (hold)**  | Open setup screen                |

**Setup Screen:**

| Button        | Action                         |
|---------------|--------------------------------|
| **↑** / **↓** | Navigate menu                                              |
| **←** / **→** | Change channel, *Shock max*, *Vibration* or *Vibro by strength* |
| **OK**        | Select option                                              |
| **OK (hold)** | On *Listen for Remote*: forget learned timings (defaults)  |
| **Back**      | Exit app (while editing the Station ID: cancel the edit)   |

**Cloning tips:**

- While listening, press the button on the remote that actually controls your collar. On some
  remotes every channel button is a different Station ID.
- The Listen screen shows the measured pulse timings (`Sync`, `Bit`, `End`). Press **OK** to apply
  them together with the Station ID and channel; they are used for every transmission from then on.
- If the collar still ignores the Flipper, pair the collar to it instead: hold the collar's power
  button until its LED flashes fast, then send **Beep** from the app. The collar beeps once paired.
- `[ TX failed! ]` on the main screen means the radio refused to transmit (missing radio or a region
  lock that forbids 433.92 MHz), not that the collar ignored the packet.

## Protocol Specification

**RF Parameters:**

| Parameter  | Value                          |
|------------|--------------------------------|
| Frequency  | 433.92 MHz                     |
| Modulation | ASK/OOK                        |
| Preset     | FuriHalSubGhzPresetOok270Async |

**Bit Encoding (defaults):**

| Element | HIGH Duration | LOW Duration |
|---------|---------------|--------------|
| Sync    | ~1400 µs      | ~750 µs      |
| Bit 1   | ~750 µs       | ~250 µs      |
| Bit 0   | ~250 µs       | ~750 µs      |

The defaults follow the OpenShock reference encoder. Remotes differ slightly (sync pulses of
1250–1500 µs and 2 or 3 trailing bits have been seen), and some collars are picky about it, so
**Listen for Remote** measures the pulse timings and the trailing-bit count of the captured remote
and uses those for transmission once you apply the capture. The measured values are shown on the
Listen screen.

Based on [https://wiki.openshock.org/hardware/shockers/caixianlin](https://wiki.openshock.org/hardware/shockers/caixianlin)
and captured signals from real remotes and controller hubs.

**Packet Structure (40 data bits + trailing zeros):**

```
[SYNC] [STATION_ID:16] [CHANNEL:4] [MODE:4] [STRENGTH:8] [CHECKSUM:8] [END:3]
```

| Field      | Bits | Range   | Description                                    |
|------------|------|---------|------------------------------------------------|
| Station ID | 16   | 0-65535 | Transmitter identifier (collar paired to this) |
| Channel    | 4    | 0-2     | Channel number                                 |
| Mode       | 4    | 1-3     | 1=Shock, 2=Vibrate, 3=Beep                     |
| Strength   | 8    | 0-99    | Intensity (sent as 0 for Beep)                 |
| Checksum   | 8    | 0-255   | 8-bit sum of the four preceding bytes          |
| End        | 3    | 000     | Trailing 0 bits (count learned from remote)    |

**Checksum Calculation:**

```c
checksum = (STATION_ID_high_byte + STATION_ID_low_byte + ((Channel << 4) | Mode) + Strength) & 0xFF
```

Channel and mode share one byte, with the channel in the high nibble.

## First Launch

On the first launch, the setup screen will appear. You can:
1. Manually enter the Station ID (if you know it)
2. Set the channel (0-2)
3. Set **Shock max** to how long your collar keeps shocking with the remote's button held (hold
   it once and count; the app stops its own shock at the same point, *off* disables the cutoff)
4. Set **Vibration** to how strongly the Flipper should buzz during a shock (or *off*)
5. Turn **Vibro by strength** on if the buzz should follow the shock strength
6. Use **Listen for Remote** to clone an existing remote
7. Press **Done** to start using the app

Settings are automatically saved and will be restored on the next launch.

## License

MIT License for the original code, plus an additional condition for this fork: the software must not be used on animals or on any being that cannot give informed consent. See [LICENSE](LICENSE) for the exact terms.

## Acknowledgments

- Protocol analysis based on [https://wiki.openshock.org/hardware/shockers/caixianlin](https://wiki.openshock.org/hardware/shockers/caixianlin) and reverse engineering of 
  captured signals from the controller hub
- Flipper Zero firmware team for the excellent SDK
