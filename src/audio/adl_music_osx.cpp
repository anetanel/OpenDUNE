/** @file src/audio/adl_music_osx.cpp AdLib/OPL music playback via a ported
 * Westwood .ADL interpreter (adl/sound_adlib.cpp) and OPL2/3 emulator
 * (adl/opl_dosbox.cpp, adl/opl_mame.cpp). Bypasses the MIDI/mt32mpu.c
 * pipeline entirely -- .ADL files carry their own instrument patches and
 * note sequencing, not General MIDI events. Output goes through the same
 * CoreAudio AudioUnit API dsp_osx.c uses for VOC playback, but that file's
 * DSP_Play() hands the render callback one fixed pre-decoded buffer to
 * drain -- the wrong shape for continuously-generated streaming music.
 * Here the render callback pulls fresh samples from the OPL emulator on
 * every invocation instead, which fits this API's pull model more
 * naturally than the push/tick pattern the PulseAudio/WinMM backends need.
 */

#if defined(__ALTIVEC__) && !defined(MAC_OS_X_VERSION_10_5)
/* to circumvent a bug in Mac OS X 10.4 SDK */
#define vector __vector
#include <CoreServices/CoreServices.h>
#undef vector
#endif
#include <AudioUnit/AudioUnit.h>

#include <stdlib.h>
#include <string.h>

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

static AudioUnit s_outputAudioUnit;
static bool s_started = false;

static SoundAdLibPC *s_adlib = NULL;
static bool s_initialized = false;
static bool s_initFailed = false;

static OSStatus ADLMusic_RenderCallback(void *inRefCon,
                                         AudioUnitRenderActionFlags *ioActionFlags,
                                         const AudioTimeStamp *inTimeStamp,
                                         UInt32 inBusNumber,
                                         UInt32 inNumberFrames,
                                         AudioBufferList *ioData)
{
	UInt32 i;

	VARIABLE_NOT_USED(inRefCon);
	VARIABLE_NOT_USED(ioActionFlags);
	VARIABLE_NOT_USED(inTimeStamp);
	VARIABLE_NOT_USED(inBusNumber);
	VARIABLE_NOT_USED(inNumberFrames);

	for (i = 0; i < ioData->mNumberBuffers; i++) {
		AudioBuffer *buf = &ioData->mBuffers[i];

		if (i == 0 && s_adlib != NULL) {
			SoundAdLibPC::callback(s_adlib, (SoundAdLibPC::Uint8 *)buf->mData, (int)buf->mDataByteSize);
		} else {
			memset(buf->mData, 0, buf->mDataByteSize);
		}
	}

	return noErr;
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
	AudioStreamBasicDescription format;
	Component comp;
	ComponentDescription desc;
	struct AURenderCallbackStruct callback;
	OSStatus result;

	if (s_initialized) return true;
	if (s_initFailed) return false;

	desc.componentType = kAudioUnitType_Output;
	desc.componentSubType = kAudioUnitSubType_DefaultOutput;
	desc.componentManufacturer = kAudioUnitManufacturer_Apple;
	desc.componentFlags = 0;
	desc.componentFlagsMask = 0;

	comp = FindNextComponent(NULL, &desc);
	if (comp == NULL) goto fail;

	result = OpenAComponent(comp, &s_outputAudioUnit);
	if (result != noErr) goto fail;

	result = AudioUnitInitialize(s_outputAudioUnit);
	if (result != noErr) goto fail;

	memset(&format, 0, sizeof(format));
	format.mChannelsPerFrame = 1;
	format.mFormatID = kAudioFormatLinearPCM;
	format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
	format.mBitsPerChannel = 16;
	format.mFramesPerPacket = 1;
	format.mBytesPerFrame = format.mBitsPerChannel * format.mChannelsPerFrame / 8;
	format.mBytesPerPacket = format.mBytesPerFrame * format.mFramesPerPacket;
	format.mSampleRate = SRATE;

	result = AudioUnitSetProperty(s_outputAudioUnit,
			kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
			0, &format, sizeof(format));
	if (result != noErr) goto fail;

	callback.inputProc = ADLMusic_RenderCallback;
	callback.inputProcRefCon = NULL;
	result = AudioUnitSetProperty(s_outputAudioUnit,
			kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
			0, &callback, sizeof(callback));
	if (result != noErr) goto fail;

	s_initialized = true;
	return true;

fail:
	Error("ADLMusic_InitOutput() failed to set up CoreAudio output\n");
	s_initFailed = true;
	return false;
}

void ADLMusic_Play(uint16 musicID)
{
	char filename[16];
	uint32 size;
	uint8 *data;
	SoundAdLibPC *adlib;
	SoundAdLibPC *old;

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

	adlib = new SoundAdLibPC(data, size, SRATE, true);
	adlib->init();
	/* Still load the file (and thus its sound-effect table) even with
	 * music off -- ADLMusic_PlaySoundEffect() (credit ticks, UI clicks)
	 * pulls from whichever .ADL file is currently loaded here, and those
	 * effects play regardless of the music setting in the original game.
	 * Only the music track itself is gated on g_gameConfig.music, mirroring
	 * Driver_Music_Play()'s guard (sound.c) for the MIDI path. */
	if (g_gameConfig.music != 0) adlib->playTrack((uint8)g_table_musics[musicID].index);

	/* The render callback runs on CoreAudio's realtime thread and reads
	 * s_adlib on every invocation, so swap the pointer only after the new
	 * object is fully ready rather than deleting the old one first (unlike
	 * the Pulse/WinMM backends, which only ever touch s_adlib from the
	 * single tick-driven thread they themselves control). */
	old = s_adlib;
	s_adlib = adlib;
	delete old;

	free(data);

	if (!s_started) {
		if (AudioOutputUnitStart(s_outputAudioUnit) == noErr) s_started = true;
	}
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
	SoundAdLibPC *old;

	if (s_started) {
		AudioOutputUnitStop(s_outputAudioUnit);
		s_started = false;
	}

	old = s_adlib;
	s_adlib = NULL;
	delete old;

	if (s_initialized) {
		struct AURenderCallbackStruct callback;

		callback.inputProc = 0;
		callback.inputProcRefCon = 0;
		AudioUnitSetProperty(s_outputAudioUnit,
				kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
				0, &callback, sizeof(callback));

		CloseComponent(s_outputAudioUnit);
	}

	s_initialized = false;
	s_initFailed = false;
}
