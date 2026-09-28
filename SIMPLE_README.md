# CaiXianlin Shock Collar Remote for Flipper Zero

A Flipper Zero application to control CaiXianlin shock collar.

**WARNING:** This application is intended for **educational and research purposes only**.

**NOTE:** I don't endorse use of these devices on any animals. You do you, but I'd never use this device on an animal.

## Features

- **Send** shock, vibrate, or beep commands
- **Adjustable strength** (0–99)
- **Channels** (0–2)
- **Clone/Listen mode** – Capture Station ID, channel and the exact pulse timings from an existing remote controller / hub
- **Haptic feedback with shock cutoff** – The Flipper vibrates while it transmits a shock, at the strength set by *Vibration* (off to 100 %), optionally following the shock strength on a logarithmic curve (*Vibro by strength*, needs *Vibration* above 25 %); when the collar's cutoff (*Shock max* setting, typically 10 s) is reached the vibration and the shock stop until OK is pressed again
- **Persistent settings** – Station ID, channel, learned timings, shock cutoff and vibration settings are saved

## First Launch

On the first launch, the setup screen will appear. You can:
1. Manually enter the Station ID (if you know it)
2. Set the channel (0-2)
3. Set **Shock max** to how long your collar keeps shocking with the button held
4. Set **Vibration** to how strongly the Flipper should buzz during a shock, and **Vibro by
   strength** if the buzz should follow the shock strength
5. Use **Listen for Remote** to clone an existing remote (press the remote button that controls
   your collar; the app also measures the remote's pulse timings and transmits with them)
6. Press **Done** to start using the app

If the collar ignores the Flipper, pair it instead: hold the collar's power button until its LED
flashes fast, then send **Beep** from the app.

Settings are automatically saved and will be restored on the next launch.
