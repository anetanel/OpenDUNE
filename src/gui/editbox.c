/** @file src/gui/editbox.c Editbox routines. */

#include <stdio.h>
#include "types.h"
#include "../os/sleep.h"

#include "font.h"
#include "gui.h"
#include "widget.h"
#include "../gfx.h"
#include "../input/input.h"
#include "../timer.h"
#include "../video/video.h"

/**
 * Draw a blinking cursor, used inside the EditBox.
 *
 * @param positionX Where to draw the cursor on the X position.
 * @param resetBlink If true, the blinking is reset and restarted.
 */
static void GUI_EditBox_BlinkCursor(uint16 positionX, bool resetBlink)
{
	static uint32 tickEditBox = 0;           /* Ticker for cursor blinking. */
	static bool   editBoxShowCursor = false; /* Cursor is active. */

	if (resetBlink) {
		tickEditBox = 0;
		editBoxShowCursor = true;
	}

	if (tickEditBox > g_timerGUI) return;
	if (!resetBlink) {
		tickEditBox = g_timerGUI + 20;
	}

	editBoxShowCursor = !editBoxShowCursor;

	GUI_Mouse_Hide_Safe();
	GUI_DrawFilledRectangle(positionX, g_curWidgetYBase, positionX + Font_GetCharWidth('W'), g_curWidgetYBase + g_curWidgetHeight - 1, (editBoxShowCursor) ? g_curWidgetFGColourBlink : g_curWidgetFGColourNormal);
	GUI_Mouse_Show_Safe();
}

/**
 * X position of the idle text-entry cursor. For a right-to-left (Hebrew)
 * editbox, text is right-anchored at `rtlAnchorX` and grows toward the
 * left as more is typed, so the cursor sits `textWidth` pixels to the
 * *left* of that anchor (minus one more character's width, reserved so
 * the cursor block itself always fits inside the widget); for every other
 * language, text is left-anchored at `positionX` and grows rightward, so
 * the cursor sits `textWidth` pixels to the *right* of it.
 */
static uint16 GUI_EditBox_CursorX(bool rtl, uint16 positionX, uint16 rtlAnchorX, uint16 textWidth)
{
	if (rtl) return rtlAnchorX - textWidth - Font_GetCharWidth('W');
	return positionX + textWidth;
}

/**
 * Show an EditBox and handles the input.
 * @param text The text to edit. Uses the pointer to make the modifications.
 * @param maxLength The maximum length of the text.
 * @param widgetID the widget in which the EditBox is displayed.
 * @param w The widget this editbox is attached to (for input events).
 * @param tickProc The function to call every tick, for animation etc.
 * @param paint Flag indicating if the widget need to be repainted.
 * @return Key code / Button press code.
 */
uint16 GUI_EditBox(char *text, uint16 maxLength, uint16 widgetID, Widget *w, uint16 (*tickProc)(void), bool paint)
{
	Screen oldScreenID;
	uint16 oldWidgetID;
	uint16 positionX;
	uint16 rtlAnchorX;
	bool rtl;
	uint16 maxWidth;
	uint16 textWidth;
	uint16 textLength;
	uint16 returnValue;
	char *t;

	/* Initialize */
	{
		Input_Flags_SetBits(INPUT_FLAG_NO_TRANSLATE);
		Input_Flags_ClearBits(INPUT_FLAG_KBD_MOUSE_CLK);

		oldScreenID = GFX_Screen_SetActive(SCREEN_0);

		oldWidgetID = Widget_SetCurrentWidget(widgetID);

		returnValue = 0x0;

		/* Any Hebrew-toggle keys pressed before this editbox opened (menu
		 * navigation, an earlier screen, etc.) still pushed a queue entry
		 * each with nothing to pop them, since only an active editbox
		 * ever does -- see Video_ClearHebrewTextInput(). Left alone,
		 * those stray entries would desync every pop in *this* session
		 * from its own pushes by however many leaked in beforehand. */
		Video_ClearHebrewTextInput();
	}

	positionX = g_curWidgetXBase << 3;
	rtl = GUI_IsRTLLanguage();
	rtlAnchorX = positionX + (g_curWidgetWidth << 3) - 1;

	textWidth = 0;
	textLength = 0;
	maxWidth = (g_curWidgetWidth << 3) - Font_GetCharWidth('W') - 1;
	t = text;

	/* Calculate the length and width of the current string */
	for (; *t != '\0'; t++) {
		textWidth += Font_GetCharWidth(*t);
		textLength++;

		if (textWidth >= maxWidth) break;
	}
	*t = '\0';

	GUI_Mouse_Hide_Safe();

	if (paint) Widget_PaintCurrentWidget();

	GUI_DrawText_Wrapper(text, rtl ? rtlAnchorX : positionX, g_curWidgetYBase, g_curWidgetFGColourBlink, g_curWidgetFGColourNormal, rtl ? 0x0200 : 0);

	GUI_EditBox_BlinkCursor(GUI_EditBox_CursorX(rtl, positionX, rtlAnchorX, textWidth), false);

	GUI_Mouse_Show_Safe();

	for (;; sleepIdle()) {
		uint16 keyWidth;
		uint16 key;
		bool keyIsUp;
		bool isMouseButton;
		uint8 hebrewKey;

		if (tickProc != NULL) {
			returnValue = tickProc();
			if (returnValue != 0) break;
		}

		key = GUI_Widget_HandleEvents(w);

		GUI_EditBox_BlinkCursor(GUI_EditBox_CursorX(rtl, positionX, rtlAnchorX, textWidth), false);

		if (key == 0x0) continue;

		if ((key & 0x8000) != 0) {
			returnValue = key;
			break;
		}

		/* video_sdl2.c pushes exactly one Hebrew-queue entry per SDL
		 * keydown it forwards to the game (0 if that key wasn't a Hebrew
		 * letter under the current toggle state), never for key-up, and
		 * never for mouse clicks (see the isMouseButton comment below).
		 * Every OTHER branch below -- Enter, Escape, Backspace, and the
		 * general letter path -- corresponds 1:1 to one of those pushes,
		 * so the pop must happen here, before any of them, even for the
		 * branches that don't use the value: otherwise e.g. Backspace
		 * would leave its "0" push stranded in the queue, and the next
		 * real letter would pop *that* stale entry instead of its own,
		 * desyncing every Hebrew letter typed after any Backspace. */
		keyIsUp = (key & 0x0800) != 0;
		isMouseButton = (key & 0xFF) == 0x41 || (key & 0xFF) == 0x42;
		if (rtl && !keyIsUp && !isMouseButton) {
			hebrewKey = Video_GetHebrewTextInput();
		} else {
			hebrewKey = 0;
		}

		if (key == 0x2B) {
			returnValue = 0x2B;
			break;
		}
		if (key == 0x6E) {
			*t = '\0';
			returnValue = 0x6B;
			break;
		}

		/* Handle backspace */
		if (key == 0x0F) {
			if (textLength == 0) continue;

			GUI_EditBox_BlinkCursor(GUI_EditBox_CursorX(rtl, positionX, rtlAnchorX, textWidth), true);

			if (rtl) {
				/* GUI_MirrorRTLText()/GUI_MirrorRTLLine() (gui.c) decide the
				 * visual layout of the *whole* line -- e.g. a line with no
				 * Hebrew bytes at all is left completely untouched
				 * (ordinary left-to-right), while Hebrew is reversed and
				 * ASCII words within it are kept intact. Any edit can
				 * change that outcome for the *entire* string, not just
				 * near the edited character (e.g. removing the only
				 * Hebrew byte flips the whole line from "reversed" to
				 * "untouched"), so the only correct way to keep the
				 * on-screen text matching this is to erase the whole
				 * editable area and redraw the full (new) string through
				 * the same right-aligned call every time, rather than
				 * trying to patch just the edited character's own
				 * position. */
				GUI_DrawFilledRectangle(positionX, g_curWidgetYBase, rtlAnchorX, g_curWidgetYBase + g_curWidgetHeight - 1, g_curWidgetFGColourNormal);

				textWidth -= Font_GetCharWidth(*(t - 1));
				textLength--;
				*(--t) = '\0';

				if (textLength > 0) {
					GUI_DrawText_Wrapper(text, rtlAnchorX, g_curWidgetYBase, g_curWidgetFGColourBlink, g_curWidgetFGColourNormal, 0x0200);
				}
			} else {
				textWidth -= Font_GetCharWidth(*(t - 1));
				textLength--;
				*(--t) = '\0';
			}

			GUI_EditBox_BlinkCursor(GUI_EditBox_CursorX(rtl, positionX, rtlAnchorX, textWidth), false);
			continue;
		}

		key = Input_Keyboard_HandleKeys(key) & 0xFF;

		/* The in-game Hebrew-keyboard toggle already gave us the actual
		 * typed Hebrew letter above (popped before this branch/backspace/
		 * Enter/Escape were told apart -- see the comment there) --
		 * substitute it for whatever the DOS-scancode-based English
		 * keymap above produced. Mouse clicks (isMouseButton) never had
		 * an entry pushed for them, so hebrewKey is always 0 there. */
		if (hebrewKey != 0) key = hebrewKey;

		/* Names can't start with a space, and should be alpha-numeric --
		 * or, in Hebrew, one of the font's Hebrew glyph bytes (0x80-0x9A,
		 * see hebrew/tools/eng.py). */
		if ((key == 0x20 && textLength == 0)) continue;
		if (!((key >= 0x20 && key <= 0x7E) || (key >= 0x80 && key <= 0x9A))) continue;

		keyWidth = Font_GetCharWidth(key & 0xFF);

		if (textWidth + keyWidth >= maxWidth || textLength >= maxLength) continue;

		GUI_Mouse_Hide_Safe();

		GUI_EditBox_BlinkCursor(GUI_EditBox_CursorX(rtl, positionX, rtlAnchorX, textWidth), true);

		if (rtl) {
			/* See the long comment in the backspace handler above: the
			 * whole line's visual layout is decided by
			 * GUI_MirrorRTLText()/GUI_MirrorRTLLine() (gui.c) and can
			 * change entirely with a single edit (e.g. typing the first
			 * Hebrew byte into an all-ASCII string flips it from
			 * "untouched" to "reversed, ASCII words kept intact"), so
			 * erase the whole editable area and redraw the full string
			 * every time rather than trying to patch just the new
			 * character's own position. */
			GUI_DrawFilledRectangle(positionX, g_curWidgetYBase, rtlAnchorX, g_curWidgetYBase + g_curWidgetHeight - 1, g_curWidgetFGColourNormal);

			*t = key & 0xFF;
			*(++t) = '\0';
			textLength++;
			textWidth += keyWidth;

			GUI_DrawText_Wrapper(text, rtlAnchorX, g_curWidgetYBase, g_curWidgetFGColourBlink, g_curWidgetFGColourNormal, 0x0200);
		} else {
			/* Add char to the text */
			*t = key & 0xFF;
			*(++t) = '\0';
			textLength++;
			textWidth += keyWidth;

			/* Draw new character */
			GUI_DrawText_Wrapper(text + textLength - 1, positionX + textWidth - keyWidth, g_curWidgetYBase, g_curWidgetFGColourBlink, g_curWidgetFGColourNormal, 0x020);
		}

		GUI_Mouse_Show_Safe();

		GUI_EditBox_BlinkCursor(GUI_EditBox_CursorX(rtl, positionX, rtlAnchorX, textWidth), false);
	}

	/* Deinitialize */
	{
		Input_Flags_ClearBits(INPUT_FLAG_NO_TRANSLATE);
		Input_Flags_SetBits(INPUT_FLAG_KBD_MOUSE_CLK);

		Widget_SetCurrentWidget(oldWidgetID);

		GFX_Screen_SetActive(oldScreenID);
	}

	return returnValue;
}
