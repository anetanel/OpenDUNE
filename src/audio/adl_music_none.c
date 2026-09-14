/** @file src/audio/adl_music_none.c Fallback when no AdLib output backend is
 * available (currently PulseAudio, Windows/WinMM and macOS/CoreAudio are
 * implemented -- see adl_music.cpp / adl_music_win32.cpp /
 * adl_music_osx.cpp). "adlib=1" in opendune.ini is simply ignored on every
 * other build; Music_Play() falls through to the normal MIDI path. */

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

void ADLMusic_PlaySoundEffect(uint16 index)
{
	VARIABLE_NOT_USED(index);
}

bool ADLMusic_IsPlaying(void)
{
	return false;
}

void ADLMusic_Uninit(void)
{
}
