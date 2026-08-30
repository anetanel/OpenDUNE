/** @file src/audio/adl_music.cpp AdLib/OPL music playback via a ported
 * Westwood .ADL interpreter (adl/sound_adlib.cpp) and OPL2/3 emulator
 * (adl/opl_dosbox.cpp, adl/opl_mame.cpp). Bypasses the MIDI/mt32mpu.c
 * pipeline entirely -- .ADL files carry their own instrument patches and
 * note sequencing, not General MIDI events. Output goes through its own
 * PulseAudio stream, mirroring dsp_pulse.c's push/Timer_Add-tick pattern
 * (that file's DSP_Play() is VOC-clip-shaped, wrong fit for continuous
 * looping music).
 *
 * Scoped to PulseAudio only for now -- other dsp_*.c backends (SDL/ALSA/
 * OSS) aren't covered here.
 */

#include <stdlib.h>
#include <string.h>
#define inline __inline
#include <pulse/pulseaudio.h>

extern "C" {
#include "types.h"
#include "../config.h"
#include "../os/error.h"
#include "../file.h"
#include "../inifile.h"
#include "../timer.h"
#include "sound.h"
}

#include "adl/sound_adlib.h"
#include "adl_music.h"

static const int SRATE = 44100;
static const int FRAGLEN = 1024; /* samples per tick */

static pa_mainloop *s_mainloop = NULL;
static pa_mainloop_api *s_mainloop_api = NULL;
static pa_context *s_context = NULL;
static pa_stream *s_stream = NULL;

static SoundAdLibPC *s_adlib = NULL;
static bool s_initialized = false;
static bool s_initFailed = false;

static void ADLMusic_Tick(void)
{
	int retval;
	int16 buffer[FRAGLEN];

	pa_mainloop_iterate(s_mainloop, 0, &retval);

	if (s_adlib == NULL || s_stream == NULL) return;
	if (pa_stream_get_state(s_stream) != PA_STREAM_READY) return;

	SoundAdLibPC::callback(s_adlib, (SoundAdLibPC::Uint8 *)buffer, (int)sizeof(buffer));
	pa_stream_write(s_stream, buffer, sizeof(buffer), NULL, 0, PA_SEEK_RELATIVE);
}

bool ADLMusic_IsEnabled(void)
{
	static int s_checked = 0;
	static bool s_enabled = false;

	if (!s_checked) {
		s_enabled = (IniFile_GetInteger("adlib", 0) != 0);
		s_checked = 1;
	}

	return s_enabled;
}

static bool ADLMusic_InitOutput(void)
{
	pa_context_state_t state;
	pa_sample_spec sample_spec;
	int retval;

	if (s_initialized) return true;
	if (s_initFailed) return false;

	s_mainloop = pa_mainloop_new();
	if (s_mainloop == NULL) goto fail;
	s_mainloop_api = pa_mainloop_get_api(s_mainloop);

	s_context = pa_context_new_with_proplist(s_mainloop_api, "OpenDUNE AdLib", NULL);
	if (s_context == NULL) goto fail;
	if (pa_context_connect(s_context, NULL, PA_CONTEXT_NOFLAGS, NULL) != 0) goto fail;

	do {
		pa_mainloop_iterate(s_mainloop, 1, &retval);
		state = pa_context_get_state(s_context);
		if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) goto fail;
	} while (state != PA_CONTEXT_READY);

	sample_spec.format = PA_SAMPLE_S16LE;
	sample_spec.rate = SRATE;
	sample_spec.channels = 1;
	s_stream = pa_stream_new(s_context, "OpenDUNE AdLib Music", &sample_spec, NULL);
	if (s_stream == NULL) goto fail;
	if (pa_stream_connect_playback(s_stream, NULL, NULL, PA_STREAM_START_UNMUTED, NULL, NULL) < 0) goto fail;

	Timer_Add(ADLMusic_Tick, (uint32)(1000000LL * FRAGLEN / SRATE), false);

	s_initialized = true;
	return true;

fail:
	Error("ADLMusic_InitOutput() failed to set up PulseAudio output\n");
	s_initFailed = true;
	return false;
}

void ADLMusic_Play(uint16 musicID)
{
	char filename[16];
	uint32 size;
	uint8 *data;

	if (musicID >= 38 || g_table_musics[musicID].string == NULL) {
		ADLMusic_Stop();
		return;
	}
	/* Mirror Driver_Music_Play()'s "music off" guard (sound.c) -- without
	 * this, disabling music has no effect on the AdLib path. */
	if (g_gameConfig.music == 0) return;

	if (!ADLMusic_InitOutput()) return;

	snprintf(filename, sizeof(filename), "%s.ADL", g_table_musics[musicID].string);

	if (!File_Exists_GetSize(filename, &size)) {
		Warning("ADLMusic_Play(): %s not found\n", filename);
		return;
	}

	data = (uint8 *)malloc(size);
	if (data == NULL) return;
	File_ReadBlockFile(filename, data, size);

	delete s_adlib;
	s_adlib = new SoundAdLibPC(data, size, SRATE, true);
	s_adlib->init();
	s_adlib->playTrack((uint8)g_table_musics[musicID].index);

	free(data);
}

void ADLMusic_Stop(void)
{
	if (s_adlib != NULL) s_adlib->haltTrack();
}

void ADLMusic_PlaySoundEffect(uint16 index)
{
	if (s_adlib == NULL || index >= 120) return;

	s_adlib->playSoundEffect((uint8_t)index);
}

bool ADLMusic_IsPlaying(void)
{
	return s_adlib != NULL && s_adlib->isPlaying();
}

void ADLMusic_Uninit(void)
{
	if (s_initialized) Timer_Remove(ADLMusic_Tick);

	delete s_adlib;
	s_adlib = NULL;

	if (s_stream != NULL) {
		pa_stream_disconnect(s_stream);
		pa_stream_unref(s_stream);
		s_stream = NULL;
	}
	if (s_context != NULL) {
		pa_context_unref(s_context);
		s_context = NULL;
	}
	if (s_mainloop != NULL) {
		int retval;
		pa_mainloop_quit(s_mainloop, 0);
		pa_mainloop_run(s_mainloop, &retval);
		pa_mainloop_free(s_mainloop);
		s_mainloop = NULL;
	}

	s_initialized = false;
	s_initFailed = false;
}
