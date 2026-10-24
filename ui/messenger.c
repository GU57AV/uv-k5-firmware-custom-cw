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

void UI_DisplayMSG(void)
{
	static const char kbd[3] = { 'A', 'a', '1' };

	UI_DisplayClear();

	// three newest messages, two lines each ('<' received, '>' sent, '+' sent and relayed)
	for (unsigned i = 0; i < MSG_HISTORY; i++) {
		const char *m = gMsgLines[i];
		PrintCols(m, i * 2);
		if (strlen(m) > COLS)
			PrintCols(m + COLS, i * 2 + 1);
	}

	// input line: keyboard mode, then the tail of what is being typed
	char line[COLS + 2];
	const size_t len  = strlen(gMsgInput);
	const size_t tail = len > COLS - 3 ? len - (COLS - 3) : 0;
	line[0] = kbd[gMsgKbd];
	line[1] = ':';
	strcpy(line + 2, gMsgInput + tail);
	strcat(line, "_");
	PrintCols(line, 6);

	ST7565_BlitFullScreen();
}

#endif
