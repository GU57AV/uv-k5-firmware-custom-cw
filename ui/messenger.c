/* Messenger screen for the NR7Y CW firmware (UV-K5 V1)
 * Layout idea from joaquimorg / kamilsss655, redrawn for the small font.
 * Licensed under the Apache License, Version 2.0
 */

#ifdef ENABLE_MESSENGER

#include <string.h>

#include "app/messenger.h"
#include "driver/st7565.h"
#include "ui/helper.h"
#include "ui/messenger.h"

#define COLS 18   // small font: 7 px per character

static void PrintCols(const char *s, uint8_t line)
{
	char buf[COLS + 1];
	strncpy(buf, s, COLS);
	buf[COLS] = 0;
	UI_PrintStringSmallNormal(buf, 0, 0, line);
}

// append a callsign field (not NUL terminated when 6 long)
static char *AddCall(char *p, const char *call)
{
	for (unsigned i = 0; i < MSG_CALL_LEN && call[i]; i++)
		*p++ = call[i];
	*p = 0;
	return p;
}

// FROM NR7Y          route view of one message
// VIA W1AW
//     K2ABC
// <text, two rows>
static void DisplayRoute(const MsgEntry *e)
{
	char line[COLS + 8];

	strcpy(line, e->kind == '<' ? "FROM " : "SENT ");
	AddCall(line + 5, e->src);
	PrintCols(line, 0);

	if (e->path[0][0] == 0)
		PrintCols("DIRECT", 1);                    // no relay (for our own: none heard)
	for (unsigned i = 0; i < MSG_MAX_HOPS && e->path[i][0]; i++) {
		strcpy(line, i ? "    " : "VIA ");
		AddCall(line + 4, e->path[i]);
		PrintCols(line, 1 + i);
	}

	PrintCols(e->text, 4);
	if (strlen(e->text) > COLS)
		PrintCols(e->text + COLS, 5);
}

void UI_DisplayMSG(void)
{
	static const char kbd[4] = { 'A', 'a', '1', 'C' };
	char line[MSG_CALL_LEN + MSG_PAYLOAD_LEN + 2];

	UI_DisplayClear();

	if (gMsgView) {
		DisplayRoute(&gMsgLog[MSG_HISTORY - gMsgView]);
		ST7565_BlitFullScreen();
		return;
	}

	// three newest messages, two rows each:
	//   "NR7Y:text"  received      ">text" sent      "+text" sent and heard relayed
	for (unsigned i = 0; i < MSG_HISTORY; i++) {
		const MsgEntry *e = &gMsgLog[i];
		if (!e->kind)
			continue;
		char *p = line;
		if (e->kind == '<') {
			p = AddCall(p, e->src);
			*p++ = e->path[0][0] ? '>' : ':';     // '>' = came via a relay
		} else {
			*p++ = e->kind;
		}
		strcpy(p, e->text);
		PrintCols(line, i * 2);
		if (strlen(line) > COLS)
			PrintCols(line + COLS, i * 2 + 1);
	}

	// input row: keyboard mode, then the tail of what is being typed,
	// or my callsign (or a reminder to set one) when nothing is typed
	const size_t len  = strlen(gMsgInput);
	const size_t tail = len > COLS - 3 ? len - (COLS - 3) : 0;
	line[0] = kbd[gMsgKbd];
	line[1] = ':';
	strcpy(line + 2, gMsgInput + tail);
	strcat(line, "_");
	if (len == 0) {
		if (gMsgMyCall[0])
			AddCall(strcpy(line + 3, "      ") + 6, gMsgMyCall);
		else
			strcpy(line + 3, " NO CALL SET");
	}
	PrintCols(line, 6);

	ST7565_BlitFullScreen();
}

#endif
