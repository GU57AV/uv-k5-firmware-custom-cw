# Text messaging + mesh relay (NR7Y CW firmware, UV-K5 V1)

Send short text messages between UV-K5 radios running this firmware. Radios pass each other's messages on, up to
3 relays, to reach stations out of direct range, with no repeater or other infrastructure. Like APRS, every
message carries the sender's callsign and the callsign of each relay, so you can see how it got to you. You can
type messages on the keypad or key them in CW.

NR7Y's CW features (keyer, macros, code practice, paddle support) all still work. Not everything in his build
survived: three other features were turned off to make room (see
[What was removed to make room](#what-was-removed-to-make-room)), and **F then MENU** now opens the messenger
instead of the menu. Messages are plain text, with **no encryption**.

> **Status: tested in a simulator, not yet on the air.** The protocol and screen logic pass an automated test
> suite against a simulated radio chip, but real RF behaviour (timing, levels, collisions) is still to be proven.
> Expect fixes after the first on-air tests.

## Quick start

1. Flash **the same build to every radio** on the net. Builds before the callsign version are not compatible.
2. Put each radio in **FM**, in frequency (VFO) mode on 2m or 70cm. Turn **BatSav** and **DualW** off on radios
   that should relay.
3. Open the messenger: **F**, then **MENU**. The radio jumps to the messenger frequency (see below).
4. Set your callsign: type it (in `A` mode), then **hold MENU**. Your call now shows on the bottom line.
5. Type a message and press **MENU** to send.

## How it works

1. **Sending.** Your radio builds a packet with your callsign, a random message ID, the text, a hop counter set to 3
   and a checksum. It waits for the channel to be quiet, plus a short random delay, then sends it as a data burst
   of about half a second.
2. **Relaying.** Every radio in range receives it and checks the checksum. A damaged packet is thrown away and
   never passed on. If the radio has **MsgRly** on, a callsign set, and hops left, it:
   * adds its own callsign to the packet's route,
   * lowers the hop counter by one,
   * waits a random moment, so two relays that heard the same packet don't transmit together,
   * waits for a quiet channel, then sends it on.
3. **Stopping.** Each radio remembers the IDs of the last 8 messages it has seen and ignores repeats, so a
   message is shown and relayed once per radio. When the hop counter reaches 0 it is shown but not relayed.
   This allows at most 3 relays, so a message reaches up to 4 radio-to-radio links from you.
4. **Delivery check.** When your radio hears a neighbour relay your message, the `>` in front of it turns into
   `+` and the route view shows who relayed it. Nothing extra is transmitted for this.
5. **Relay to be relayed.** A radio with MsgRly OFF sends its own messages with 0 hops, so nobody relays them.
   Only stations that relay for others get relayed.

```
 You (NR7Y)        K2ABC             N0CALL           VE3XYZ
 sends, hops 3 ──> relays, hops 2 ──> relays, hops 1 ──> relays, hops 0 ──> (shown, not relayed)
                   route: K2ABC      route: K2ABC,      route: K2ABC,
                                            N0CALL            N0CALL, VE3XYZ
```

Every station's callsign travels in the data it transmits: the sender's in the "from" field, each relay's in
the route.

## Keys

| Where | Key | Action |
|---|---|---|
| Main screen | **F**, then **MENU** | open messenger |
| Messenger | 0-9 | type (tap a key repeatedly to cycle letters; 1 = `. , ? ! - /`; 0 = space) |
| | \* | entry mode: `A` upper → `a` lower → `1` digits → `C` **CW** → `A` |
| | F / hold F | backspace / clear the line |
| | UP | recall the last message you sent |
| | hold UP | go back to the frequency you were on before the messenger opened, and make it this band's messenger frequency |
| | DOWN | route of the newest message; DOWN again for older ones; any other key goes back |
| | hold MENU | save what you've typed as **your callsign** (up to 6 characters) |
| | MENU | send |
| | PTT | voice as normal (you stay on the messenger screen); in `C` mode PTT and the side keys are your paddles |
| | EXIT | back to the main screen (also leaves `C` mode) |

## The screen

```
W1AW:GOOD MORNING          received direct ("NAME:")
VE3XYZ>ANYONE ON THE       received through relays ("NAME>")
 NET TONIGHT
+QRV 146.52                sent by you; '+' = heard relayed ('>' = not yet)
A:_            NR7Y        entry mode, what you're typing, your callsign
```

Messages wrap over two rows, and the three newest are kept. Until you set a callsign, the bottom line shows
`NO CALL SET`, and the radio won't send or relay. An envelope icon in the status bar means an unread message.

Route view (DOWN):

```
FROM VE3XYZ
VIA K2ABC
    N0CALL
ANYONE ON THE NET TONIGHT
```

`DIRECT` means it reached you with no relay. For your own messages, it means no relay has been heard yet.

## Messenger frequency

Opening the messenger in **frequency (VFO) mode** tunes to this band's messenger frequency, simplex (any repeater
offset is turned off), so everyone running the firmware ends up in the same place:

| Band | Factory default | Band plan |
|---|---|---|
| 2 m | **145.670 MHz** | ARRL: 145.50–145.80 "miscellaneous and experimental modes" |
| 70 cm | **446.670 MHz** | ARRL: 445–447 "simplex, links, repeaters (local option)" |

* **Outside the US, or for a different local frequency:** tune to the frequency you want on the main screen,
  open the messenger (it jumps to the default), then **hold UP**. The radio goes back to your frequency and makes
  it the messenger frequency for that band from then on. It's stored per band, in steps of 1.25 kHz, so 12.5 kHz
  channels like 145.5625 are stored exactly. Do it once for 2m and once for 70cm.
* **Memory channels are never changed.** If you open the messenger on a memory channel, it stays there, so a
  stored channel is a simple way to use any frequency.
* Outside 144–148 and 420–450 MHz, nothing changes.
* You can still tune anywhere afterwards. The jump only happens when the messenger opens.

The US defaults don't suit everywhere. Suggestions to store with hold UP (always check your national or local
band plan first):

| Region | 2 m | 70 cm | Why |
|---|---|---|---|
| EU / UK (IARU Region 1) | 145.550 | 433.900 | 145.575–145.7935 is repeater outputs, and 446 MHz is outside the band (PMR446). 433.8–434.25 is "digital communications and experiments" in the UK plan. |
| Japan (JARL) | ~144.5–144.7 | ~430.7–431.4 | The JARL plan is legally binding; these appear to be the wide-band data segments. Confirm against the official plan. |

## Keying messages in CW

Press \* until the mode shows `C`. The keyer now gives sidetone only, with nothing going on the air, and each
character you key is typed into the message. It uses your normal keyer settings: WPM, keyer mode and key input.
A word gap inserts a space. Press MENU to send, or \* to go back to `A`.

While in `C` mode, the speaker carries only your sidetone, so you won't hear voice traffic. Data messages are
still received, except while you're actually keying a character.

## Addressed messages

Everyone receives and relays every message. Starting a message with an address only changes who gets the alert
(beep + envelope icon):

* `@NR7Y HI` or `NR7Y DE W9SSN HI` alerts only NR7Y. Use the second form in CW, since Morse has no `@`.
* `CQ DE W9SSN`, `QST ...` and anything without an address alert everyone.

You don't need to add your own callsign; it's sent automatically. Other stations still see an addressed message
on their screen, since it's open amateur traffic.

## Menu items

* **MsgRx**: listen for messages (default ON).
* **MsgRly**: relay other stations' messages (default ON). It also needs a callsign set. When OFF, your own
  messages go out with 0 hops, so nobody relays them.
* **MsgMod**: FSK450 / FSK700 / AFSK12. **All radios on the net must use the same setting.** FSK450 is the most
  robust; AFSK12 (the default) is the fastest.

## Notes

* Messages are only sent and received in **FM**. A VFO set to CW or USB won't send data.
* Relay stations need **BatSav OFF**: the receiver sleeps while saving power and misses packets. They also need
  **DualW OFF**: relays transmit on the TX VFO, which may not be where the message was heard.
* Three features of NR7Y's build are turned off to make room; see
  [What was removed to make room](#what-was-removed-to-make-room).
* The callsign is stored in EEPROM at 0x0F48, and the messenger frequencies at 0x0EA4–0x0EA7. Both survive a
  factory reset.
* The mesh follows the behaviour documented for kamilsss655's NUNU protocol. The code was written fresh because
  NUNU itself is not open source, so it is **not** compatible with radios running the NUNU binary.

## What was removed to make room

The UV-K5 V1 has 60 KB of program space, and NR7Y's build already used almost all of it. To fit the messenger,
three features of his build, inherited from the egzumer firmware, were turned off. They're gone from this build:

| Feature | What you lose | Bytes freed | Build option |
|---|---|---|---|
| FM mic-level bar | The bar showing your mic level while transmitting FM voice. It isn't used in CW. | 344 | `ENABLE_AUDIO_BAR` |
| Scan ranges | Scanning only between the two VFO frequencies. In NR7Y's build the key that starts it (F + 5 in VFO mode) only works with VOX enabled, and VOX is off, so it couldn't be used anyway. | 128 | `ENABLE_SCAN_RANGES` |
| Frequency-calibration menu | The hidden menu for trimming the radio's frequency calibration, usually used once. Calibration already stored in the radio is kept and still applied. | 172 | `ENABLE_F_CAL_MENU` |

Nothing else was removed. One existing key changed meaning: **F then MENU** used to open the menu (the same as
MENU on its own) and now opens the messenger. NR7Y's CW features (keyer, macros, code practice, paddle support)
all still work. 61,380 of 61,440 bytes are now used, leaving 60.

**Getting them back:** to recalibrate, flash NR7Y's original firmware, use its calibration menu, then flash this
one again (calibration is stored in the radio, not the firmware). With the messenger built in there's no room for
any of them; even the smallest needs more than the 60 bytes left. To build without the messenger, use
`make ENABLE_MESSENGER=0 ENABLE_AUDIO_BAR=1 ENABLE_SCAN_RANGES=1 ENABLE_F_CAL_MENU=1`, which is NR7Y's original.
If you need room for something else, the next easiest feature to remove is copy-channel-to-VFO (long-press 1
in channel mode, 124 bytes).

## Technical details

* Packet, 60 bytes after a 4-byte sync word `30 72 57 6C`:
  header (type 5 bits + hops remaining 3 bits), 3-byte random ID, sender callsign (6), 3 route slots (6 each),
  text (30), CRC-16/CCITT (2). Callsigns are ASCII, zero-padded.
* Carrier sense: a random 40-103 ms backoff, repeated while squelch is open, for up to ~4 s. Relays add a further
  150-915 ms random delay.
* Maximum hops: `MSG_MAX_HOPS` in `app/messenger.h` (1-7). Changing it changes the packet size, so all radios
  must match.
* Build options: `ENABLE_MESSENGER` (on), `ENABLE_MESSENGER_FSK_MUTE` (on: don't play data bursts on the speaker).
* The original firmware, exactly: `make ENABLE_MESSENGER=0 ENABLE_AUDIO_BAR=1 ENABLE_SCAN_RANGES=1 ENABLE_F_CAL_MENU=1`.
