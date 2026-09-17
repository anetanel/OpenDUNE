#include <string.h>

#include "types.h"
#include "../timer.h"

#include "video_fps.h"

/* The digit glyphs are 3px wide with a 1px gap (4px per digit) and 5px
 * tall (see Video_ShowFPS_DrawChar()); 10 digits comfortably covers the
 * largest value Video_ShowFPS_2() can compute. */
#define FPS_AREA_HEIGHT 5
#define FPS_AREA_MAX_DIGITS 10
#define FPS_AREA_WIDTH (FPS_AREA_MAX_DIGITS * 4)
#define FPS_AREA_LEFT (320 - FPS_AREA_WIDTH)

static void Video_ShowFPS_DrawChar(uint8 * screen, int bytes_per_row, uint16 x, uint8 digit)
{
	int i;
	static const uint8 fontdigits[10] = {0167,044,0135,0155,056,0153,0173,045,0177,0157};
	static const uint8 fonttestsegments[15] = {03,01,05, 02,0,04, 032,010,054, 020,0,040, 0120,0100,0140};
	uint8 segments = fontdigits[digit];
	int offset = 0;
	for(i=0; i<15; i++) {
		screen[x+offset] = (segments & fonttestsegments[i]) ? 15 : 0;
		offset++;
		if((i % 3) == 2) {
			screen[x+offset] = 0;
			offset += bytes_per_row - 3;
		}
	}
}

void Video_ShowFPS_2(uint8 *screen, int bytes_per_row, bool enabled, Video_ShowFPS_Proc drawchar)
{
	uint32 timeStamp;
	static uint32 s_previousTimeStamps[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	static uint8 s_previousTimeStampsIndex = 0;
	/* The digits are drawn straight into the live game screen buffer
	 * (whatever callers pass in is SCREEN_0 itself, not a copy), so
	 * disabling the display or drawing fewer digits than last time would
	 * otherwise leave stale digit pixels behind forever -- nothing else
	 * repaints this corner on its own. Freeze a copy of what was really
	 * there the moment the overlay is first turned on, and re-composite
	 * onto that same frozen backdrop every frame (rather than the
	 * evolving live content) so toggling off -- or the digit count
	 * shrinking -- can cleanly restore it. */
	static uint8 s_backup[FPS_AREA_HEIGHT][FPS_AREA_WIDTH];
	static bool s_backedUp = false;
	int row;

	if (!enabled) {
		if (s_backedUp) {
			for (row = 0; row < FPS_AREA_HEIGHT; row++) {
				memcpy(screen + FPS_AREA_LEFT + row * bytes_per_row, s_backup[row], FPS_AREA_WIDTH);
			}
			s_backedUp = false;
		}
		return;
	}

	if (!s_backedUp) {
		for (row = 0; row < FPS_AREA_HEIGHT; row++) {
			memcpy(s_backup[row], screen + FPS_AREA_LEFT + row * bytes_per_row, FPS_AREA_WIDTH);
		}
		s_backedUp = true;
	} else {
		for (row = 0; row < FPS_AREA_HEIGHT; row++) {
			memcpy(screen + FPS_AREA_LEFT + row * bytes_per_row, s_backup[row], FPS_AREA_WIDTH);
		}
	}

	timeStamp = Timer_GetTime();
	if(s_previousTimeStamps[s_previousTimeStampsIndex] > 0
			&& timeStamp != s_previousTimeStamps[s_previousTimeStampsIndex]) {
		int x, i;
		/* calculate average frames per 1000sec on the 16 last time measures */
		uint32 kfps = 16000000 / (timeStamp - s_previousTimeStamps[s_previousTimeStampsIndex]);
		for(x = 320 - 4; kfps > 0; kfps /= 10, x -= 4) {
			/* draw the digits */
			if (drawchar)
				drawchar(screen, x, kfps % 10);
			else
				Video_ShowFPS_DrawChar(screen, bytes_per_row, x, kfps % 10);
		}
		if (!drawchar) {
			for (i=0; i<5; i++) screen[x+2+i*bytes_per_row] = 0;
		}
	}
	s_previousTimeStamps[s_previousTimeStampsIndex] = timeStamp;
	s_previousTimeStampsIndex = (s_previousTimeStampsIndex + 1) & 0x0f;
}
