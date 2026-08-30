/** @file src/video/video.h Definitions of a video driver. */

#ifndef VIDEO_VIDEO_H
#define VIDEO_VIDEO_H

typedef enum VideoScaleFilter {
	FILTER_NEAREST_NEIGHBOR = 0,	/**<! Default */
	FILTER_SCALE2X,					/**<! see http://scale2x.sourceforge.net/ */
	FILTER_HQX						/**<! see https://code.google.com/p/hqx/ */
} VideoScaleFilter;

extern bool Video_Init(int screen_magnification, VideoScaleFilter filter);
extern void Video_Uninit(void);
extern void Video_Tick(void);
extern void Video_SetPalette(void *palette, int from, int length);
extern void Video_Mouse_SetPosition(uint16 x, uint16 y);
extern void Video_Mouse_SetRegion(uint16 minX, uint16 maxX, uint16 minY, uint16 maxY);
extern void Video_SetOffset(uint16 offset);
extern void * Video_GetFrameBuffer(uint16 size);

/**
 * Consume the Hebrew character (if any) most recently typed via the
 * in-game Hebrew-keyboard toggle (Right-Ctrl), as a cp862 byte in the
 * font's Hebrew glyph range (0x80-0x9A -- see hebrew/tools/eng.py) ready
 * to store directly in a game string buffer. Returns 0 if nothing (or
 * nothing Hebrew) is pending. Reading clears the pending value. Not
 * implemented on every video backend (video_sdl2.c is the only one with
 * a Hebrew keyboard toggle; others always return 0), so this is safe to
 * call unconditionally.
 */
extern uint8 Video_GetHebrewTextInput(void);

/**
 * Discard any pending entries in the Hebrew-text-input queue (see
 * Video_GetHebrewTextInput()). GUI_EditBox() calls this once at the
 * start of every edit session: any keys pressed outside of an active
 * editbox (menu navigation, gameplay, etc.) still push an entry each --
 * video_sdl2.c has no notion of "is an editbox open" -- but nothing
 * pops them in that case, so they'd otherwise sit in the queue and
 * silently desync the next editbox session's pops from its own pushes
 * by however many stray keys leaked in beforehand. A no-op on video
 * backends without a Hebrew keyboard toggle.
 */
extern void Video_ClearHebrewTextInput(void);

/**
 * Whether the in-game Hebrew-keyboard toggle is currently on (see
 * Video_GetHebrewTextInput()). Not implemented on every video backend
 * (video_sdl2.c is the only one with a Hebrew keyboard toggle; others
 * always return false), so this is safe to call unconditionally.
 */
extern bool Video_IsHebrewKeyboardMode(void);

/**
 * Flip the in-game Hebrew-keyboard toggle, the same as pressing
 * Right-Ctrl -- lets a UI button drive the same toggle a key can. A
 * no-op on video backends without a Hebrew keyboard toggle.
 */
extern void Video_ToggleHebrewKeyboardMode(void);

#endif /* VIDEO_VIDEO_H */
