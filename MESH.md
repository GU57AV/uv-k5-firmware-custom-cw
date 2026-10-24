# Text messaging + mesh relay (NR7Y CW firmware, UV-K5 V1)

Adds a T9 text messenger and simple mesh relaying to the NR7Y CW firmware. All CW features are kept.
The build uses 60,700 of the 61,440 bytes of flash.

The mesh follows the behaviour documented for kamilsss655's NUNU protocol: flooding with a hop limit, carrier
sense before transmitting, a CRC check, and "relay to be relayed". The code was written fresh because NUNU itself
is not open source. It is **not** guaranteed to interoperate with radios running the NUNU binary.

There is **no encryption**: messages are plain text.

## Use

| Where | Key | Action |
|---|---|---|
| Main screen | **F**, then **MENU** | open messenger |
| Messenger | 0-9 | T9 entry (tap a key repeatedly to cycle letters; 0 = space) |
| | \* | switch case/mode: `A` upper, `a` lower, `1` digits |
| | F / hold F | backspace / clear the line |
| | UP | recall the last message you sent |
| | MENU | send |
| | PTT | voice as normal (you stay on the messenger screen) |
| | EXIT | back to the main screen |

On screen, `<` marks a received message and `>` a message you sent. The `>` turns into `+` when you hear another
station relay your message. An envelope icon in the status bar means you have an unread message.

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
  given message only once.
* Packet: 4-byte sync `30 72 57 6C`, then header (type 5 bits + hops 3 bits), a 3-byte random id,
  30 bytes of text and a CRC-16/CCITT, 36 bytes in total.
* Build without it: `make ENABLE_MESSENGER=0`. The result is byte-for-byte the same size as the original firmware.
