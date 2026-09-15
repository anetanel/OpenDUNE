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
 * EXPERIMENTAL single-threaded variant (branch: adlib-win32-singlethread).
 * dunedynasty (the fork this AdLib port comes from) drives its AdLib
 * emulator entirely from one thread: the main/game thread polls Allegro's
 * audio-stream-fragment event synchronously as part of the normal game
 * loop, so nothing ever touches the emulator concurrently. OpenDUNE's own
 * POSIX/SDL builds work the same way (SleepAndProcessBackgroundTasks()
 * calls Timer_InterruptRun() synchronously on the calling thread).
 *
 * Win32 *without* SDL/SDL2 is the exception: Timer_Add() callbacks
 * (ADLMusic_Tick() here, but also Timer_Tick() and Video_Tick() --
 * this isn't AdLib-specific) run on a Windows timer-queue thread, and
 * sleepIdle() on this config is just `msleep(1)` -- it does NOT drive
 * Timer_InterruptRun() at all (see os/sleep.h). Game timing and screen
 * redraws on this platform already depend on that background thread
 * ticking independently of the main thread, so removing it entirely
 * (to fully match dunedynasty/POSIX) would mean reworking timer.c
 * game-wide, not just this file.
 *
 * This variant keeps that constraint but still gets to "single-threaded"
 * from SoundAdLibPC's point of view: s_adlib is touched ONLY by
 * ADLMusic_Tick(), which always runs on that one shared timer-queue
 * thread (same as Timer_Tick()/Video_Tick() already do). The main-thread
 * entry points (ADLMusic_Play/Stop/PlaySoundEffect(), called from
 * sound.c's Music_Play()/Sound_Play()) never call into the AdLib object
 * directly -- they publish lock-free requests (Interlocked* pointer/value
 * swaps, single-producer/single-consumer) that the tick consumes for
 * itself before rendering each buffer. No CRITICAL_SECTION, no dedicated
 * thread of our own -- compare against master's dedicated-thread+lock
 * fix for the same bug.
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
static const LONG NO_EFFECT_PENDING = -1;

static HWAVEOUT s_waveOut = NULL;
static WAVEHDR s_waveHdr[NUM_BUFFERS];
static int16 s_waveBuf[NUM_BUFFERS][FRAGLEN];

/* Owned exclusively by ADLMusic_Tick() -- never touched from the main
 * thread. Everything below is how the main thread hands it requests. */
static SoundAdLibPC *s_adlib = NULL;

/* Published by ADLMusic_Play(), consumed (and cleared) by the tick via
 * InterlockedExchangePointer(). A fully-built, fully-initialized object
 * is handed off with a single pointer swap -- same "publish, don't leave
 * a torn intermediate state" principle as master's fix, just consumed by
 * the tick instead of applied directly to s_adlib from the main thread. */
static SoundAdLibPC *volatile s_pendingAdlib = NULL;

static volatile LONG s_pendingStop = 0;
static volatile LONG s_pendingSoundEffect = NO_EFFECT_PENDING;
static volatile LONG s_isPlayingCache = 0; /* written by the tick, read by ADLMusic_IsPlaying() */

static bool s_initialized = false;
static bool s_initFailed = false;

static void ADLMusic_Tick(void)
{
	int i;
	SoundAdLibPC *newAdlib;
	LONG stopRequested;
	LONG effectRequested;

	newAdlib = (SoundAdLibPC *)InterlockedExchangePointer((PVOID volatile *)&s_pendingAdlib, NULL);
	if (newAdlib != NULL) {
		delete s_adlib;
		s_adlib = newAdlib;
	}

	stopRequested = InterlockedExchange(&s_pendingStop, 0);
	if (stopRequested != 0 && s_adlib != NULL) s_adlib->haltTrack();

	effectRequested = InterlockedExchange(&s_pendingSoundEffect, NO_EFFECT_PENDING);
	if (effectRequested != NO_EFFECT_PENDING && s_adlib != NULL) s_adlib->playSoundEffect((uint8_t)effectRequested);

	s_isPlayingCache = (s_adlib != NULL && s_adlib->isPlaying()) ? 1 : 0;

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

	Timer_Add(ADLMusic_Tick, (uint32)(1000000LL * FRAGLEN / SRATE), false);

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
	SoundAdLibPC *newAdlib;
	SoundAdLibPC *unconsumed;

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

	/* Fully build off to the side -- nothing else can see newAdlib until
	 * the InterlockedExchangePointer() below publishes it, so none of
	 * this needs any synchronization. */
	newAdlib = new SoundAdLibPC(data, size, SRATE, true);
	newAdlib->init();
	/* Still load the file (and thus its sound-effect table) even with
	 * music off -- ADLMusic_PlaySoundEffect() (credit ticks, UI clicks)
	 * pulls from whichever .ADL file is currently loaded here, and those
	 * effects play regardless of the music setting in the original game.
	 * Only the music track itself is gated on g_gameConfig.music, mirroring
	 * Driver_Music_Play()'s guard (sound.c) for the MIDI path. */
	if (g_gameConfig.music != 0) newAdlib->playTrack((uint8)g_table_musics[musicID].index);

	free(data);

	/* Hand off to the tick -- it's the only code that ever touches
	 * s_adlib. If a previous request hadn't been consumed yet (the tick
	 * runs at ~43Hz, so this would need two ADLMusic_Play() calls within
	 * one tick interval), free it here rather than leak it. */
	unconsumed = (SoundAdLibPC *)InterlockedExchangePointer((PVOID volatile *)&s_pendingAdlib, newAdlib);
	delete unconsumed;
}

void ADLMusic_Stop(void)
{
	InterlockedExchange(&s_pendingStop, 1);
}

void ADLMusic_PlaySoundEffect(uint16 index)
{
	if (index >= 120) return;

	InterlockedExchange(&s_pendingSoundEffect, (LONG)index);
}

bool ADLMusic_IsPlaying(void)
{
	return s_isPlayingCache != 0;
}

void ADLMusic_Uninit(void)
{
	int i;
	SoundAdLibPC *unconsumed;

	if (!s_initialized) return;

	/* Timer_Remove() itself has a pre-existing, general race in timer.c
	 * against a concurrently in-flight Timer_InterruptRun() on this
	 * platform (it mutates the shared node array with no synchronization
	 * of its own) -- not introduced by this file and not attempted to be
	 * fixed here, since it'd mean touching the shared timer subsystem
	 * game-wide. In practice this only runs once, at shutdown. */
	Timer_Remove(ADLMusic_Tick);

	unconsumed = (SoundAdLibPC *)InterlockedExchangePointer((PVOID volatile *)&s_pendingAdlib, NULL);
	delete unconsumed;
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
