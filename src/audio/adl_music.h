/** @file src/audio/adl_music.h AdLib/OPL music playback (authentic Westwood
 * .ADL resource files through a ported OPL2/3 emulator), as an alternative
 * to the MIDI-based music path in mt32mpu.c/driver.c. Bypasses that pipeline
 * entirely -- see hebrew branch plan notes / commit message for why. */

#ifndef AUDIO_ADL_MUSIC_H
#define AUDIO_ADL_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/** True if "adlib=1" is set in opendune.ini (checked, and the result
 * cached, on first call). */
extern bool ADLMusic_IsEnabled(void);

/** Load DUNEn.ADL (per g_table_musics[musicID]) and start playing the
 * track within it, replacing whatever was previously playing. Lazily
 * initializes the PulseAudio output on first call. */
extern void ADLMusic_Play(uint16 musicID);

extern void ADLMusic_Stop(void);
extern bool ADLMusic_IsPlaying(void);
extern void ADLMusic_Uninit(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_ADL_MUSIC_H */
