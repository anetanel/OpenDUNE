/** @file src/audio/adl_music_none.c Fallback when no AdLib output backend is
 * available (currently only PulseAudio is implemented -- see
 * adl_music.cpp). "adlib=1" in opendune.ini is simply ignored on builds
 * without PulseAudio; Music_Play() falls through to the normal MIDI path. */

#include "types.h"

#include "adl_music.h"

bool ADLMusic_IsEnabled(void)
{
	return false;
}

void ADLMusic_Play(uint16 musicID)
{
	VARIABLE_NOT_USED(musicID);
}

void ADLMusic_Stop(void)
{
}

bool ADLMusic_IsPlaying(void)
{
	return false;
}

void ADLMusic_Uninit(void)
{
}
