/* Messenger + mesh relaying for the NR7Y CW firmware (UV-K5 V1)
 *
 * Messenger UI, T9 entry and BK4819 FSK modem setup derived from
 *   Copyright 2023 joaquimorg  https://github.com/joaquimorg
 *   Copyright 2024 kamilsss655 https://github.com/kamilsss655
 * Mesh relaying is a clean-room implementation of the publicly documented
 * NUNU protocol behaviour (flooding with hop limit, CSMA/CA, CRC check,
 * "relay to be relayed"). No encryption: plain text only, as required on
 * amateur frequencies.
 *
 * Licensed under the Apache License, Version 2.0
 */

#ifndef APP_MESSENGER_H
#define APP_MESSENGER_H

#ifdef ENABLE_MESSENGER

#include <stdbool.h>
#include <stdint.h>
#include "driver/keyboard.h"

enum {
	MSG_PAYLOAD_LEN = 30,   // max 29 characters + terminator
	MSG_ID_LEN      = 3,    // random message id, used to drop duplicates
	MSG_CALL_LEN    = 6,    // callsign, NUL padded (not terminated when 6 long)
	MSG_MAX_HOPS    = 3,    // relays a message may pass through (1..7)
	MSG_HISTORY     = 3     // messages kept on screen
};

typedef enum {
	MSG_TYPE_TEXT = 1       // only packet type; anything else is ignored
} MsgType;

// header byte: bits 0-4 type, bits 5-7 hops remaining
#define MSG_HDR(type, hops)   ((uint8_t)(((hops) << 5) | ((type) & 0x1F)))
#define MSG_HDR_TYPE(h)       ((h) & 0x1F)
#define MSG_HDR_HOPS(h)       ((h) >> 5)

// APRS-style: the originator's callsign plus the callsign of every relay,
// each relay filling the next empty path slot as it passes the message on.
typedef union {
	struct {
		uint8_t  header;
		uint8_t  id[MSG_ID_LEN];
		char     src[MSG_CALL_LEN];
		char     path[MSG_MAX_HOPS][MSG_CALL_LEN];
		char     payload[MSG_PAYLOAD_LEN];
		uint16_t crc;                     // CRC16-CCITT over everything before it
	} __attribute__((packed)) d;
	uint8_t raw[1 + MSG_ID_LEN + MSG_CALL_LEN * (1 + MSG_MAX_HOPS) + MSG_PAYLOAD_LEN + 2];
} MeshPacket;

// one message as kept for the screen
typedef struct {
	char kind;                                // '<' received, '>' sent, '+' sent and heard relayed
	char src[MSG_CALL_LEN];
	char path[MSG_MAX_HOPS][MSG_CALL_LEN];    // route it took (for '+': who relayed ours)
	char text[MSG_PAYLOAD_LEN];
} MsgEntry;

typedef enum {
	MOD_FSK_450,    // most robust, slowest
	MOD_FSK_700,
	MOD_AFSK_1200   // fastest, needs a clean signal
} ModemModulation;

typedef union {
	struct {
		uint8_t
			receive    :1,  // listen for messages
			relay      :1,  // relay messages for other stations
			unused     :2,
			modulation :2,
			unused2    :2;
	} data;
	uint8_t __val;
} MessengerConfig;

extern uint8_t hasNewMessage;
extern uint8_t gMsgKeyTick;
extern char    gMsgInput[MSG_PAYLOAD_LEN];
extern MsgEntry gMsgLog[MSG_HISTORY];      // [MSG_HISTORY-1] is the newest
extern char    gMsgMyCall[MSG_CALL_LEN];
extern uint8_t gMsgKbd;
extern uint8_t gMsgView;                   // 0 = list, n = route of the n-th newest message

void MSG_Init(void);
void MSG_EnableRX(bool enable);
void MSG_StorePacket(uint16_t interrupt_bits);
void MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

#endif
#endif
