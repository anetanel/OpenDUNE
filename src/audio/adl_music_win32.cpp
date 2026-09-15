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
 * The tick runs on our own dedicated thread rather than through the
 * shared Timer_Add() mechanism. On Win32 without SDL/SDL2 (how the
 * release win32/win64 builds are configured), timer.c's tick callbacks
 * run on a Windows timer-queue thread that only fakes single-threadedness
 * by SuspendThread()-ing the main thread for the callback's duration --
 * that can land mid-instruction, not just at C-statement boundaries. A
 * plain CRITICAL_SECTION shared with that mechanism is actually a
 * deadlock trap: if the main thread is suspended while holding the lock,
 * the timer thread's own attempt to acquire it inside the tick blocks
 * forever (it can't be released until the tick returns, which can't
 * happen until the lock is acquired). Our own thread is never the one
 * SuspendThread() targets, so a normal critical section between it and
 * the main thread is safe. (An earlier version of this file put the tick
 * on Timer_Add and raced s_adlib against a delete-then-reassign in
 * ADLMusic_Play() -- fixed first, but the deeper internal-state race
 * against ADLMusic_Stop()/ADLMusic_PlaySoundEffect() needed this.)
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
#include "sound.h"
}

#include "adl/sound_adlib.h"
#include "adl_music.h"

static const int SRATE = 44100;
static const int FRAGLEN = 1024; /* samples per buffer/tick */
static const int NUM_BUFFERS = 4; /* ~93ms of slack ahead of playback */
static const DWORD TICK_POLL_MS = 10; /* well under one buffer's ~23ms drain time */

static HWAVEOUT s_waveOut = NULL;
static WAVEHDR s_waveHdr[NUM_BUFFERS];
static int16 s_waveBuf[NUM_BUFFERS][FRAGLEN];

static SoundAdLibPC *s_adlib = NULL;
static bool s_initialized = false;
static bool s_initFailed = false;

static HANDLE s_audioThread = NULL;
static HANDLE s_stopEvent = NULL;

/* Lives for the whole process (constructed before main() runs), unlike
 * s_adlib/s_waveOut/etc which come and go across Init/Uninit cycles --
 * ADLMusic_Stop()/ADLMusic_PlaySoundEffect() can be reached (via
 * Music_Play()/Sound_Play() in sound.c) before ADLMusic_Play() has ever
 * run ADLMusic_InitOutput(), so a lock lazily created there would still
 * be uninitialized on that first call. Entering an uninitialized
 * CRITICAL_SECTION is undefined behavior -- this is what actually
 * produced the C0000005 inside RtlEnterCriticalSection. */
class Win32Lock {
public:
	Win32Lock() { InitializeCriticalSection(&cs); }
	~Win32Lock() { DeleteCriticalSection(&cs); }
	CRITICAL_SECTION cs;
};
static Win32Lock s_lock;

class ScopedLock {
public:
	ScopedLock(CRITICAL_SECTION &cs) : m_cs(cs) { EnterCriticalSection(&m_cs); }
	~ScopedLock() { LeaveCriticalSection(&m_cs); }
private:
	CRITICAL_SECTION &m_cs;
};

/* Caller must hold s_lock. */
static void ADLMusic_TickLocked(void)
{
	int i;

	if (s_adlib == NULL || s_waveOut == NULL) return;

	for (i = 0; i < NUM_BUFFERS; i++) {
		if (!(s_waveHdr[i].dwFlags & WHDR_DONE)) continue;

		SoundAdLibPC::callback(s_adlib, (SoundAdLibPC::Uint8 *)s_waveBuf[i], (int)sizeof(s_waveBuf[i]));
		waveOutWrite(s_waveOut, &s_waveHdr[i], sizeof(WAVEHDR));
	}
}

static DWORD WINAPI ADLMusic_ThreadProc(LPVOID param)
{
	VARIABLE_NOT_USED(param);

	for (;;) {
		if (WaitForSingleObject(s_stopEvent, TICK_POLL_MS) == WAIT_OBJECT_0) break;

		ScopedLock guard(s_lock.cs);
		ADLMusic_TickLocked();
	}

	return 0;
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
		 * the tick thread refills with real audio as buffers drain. */
		res = waveOutWrite(s_waveOut, &s_waveHdr[i], sizeof(WAVEHDR));
		if (res != MMSYSERR_NOERROR) goto fail;
	}

	s_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (s_stopEvent == NULL) goto fail;

	s_audioThread = CreateThread(NULL, 0, ADLMusic_ThreadProc, NULL, 0, NULL);
	if (s_audioThread == NULL) goto fail;

	s_initialized = true;
	return true;

fail:
	Error("ADLMusic_InitOutput() failed to set up WinMM output\n");
	if (s_stopEvent != NULL) { CloseHandle(s_stopEvent); s_stopEvent = NULL; }
	s_initFailed = true;
	return false;
}

void ADLMusic_Play(uint16 musicID)
{
	char filename[16];
	uint32 size;
	uint8 *data;
	SoundAdLibPC *newAdlib;
	SoundAdLibPC *oldAdlib;

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

	/* Build/initialize off to the side -- newAdlib isn't shared yet, so
	 * none of this needs the lock. */
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

	{
		ScopedLock guard(s_lock.cs);
		oldAdlib = s_adlib;
		s_adlib = newAdlib;
	}
	delete oldAdlib;
}

void ADLMusic_Stop(void)
{
	ScopedLock guard(s_lock.cs);
	if (s_adlib != NULL) s_adlib->haltTrack();
}

void ADLMusic_PlaySoundEffect(uint16 index)
{
	ScopedLock guard(s_lock.cs);
	if (s_adlib == NULL || index >= 120) return;

	s_adlib->playSoundEffect((uint8_t)index);
}

bool ADLMusic_IsPlaying(void)
{
	ScopedLock guard(s_lock.cs);
	return s_adlib != NULL && s_adlib->isPlaying();
}

void ADLMusic_Uninit(void)
{
	int i;
	SoundAdLibPC *oldAdlib;

	if (!s_initialized) return;

	/* Stop the tick thread first, and join it outside the lock -- it
	 * only ever needs s_lock for one ADLMusic_TickLocked() call at a
	 * time, so it will see the signaled event and exit within one
	 * TICK_POLL_MS regardless of what this thread is doing. Taking
	 * s_lock here before joining would risk exactly the kind of
	 * lock-vs-suspend deadlock this design was written to avoid. */
	if (s_audioThread != NULL) {
		SetEvent(s_stopEvent);
		WaitForSingleObject(s_audioThread, INFINITE);
		CloseHandle(s_audioThread);
		s_audioThread = NULL;
	}
	if (s_stopEvent != NULL) {
		CloseHandle(s_stopEvent);
		s_stopEvent = NULL;
	}

	oldAdlib = s_adlib;
	s_adlib = NULL;
	delete oldAdlib;

	if (s_waveOut != NULL) {
		waveOutReset(s_waveOut);
		for (i = 0; i < NUM_BUFFERS; i++) waveOutUnprepareHeader(s_waveOut, &s_waveHdr[i], sizeof(WAVEHDR));
		waveOutClose(s_waveOut);
		s_waveOut = NULL;
	}

	s_initialized = false;
	s_initFailed = false;
}
