/** @file src/audio/adl_music_win32.cpp AdLib/OPL music playback via a ported
 * Westwood .ADL interpreter (adl/sound_adlib.cpp) and OPL2/3 emulator
 * (adl/opl_dosbox.cpp, adl/opl_mame.cpp). Bypasses the MIDI/mt32mpu.c
 * pipeline entirely -- .ADL files carry their own instrument patches and
 * note sequencing, not General MIDI events. Output goes through WinMM
 * waveOut, mirroring adl_music.cpp's PulseAudio push/tick pattern: a
 * handful of buffers cycled by polling WHDR_DONE -- dsp_win32.c's
 * DSP_Play() is a fire-and-forget single buffer, the wrong shape for
 * continuous looping music.
 *
 * Genuinely single-threaded, matching dunedynasty (the fork this AdLib
 * port is from -- its AudioA5_PollMusic() polls synchronously from the
 * main/game thread) and OpenDUNE's own POSIX/SDL builds
 * (SleepAndProcessBackgroundTasks() calls Timer_InterruptRun()
 * synchronously on the calling thread). s_adlib is touched only from
 * whatever thread calls sleepIdle() -- which in this codebase is always
 * the main thread (verified: nothing else calls it) -- so ADLMusic_Play/
 * Stop/PlaySoundEffect/Tick can all just call into it directly, no
 * locking needed.
 *
 * Deliberately does NOT use Timer_Add(): on Win32 without SDL/SDL2,
 * Timer_Add() callbacks run on a separate Windows timer-queue thread
 * (see timer.c), which only fakes single-threadedness by
 * SuspendThread()-ing the main thread around each callback. An earlier
 * version of this file (branch history) put the tick there and hung
 * hard on real hardware: waveOutWrite() from inside that callback can
 * need an internal WinMM/RPC lock that the main thread was holding at
 * the exact moment it got suspended mid-call inside its own
 * waveOutOpen()/waveOutPrepareHeader() in ADLMusic_InitOutput() --
 * confirmed via a Windows minidump (main thread's stack: our own
 * SoundAdLibPC::callback() -> waveOutWrite() -> wdmaud.drv!wodMessage
 * -> blocked in ZwWaitForAlertByThreadId, a genuine contended-lock
 * wait, not just a frozen thread). WinMM tolerates concurrent calls
 * from ordinary threads fine; it does not tolerate one of those threads
 * being externally suspended mid-call. Using Timer_AddIdleHook()
 * instead (timer.c) runs the tick inline on whatever thread calls
 * sleepIdle() -- never the SuspendThread-bracketed one -- avoiding the
 * whole class of hazard rather than trying to synchronize around it.
 */

#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>

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
static const int FRAGLEN = 1024; /* samples per buffer/tick */
static const int NUM_BUFFERS = 4; /* ~93ms of slack ahead of playback */
static const uint32 TICK_INTERVAL_USEC = (uint32)(1000000LL * FRAGLEN / SRATE);

static HWAVEOUT s_waveOut = NULL;
static WAVEHDR s_waveHdr[NUM_BUFFERS];
static int16 s_waveBuf[NUM_BUFFERS][FRAGLEN];

static SoundAdLibPC *s_adlib = NULL;
static bool s_initialized = false;
static bool s_initFailed = false;
static uint32 s_lastTickTime = 0; /* Timer_GetTime(), milliseconds */

static void ADLMusic_Tick(void)
{
	int i;
	uint32 now;

	/* Timer_AddIdleHook() runs this on every sleepIdle() call, which is
	 * far more often than the ~23ms one buffer takes to drain -- only
	 * actually do the (comparatively expensive) render+write once that
	 * long has passed. */
	now = Timer_GetTime();
	if ((now - s_lastTickTime) * 1000 < TICK_INTERVAL_USEC) return;
	s_lastTickTime = now;

	if (s_adlib == NULL || s_waveOut == NULL) return;

	for (i = 0; i < NUM_BUFFERS; i++) {
		if (!(s_waveHdr[i].dwFlags & WHDR_DONE)) continue;

		SoundAdLibPC::callback(s_adlib, (SoundAdLibPC::Uint8 *)s_waveBuf[i], (int)sizeof(s_waveBuf[i]));
		waveOutWrite(s_waveOut, &s_waveHdr[i], sizeof(WAVEHDR));
	}
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
	WAVEFORMATEX waveFormat;
	MMRESULT res;
	int i;

	if (s_initialized) return true;
	if (s_initFailed) return false;

	memset(&waveFormat, 0, sizeof(waveFormat));
	waveFormat.wFormatTag      = WAVE_FORMAT_PCM;
	waveFormat.nChannels       = 1;
	waveFormat.nSamplesPerSec  = SRATE;
	waveFormat.wBitsPerSample  = 16;
	waveFormat.nBlockAlign     = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
	waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

	res = waveOutOpen(&s_waveOut, WAVE_MAPPER, &waveFormat, 0, 0, CALLBACK_NULL);
	if (res != MMSYSERR_NOERROR) goto fail;

	memset(s_waveHdr, 0, sizeof(s_waveHdr));
	memset(s_waveBuf, 0, sizeof(s_waveBuf));

	for (i = 0; i < NUM_BUFFERS; i++) {
		s_waveHdr[i].lpData         = (LPSTR)s_waveBuf[i];
		s_waveHdr[i].dwBufferLength = sizeof(s_waveBuf[i]);

		res = waveOutPrepareHeader(s_waveOut, &s_waveHdr[i], sizeof(WAVEHDR));
		if (res != MMSYSERR_NOERROR) goto fail;

		/* Queue silence up front so playback starts immediately;
		 * the tick refills with real audio as buffers drain. */
		res = waveOutWrite(s_waveOut, &s_waveHdr[i], sizeof(WAVEHDR));
		if (res != MMSYSERR_NOERROR) goto fail;
	}

	s_lastTickTime = Timer_GetTime();
	Timer_AddIdleHook(ADLMusic_Tick);

	s_initialized = true;
	return true;

fail:
	Error("ADLMusic_InitOutput() failed to set up WinMM output\n");
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
	/* Still load the file (and thus its sound-effect table) even with
	 * music off -- ADLMusic_PlaySoundEffect() (credit ticks, UI clicks)
	 * pulls from whichever .ADL file is currently loaded here, and those
	 * effects play regardless of the music setting in the original game.
	 * Only the music track itself is gated on g_gameConfig.music, mirroring
	 * Driver_Music_Play()'s guard (sound.c) for the MIDI path. */
	if (g_gameConfig.music != 0) s_adlib->playTrack((uint8)g_table_musics[musicID].index);

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
	int i;

	if (!s_initialized) return;

	Timer_RemoveIdleHook(ADLMusic_Tick);

	delete s_adlib;
	s_adlib = NULL;

	if (s_waveOut != NULL) {
		waveOutReset(s_waveOut);
		for (i = 0; i < NUM_BUFFERS; i++) waveOutUnprepareHeader(s_waveOut, &s_waveHdr[i], sizeof(WAVEHDR));
		waveOutClose(s_waveOut);
		s_waveOut = NULL;
	}

	s_initialized = false;
	s_initFailed = false;
}
