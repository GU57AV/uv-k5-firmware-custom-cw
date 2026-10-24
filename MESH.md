# Text messaging + mesh relay (NR7Y CW firmware, UV-K5 V1)

Adds a T9 text messenger and simple mesh relaying to the NR7Y CW firmware. All CW features are kept.
The build uses 61,304 of the 61,440 bytes of flash. To make room, the FM mic-level bar (`ENABLE_AUDIO_BAR`) and scan ranges (`ENABLE_SCAN_RANGES`, which had no key to start it in this build) are off.

Like APRS, every message carries the sender's callsign, and each relay adds its own callsign as it passes the
message on. You can see exactly how a message reached you, and every station identifies itself in the data it sends.

The mesh follows the behaviour documented for kamilsss655's NUNU protocol: flooding with a hop limit, carrier
sense before transmitting, a CRC check, and "relay to be relayed". The code was written fresh because NUNU itself
is not open source. It is **not** guaranteed to interoperate with radios running the NUNU binary.

There is **no encryption**: messages are plain text.

## Use

| Where | Key | Action |
|---|---|---|
| Main screen | **F**, then **MENU** | open messenger |
| Messenger | 0-9 | T9 entry (tap a key repeatedly to cycle letters; 0 = space) |
| | \* | switch entry mode: `A` upper, `a` lower, `1` digits, `C` **CW** (key the message with your paddle or key) |
| | F / hold F | backspace / clear the line |
| | UP | recall the last message you sent |
| | DOWN | show the route of the newest message; DOWN again for older ones, any other key to go back |
| | hold MENU | save what you've typed as **your callsign** (type it in `A` mode; up to 6 characters) |
| | MENU | send |
| | PTT | voice as normal (you stay on the messenger screen); in `C` mode PTT/side keys are your paddles |
| | EXIT | back to the main screen |

**Set your callsign first.** Until you do, the input line shows `NO CALL SET`, the radio won't send, and it won't
relay. The callsign is kept in EEPROM (0x0F48) and survives a reset.

On screen, `NR7Y:` marks a message received direct and `NR7Y>` one that came through relays. `>` marks a message
you sent, and it turns into `+` when you hear a station relay it. The route view then shows who relayed it.
An envelope icon in the status bar means you have an unread message.

```
FROM VE3XYZ          route view (DOWN)
VIA K2ABC
    N0CALL
ABCDEFGHIJKLMNOPQR
STUVWXYZ012
```

## Keying messages in CW

Press \* until the mode shows `C`. The keyer now gives sidetone only (nothing goes on the air), and every character
you key is typed into the message. It uses your normal keyer settings: WPM, mode and key input. A word gap
inserts a space. Press MENU to send, or \* to go back to `A`. Leaving the messenger also leaves CW mode.

## Addressed messages

Everyone receives and relays every message. Starting a message with an address only changes who gets the alert
(beep + envelope icon):

* `@NR7Y HI` or `NR7Y DE W9SSN HI` alerts only NR7Y. The second form can be keyed in CW, since Morse has no `@`.
* `CQ DE W9SSN`, `QST ...` and anything without an address alert everyone.

Other stations still see an addressed message on their messenger screen; it's open amateur traffic.

## Menu items

* **MsgRx**: listen for messages (default ON)
* **MsgRly**: relay other stations' messages (default ON). If this is OFF, your own messages go out with 0 hops,
  so nobody relays them.
* **MsgMod**: FSK450 / FSK700 / AFSK12. All radios on the net must use the same setting. FSK450 is the most robust;
  AFSK12 (the default) is the fastest.

## Notes

* Messages are only sent and received in **FM** mode. A VFO set to CW or USB refuses to send data.
* For relay stations, set **BatSav OFF** (the receiver sleeps while saving power) and **DualW OFF**
  (relays transmit on the TX VFO).
* Messages hop at most 3 times (`MSG_MAX_HOPS` in `app/messenger.h`, maximum 7). Each radio shows and relays a
  given message only once, and drops packets that carry no sender callsign.
* Packet: 4-byte sync `30 72 57 6C`, header (type 5 bits + hops 3 bits), a 3-byte random id, the sender's callsign
  (6 bytes), 3 path slots (6 bytes each), 30 bytes of text and a CRC-16/CCITT. That's 60 bytes in total.
* Build without it: `make ENABLE_MESSENGER=0`. The result is byte-for-byte the same size as the original firmware.
