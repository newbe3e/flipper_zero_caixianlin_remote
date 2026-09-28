1.3:
- "Vibro by strength" setting: the vibration follows the shock strength on a logarithmic curve between the motor's 25 % floor and the "Vibration" setting (needs "Vibration" above 25 %)
- vibration strength is now pulsed in 1 % steps with a solid kick-start, so lower levels start reliably and scale smoothly
- license: this fork adds a condition forbidding use on animals or any being that cannot give informed consent

1.2:
- haptic feedback: the Flipper vibrates while it transmits a shock; at the collar's cutoff (new "Shock max" setting, default 10 s) the vibration and the shock stop until OK is pressed again
- "Vibration" setting: strength of that feedback (off, 25, 50, 75, 100 %)
- Setup menu scrolls; channel limited to 0-2 as documented

1.1:
- fix crash when opening Listen for Remote
- Listen mode measures the remote's pulse timings and trailing bits and transmits with them
- checksum, beep intensity and strength range (0-99) now match the reference protocol
- minimum transmit burst, "TX failed" indication, hold OK on Listen for Remote to reset timings

1.0:
initial release
