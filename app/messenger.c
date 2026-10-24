/* Messenger + mesh relaying for the NR7Y CW firmware (UV-K5 V1)
 *
 * T9 entry and BK4819 FSK modem configuration derived from
 *   Copyright 2023 joaquimorg  https://github.com/joaquimorg
 *   Copyright 2024 kamilsss655 https://github.com/kamilsss655
 *
 * Mesh relaying: clean-room implementation of the behaviour documented for
 * kamilsss655's NUNU protocol:
 *   - flooding with a hop limit (3 header bits)
 *   - carrier sense + random backoff before every transmission
 *   - CRC check, corrupted packets are never relayed
 *   - "give and take": a station that does not relay sends with 0 hops,
 *     so only relaying stations get their own messages relayed
 * Additions:
 *   - APRS-style path: every packet carries the originator's callsign and
 *     each relay writes its own callsign into the next path slot, so the
 *     receiver sees the route (and every station identifies itself)
 *   - a random message id so each station shows and relays a message once
 *   - implicit delivery mark: when we hear a neighbour relay our own
 *     message, '>' turns into '+' and we record who relayed it
 *
 * There is deliberately NO encryption (amateur radio rules).
 *
 * Licensed under the Apache License, Version 2.0
 */

#ifdef ENABLE_MESSENGER

#include <string.h>

#include "app/generic.h"
#include "app/messenger.h"
#include "audio.h"
#include "driver/bk4819.h"
#include "driver/crc.h"
#include "driver/eeprom.h"
#include "driver/system.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/ui.h"

#define NEXT_CHAR_DELAY  100     // 10 ms ticks: pause after which the same key starts a new letter
#define CRC_LEN          (sizeof(MeshPacket) - 2)
#define SEEN_SIZE        8
#define EEPROM_MYCALL    0x0F48  // 8 bytes, unused by the rest of the firmware, kept on reset

_Static_assert(sizeof(MeshPacket) == 60, "packet layout");
_Static_assert((sizeof(MeshPacket) & 1) == 0, "FIFO is 16 bit");
_Static_assert(MSG_MAX_HOPS >= 1 && MSG_MAX_HOPS <= 7, "hop field is 3 bits");

uint8_t  hasNewMessage;
uint8_t  gMsgKeyTick;
uint8_t  gMsgKbd;                                  // 0 = ABC, 1 = abc, 2 = 123
uint8_t  gMsgView;
char     gMsgInput[MSG_PAYLOAD_LEN];
char     gMsgMyCall[MSG_CALL_LEN];
MsgEntry gMsgLog[MSG_HISTORY];

static char       s_lastSent[MSG_PAYLOAD_LEN];
static uint8_t    s_cursor;
static uint8_t    s_prevKey;
static uint8_t    s_prevLetter;
static bool       s_receiving;
static MeshPacket s_pkt;
static uint8_t    s_seen[SEEN_SIZE][MSG_ID_LEN];
static uint8_t    s_seenPos;
static uint8_t    s_sentId[MSG_ID_LEN];
static uint8_t    s_sentLine = 0xFF;               // log entry holding our last sent message
static uint32_t   s_rng = 0x2545F491u;

static const char *const T9[9] = {
	".,?!-/", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"
};

// ---------------------------------------------------------------- helpers

static uint8_t Rand(void)
{	// xorshift, continuously stirred with RF noise from the RSSI reading
	s_rng ^= BK4819_GetRSSI();
	s_rng ^= s_rng << 13;
	s_rng ^= s_rng >> 17;
	s_rng ^= s_rng << 5;
	return (uint8_t)s_rng;
}

static bool SeenBefore(const uint8_t *id)
{
	for (unsigned i = 0; i < SEEN_SIZE; i++)
		if (memcmp(s_seen[i], id, MSG_ID_LEN) == 0)
			return true;

	memcpy(s_seen[s_seenPos], id, MSG_ID_LEN);
	s_seenPos = (s_seenPos + 1) % SEEN_SIZE;
	return false;
}

static MsgEntry *NewEntry(char kind)
{
	memmove(&gMsgLog[0], &gMsgLog[1], sizeof(MsgEntry) * (MSG_HISTORY - 1));
	MsgEntry *e = &gMsgLog[MSG_HISTORY - 1];
	memset(e, 0, sizeof(*e));
	e->kind = kind;

	if (s_sentLine < MSG_HISTORY)
		s_sentLine = s_sentLine ? s_sentLine - 1 : 0xFF;

	gUpdateDisplay = true;
	return e;
}

static void SealPacket(uint8_t hops)
{
	s_pkt.d.header = MSG_HDR(MSG_TYPE_TEXT, hops);
	s_pkt.d.crc    = CRC_Calculate(s_pkt.raw, CRC_LEN);
}

static void SetMyCall(const char *s)
{	// first word of the input, up to 6 characters (type it in ABC mode)
	uint8_t buf[8];
	memset(buf, 0, sizeof(buf));
	for (unsigned i = 0; i < MSG_CALL_LEN && s[i] > ' '; i++)
		buf[i] = s[i];
	memcpy(gMsgMyCall, buf, MSG_CALL_LEN);
	EEPROM_WriteBuffer(EEPROM_MYCALL, buf);
}

// ---------------------------------------------------------------- modem

static void ConfigureFSK(bool rx)
{
	const unsigned mod = gEeprom.MESSENGER_CONFIG.data.modulation;

	// TONE2 = FSK, tuning gain 96
	BK4819_WriteRegister(BK4819_REG_70, (1u << 7) | 96u);

	// TONE2 frequency sets the baud rate
	static const uint16_t baud[3] = { 4646u, 7227u, 12389u };
	BK4819_WriteRegister(BK4819_REG_72, baud[mod]);

	// FSK TX/RX mode: direct FSK for 450/700, FFSK 1200/1800 for AFSK 1.2k;
	// RX gain 3, FSK enable
	BK4819_WriteRegister(BK4819_REG_58, (mod == MOD_AFSK_1200)
		? ((1u << 13) | (7u << 10) | (3u << 8) | (1u << 1) | 1u)
		: (                          (3u << 8)              | 1u));

	// sync word 0x30 0x72 0x57 0x6C (same as the NUNU protocol)
	BK4819_WriteRegister(BK4819_REG_5A, 0x3072);
	BK4819_WriteRegister(BK4819_REG_5B, 0x576C);

	// no hardware CRC, we check our own
	BK4819_WriteRegister(BK4819_REG_5C, 0x5625);

	uint16_t size = sizeof(s_pkt.raw);
	if (rx) {
		BK4819_WriteRegister(BK4819_REG_5E, (64u << 3) | 1u);   // FIFO almost-full threshold
		size += 2;                                              // RX needs the extra word
	}
	BK4819_WriteRegister(BK4819_REG_5D, size << 8);

	// clear both FIFOs, then preamble length (TX only) + 4 byte sync
	BK4819_WriteRegister(BK4819_REG_59, (1u << 15) | (1u << 14));
	BK4819_WriteRegister(BK4819_REG_59, ((rx ? 0u : 15u) << 4) | (1u << 3));

	BK4819_WriteRegister(BK4819_REG_02, 0);
}

void MSG_EnableRX(bool enable)
{
	if (enable) {
		ConfigureFSK(true);
		BK4819_WriteRegister(BK4819_REG_59, BK4819_ReadRegister(BK4819_REG_59) | (1u << 12));
	} else {
		BK4819_WriteRegister(BK4819_REG_70, 0);
		BK4819_WriteRegister(BK4819_REG_58, 0);
	}
}

static void SendFSK(void)
{
	// no CTCSS/DCS during data
	const uint16_t css = BK4819_ReadRegister(BK4819_REG_51);
	BK4819_WriteRegister(BK4819_REG_51, 0);

	// deviation to suit the channel bandwidth
	const uint16_t dev = BK4819_ReadRegister(BK4819_REG_40);
	const uint16_t d   = (gTxVfo->CHANNEL_BANDWIDTH == BK4819_FILTER_BW_WIDE)   ? 1300 :
	                     (gTxVfo->CHANNEL_BANDWIDTH == BK4819_FILTER_BW_NARROW) ? 1200 : 850;
	BK4819_WriteRegister(BK4819_REG_40, (dev & 0xF000) | d);

	// no 300 Hz HPF or pre-emphasis on TX
	const uint16_t filt = BK4819_ReadRegister(BK4819_REG_2B);
	BK4819_WriteRegister(BK4819_REG_2B, (1u << 2) | (1u << 0));

	ConfigureFSK(false);
	SYSTEM_DelayMs(100);

	for (unsigned i = 0; i < sizeof(s_pkt.raw); i += 2)
		BK4819_WriteRegister(BK4819_REG_5F, (s_pkt.raw[i + 1] << 8) | s_pkt.raw[i]);

	BK4819_WriteRegister(BK4819_REG_59, BK4819_ReadRegister(BK4819_REG_59) | (1u << 11));

	// wait up to 1.5 s for "TX finished"
	for (unsigned t = 0; t < 300; t++) {
		SYSTEM_DelayMs(5);
		if (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
			BK4819_WriteRegister(BK4819_REG_02, 0);
			if (BK4819_ReadRegister(BK4819_REG_02) & BK4819_REG_02_FSK_TX_FINISHED)
				break;
		}
	}

	SYSTEM_DelayMs(100);

	BK4819_WriteRegister(BK4819_REG_40, dev);
	BK4819_WriteRegister(BK4819_REG_2B, filt);
	BK4819_WriteRegister(BK4819_REG_51, css);
}

// Carrier sense, key up, send s_pkt, back to RX. Returns false if TX was refused.
static bool Transmit(void)
{
	if (gTxVfo->Modulation != MODULATION_FM)
		return false;

	// CSMA/CA: random backoff until the channel is quiet (gives up waiting after ~4 s)
	for (unsigned i = 0; i < 40; i++) {
		SYSTEM_DelayMs(40 + (Rand() & 63));
		if (((BK4819_ReadRegister(BK4819_REG_0C) >> 1) & 1u) == 0)   // squelch closed
			break;
	}

	RADIO_PrepareTX();
	if (gCurrentFunction != FUNCTION_TRANSMIT)
		return false;

	BK4819_DisableScramble();
	BK4819_WriteRegister(BK4819_REG_30, BK4819_ReadRegister(BK4819_REG_30) & ~(1u << 2));  // mic off
	SYSTEM_DelayMs(50);

	SendFSK();

	RADIO_SetupRegisters(true);   // RX again, re-arms the FSK receiver
	return true;
}

// ---------------------------------------------------------------- receive

static void HandleReceive(void)
{
	if (CRC_Calculate(s_pkt.raw, CRC_LEN) != s_pkt.d.crc ||
	    MSG_HDR_TYPE(s_pkt.d.header) != MSG_TYPE_TEXT ||
	    s_pkt.d.src[0] == 0)
		return;                                   // corrupted, not ours, or anonymous: drop

	s_pkt.d.payload[MSG_PAYLOAD_LEN - 1] = 0;

	if (SeenBefore(s_pkt.d.id)) {
		// a neighbour relaying our own message = it got out; remember who did
		if (s_sentLine < MSG_HISTORY && memcmp(s_pkt.d.id, s_sentId, MSG_ID_LEN) == 0 &&
		    gMsgLog[s_sentLine].kind == '>') {
			gMsgLog[s_sentLine].kind = '+';
			memcpy(gMsgLog[s_sentLine].path, s_pkt.d.path, sizeof(s_pkt.d.path));
			gUpdateDisplay = true;
		}
		return;
	}

	MsgEntry *e = NewEntry('<');
	memcpy(e->src,  s_pkt.d.src,     sizeof(e->src));
	memcpy(e->path, s_pkt.d.path,    sizeof(e->path));
	memcpy(e->text, s_pkt.d.payload, sizeof(e->text));

	if (gScreenToDisplay != DISPLAY_MSG) {
		hasNewMessage = 1;
		gUpdateStatus = true;
	}

	// relay: only with hops left, a free path slot, and our own callsign to put in it
	const uint8_t hops = MSG_HDR_HOPS(s_pkt.d.header);
	if (!gEeprom.MESSENGER_CONFIG.data.relay || hops == 0 || gMsgMyCall[0] == 0)
		return;

	for (unsigned i = 0; i < MSG_MAX_HOPS; i++) {
		if (s_pkt.d.path[i][0] == 0) {
			memcpy(s_pkt.d.path[i], gMsgMyCall, MSG_CALL_LEN);
			SealPacket(hops - 1);
			// extra spread so several relays that heard the same packet don't key up together
			SYSTEM_DelayMs(150 + 3u * Rand());
			Transmit();
			return;
		}
	}
}

void MSG_StorePacket(uint16_t irq)
{
	if (irq & BK4819_REG_02_FSK_RX_SYNC) {
#ifdef ENABLE_MESSENGER_FSK_MUTE
		if (gCurrentCodeType == CODE_TYPE_OFF)
			AUDIO_AudioPathOff();                 // don't play the data burst
#endif
		gFSKWriteIndex = 0;
		memset(s_pkt.raw, 0, sizeof(s_pkt.raw));
		s_receiving = true;
	}

	if ((irq & BK4819_REG_02_FSK_FIFO_ALMOST_FULL) && s_receiving) {
		const uint16_t count = BK4819_ReadRegister(BK4819_REG_5E) & 7u;
		for (uint16_t i = 0; i < count; i++) {
			const uint16_t word = BK4819_ReadRegister(BK4819_REG_5F);
			if (gFSKWriteIndex < sizeof(s_pkt.raw)) s_pkt.raw[gFSKWriteIndex++] = word & 0xFF;
			if (gFSKWriteIndex < sizeof(s_pkt.raw)) s_pkt.raw[gFSKWriteIndex++] = word >> 8;
		}
	}

	if (irq & BK4819_REG_02_FSK_RX_FINISHED) {
		BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
		MSG_EnableRX(true);                       // clear the FIFO and listen again
		const bool full = s_receiving && gFSKWriteIndex >= sizeof(s_pkt.raw);
		s_receiving    = false;
		gFSKWriteIndex = 0;
		if (full)
			HandleReceive();
	}
}

// ---------------------------------------------------------------- send

static void Send(void)
{
	if (gMsgInput[0] == 0 || gMsgMyCall[0] == 0) {   // nothing to send / no callsign set
		gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
		return;
	}

	memset(s_pkt.raw, 0, sizeof(s_pkt.raw));
	for (unsigned i = 0; i < MSG_ID_LEN; i++)
		s_pkt.d.id[i] = Rand();
	SeenBefore(s_pkt.d.id);                       // so our own echoes are recognised
	memcpy(s_sentId, s_pkt.d.id, MSG_ID_LEN);
	memcpy(s_pkt.d.src, gMsgMyCall, MSG_CALL_LEN);
	strncpy(s_pkt.d.payload, gMsgInput, MSG_PAYLOAD_LEN - 1);

	// give and take: only stations that relay ask to be relayed
	SealPacket(gEeprom.MESSENGER_CONFIG.data.relay ? MSG_MAX_HOPS : 0);

	if (!Transmit()) {
		gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
		return;
	}

	MsgEntry *e = NewEntry('>');
	memcpy(e->src,  gMsgMyCall, sizeof(e->src));
	memcpy(e->text, gMsgInput,  sizeof(e->text));
	s_sentLine = MSG_HISTORY - 1;
	memcpy(s_lastSent, gMsgInput, sizeof(s_lastSent));
	memset(gMsgInput, 0, sizeof(gMsgInput));
	s_cursor = s_prevKey = 0;
}

void MSG_Init(void)
{
	memset(gMsgLog, 0, sizeof(gMsgLog));
	memset(gMsgInput, 0, sizeof(gMsgInput));
	s_cursor = s_prevKey = s_prevLetter = 0;
	s_sentLine    = 0xFF;
	hasNewMessage = 0;
	gMsgView      = 0;

	EEPROM_ReadBuffer(EEPROM_MYCALL, gMsgMyCall, MSG_CALL_LEN);
	if ((uint8_t)gMsgMyCall[0] == 0xFF)           // never set
		memset(gMsgMyCall, 0, sizeof(gMsgMyCall));
}

// ---------------------------------------------------------------- keys

static void InsertChar(KEY_Code_t key)
{
	char c;

	if (key == KEY_0) {
		c = (gMsgKbd == 2) ? '0' : ' ';
		s_prevKey = 0;
	}
	else if (gMsgKbd == 2) {
		c = '0' + key;
		s_prevKey = 0;
	}
	else {
		const char *set = T9[key - 1];
		if (s_prevKey == key && gMsgKeyTick < NEXT_CHAR_DELAY && s_cursor > 0) {
			s_cursor--;                           // same key again: next letter on that key
			s_prevLetter = (s_prevLetter + 1) % strlen(set);
		} else {
			s_prevLetter = 0;
		}
		c = set[s_prevLetter];
		if (gMsgKbd == 0 && c >= 'a')
			c -= 'a' - 'A';
		s_prevKey = key;
	}

	if (s_cursor >= MSG_PAYLOAD_LEN - 1)
		return;                                   // full

	gMsgInput[s_cursor++] = c;
	gMsgInput[s_cursor]   = 0;
	gMsgKeyTick = 0;
}

void MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	if (Key == KEY_PTT) {                         // voice still works on this screen
		GENERIC_Key_PTT(bKeyPressed);
		if (gRequestDisplayScreen == DISPLAY_MAIN)
			gRequestDisplayScreen = DISPLAY_INVALID;  // ..and we stay on it
		return;
	}

	if (bKeyHeld) {
		if (bKeyPressed && Key == KEY_F)          // long F: clear the line
			memset(gMsgInput, 0, sizeof(gMsgInput)), s_cursor = s_prevKey = 0;
		if (bKeyPressed && Key == KEY_MENU && gMsgInput[0]) {
			SetMyCall(gMsgInput);                 // long MENU: typed text becomes my callsign
			memset(gMsgInput, 0, sizeof(gMsgInput)), s_cursor = s_prevKey = 0;
			gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
		}
		gUpdateDisplay = true;                    // (the release after a hold also lands here)
		return;
	}

	if (bKeyPressed)
		return;                                   // act on release

	if (gMsgView && Key != KEY_DOWN) {            // route view: DOWN = older, anything else = back
		gMsgView = 0;
		gUpdateDisplay = true;
		return;
	}

	switch (Key) {
		case KEY_0 ... KEY_9:
			InsertChar(Key);
			break;
		case KEY_STAR:
			gMsgKbd = (gMsgKbd + 1) % 3;
			s_prevKey = 0;
			break;
		case KEY_F:                               // backspace
			if (s_cursor)
				gMsgInput[--s_cursor] = 0;
			s_prevKey = 0;
			break;
		case KEY_UP:                              // recall last sent message
			memcpy(gMsgInput, s_lastSent, sizeof(gMsgInput));
			s_cursor  = strlen(gMsgInput);
			s_prevKey = 0;
			break;
		case KEY_DOWN:                            // route of the newest, then older messages
			while (++gMsgView <= MSG_HISTORY && gMsgLog[MSG_HISTORY - gMsgView].kind == 0)
				;
			if (gMsgView > MSG_HISTORY)
				gMsgView = 0;                     // past the oldest: back to the list
			break;
		case KEY_MENU:
			Send();
			break;
		case KEY_EXIT:
			gRequestDisplayScreen = DISPLAY_MAIN;
			break;
		default:
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			break;
	}
	gUpdateDisplay = true;
}

#endif
