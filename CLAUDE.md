# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

OpenDUNE is an open-source re-implementation of Dune II (Westwood Studios /
Virgin Entertainment, 1992), written in ANSI C (C89). It requires the
original Dune II 1.07 game data files (`.PAK` archives etc.) placed in
`bin/data/` (or `data/`) to actually run — this repo ships no copyrighted
game assets. The current branch (`hebrew`) additionally carries an
in-progress Hebrew localization under `hebrew/` (see below).

## Build

The build system is a hand-rolled `./configure` + generated `Makefile`
(not GNU autoconf, but similar usage):

```
./configure              # detects SDL/SDL2, ALSA/OSS/PulseAudio, etc.
make -j$(nproc)           # builds bin/opendune
```

- `./configure --help` lists all `--with-*`/`--enable-*` flags (audio
  backends, SDL1 vs SDL2, MT32/FluidSynth support, debug level, LTO, etc.).
- `make` auto-reconfigures with the last-used flags when it detects
  `source.list` or any configure input changed; `./configure --reconfig`
  does this explicitly.
- `make mrproper` (or `make distclean`) wipes all configure+build output
  (`objs/`, `Makefile*`, `config.*`) — needed before switching major
  configure options.
- `make run` / `make run-gdb` / `make run-valgrind` build then launch the
  game from `bin/`.
- `make bundle_zip` (also `bundle_gzip`/`bundle_bzip2`/`bundle_dmg`)
  produces a distributable package; this is what CI
  (`.github/workflows/builds.yml`) runs after `make`.
- If `bin/opendune` is currently running, relinking fails with "Text file
  busy" — close the running game first.

There is **no automated test suite**. Verify changes by building and
running the game (`make run`) against real Dune II 1.07 data files; UI
changes especially need visual confirmation in-game, not just a clean
compile.

## Data loading and localization

- `src/file.c` (`File_Init()`) implements a loose-file-overrides-PAK
  lookup across `SEARCHDIR_*` locations: a file loose in the data dir
  takes priority over the same-named entry packed inside the original
  `.PAK` archives. This is how asset overrides/localization work without
  touching the original archives — but it does **not** apply to every
  file type (VOC audio playback ignores loose-file overrides and must be
  patched directly into its `.PAK`; see `hebrew/README.md`).
- Language selection (`g_config.language`, `LANGUAGE_*` in `src/string.h`)
  drives `String_GenerateFilename()` (`src/string.c`), which appends a
  language suffix (`ENG`/`FRE`/`GER`/`ITA`/`SPA`/`HEB`) to string-table
  and graphics filenames, and `Font_Init()` (`src/gui/font.c`), which
  looks for lowercase-suffixed font files (e.g. `new6ph.fnt` for Hebrew).
  A few assets don't follow this convention and are hardcoded with no
  per-language suffix at all (e.g. the intro's `INTRO1.WSA` in
  `src/cutscene.c`) — these need explicit
  `g_config.language == LANGUAGE_X` branching for a language-specific
  variant, since dropping in a suffixed file alone does nothing.
- `hebrew/` is a self-contained localization subproject (translation JSON
  source of truth, hand-edited fonts/graphics, Python build tooling under
  `hebrew/tools/`) that regenerates and installs `bin/data/*.HEB`-style
  files without needing a recompile for text changes. `hebrew/README.md`
  is the authoritative doc for that pipeline and lists known gaps.
- RTL (Hebrew) text is mirrored at draw time, not baked into the string
  tables (`GUI_MirrorRTLText()` / `GUI_IsRTLLanguage()`, `src/gui/gui.c`).
  For a left-anchored *multi-line* text box, use
  `GUI_DrawText_WrapperBox()` (takes a real pixel box width) rather than
  `GUI_DrawText_Wrapper()`'s single-anchor align-right flag — the latter
  only shifts once by the whole string's width and silently breaks (falls
  back to flush-left) on wrapped multi-line text.

## Code architecture

- **Game loop**: `src/opendune.c`'s `GameLoop_Main()` is the top-level
  state machine (menu → house select → briefing → mission → win/lose →
  strategic map → ...). Cutscenes/animated sequences (intro, house
  animations, mission briefings/endings) are driven by `src/cutscene.c`
  playing WSA animations against table data.
- **Table-driven content**: almost all game data — structure/unit stats,
  house info, cutscene/animation sequences and their subtitles, UI widget
  geometry/colors, sound effects, tile diffs, AI action tables — lives as
  `static const` arrays in `src/table/*.c`, indexed by enums. Game logic
  in `src/*.c` reads these tables; when changing stats, layouts, or
  sequences, look here first rather than in the logic files.
- **GUI/widgets**: `src/gui/widget*.c` implements a generic linked-list
  `Widget` system; layout/color comes from `WidgetInfo`/`WidgetProperties`
  tables (`src/table/widgetinfo.c`, `g_widgetProperties[]` in
  `src/gui/widget.c`). Text drawing goes through `GUI_DrawText()` /
  `GUI_DrawText_Wrapper()` (`src/gui/gui.c`), which most call sites drive
  with a packed `flags` bitfield (font, color style, alignment) documented
  in the comment above `GUI_DrawText_Wrapper()`. `src/gui/mentat.c` +
  `src/gui/security.c` share the same scrolling-dialogue-box renderer for
  the mentat advisor and security/password screens.
- **Object AI/scripting**: `src/script/*.c` is a small bytecode-style
  interpreter that drives per-structure/unit/team AI each tick, called
  from `src/structure.c`/`src/unit.c`/`src/team.c`. Separate from
  `src/saveload/*.c`, which serializes live game state, and `src/save.c`,
  which is the top-level save-file format/versioning.
- **Platform abstraction, chosen at configure time (not runtime)**:
  `src/os/*` (endian, threading, error reporting, per-OS directory
  listing), `src/audio/*` (selectable backends — ALSA/OSS/PulseAudio/
  KAI/SDL, MIDI via native/MT32(munt)/FluidSynth), `src/video/*` (SDL1/SDL2
  backends, plus software scalers hq2x/hq3x/hq4x/scale2x/scale3x).
- **Music: MIDI vs AdLib**: the default music path is a General-MIDI-style
  XMIDI sequencer (`.C55` files) routed to a real MIDI backend (ALSA
  seq/FluidSynth/MT-32/etc.) — this is what the `src/audio/midi_*.c`
  backends and `mt32midi`/`fs_soundfont` ini options tune. Alongside it,
  `src/audio/adl/` is a ported, self-contained C++ OPL2/3 chip emulator
  plus a bytecode interpreter for Westwood's original `.ADL` resource
  files (their own instrument patches/sequencing, not General MIDI),
  giving authentic 1992 AdLib-sounding music instead of a GM soundfont.
  `src/audio/adl_music.cpp` is the glue that owns its own PulseAudio
  output stream and reuses the existing `g_table_musics[]` track table
  unchanged (same base filename/index as the MIDI source, just a `.ADL`
  extension). `Music_Play()` (`src/audio/sound.c`) branches to this path
  when `ADLMusic_IsEnabled()` is true, gated by the `adlib` ini option;
  sound effects always stay on the MIDI/DSP path. The AdLib sources are
  unconditionally compiled but only wired to real output when PulseAudio
  support is configured in — `src/audio/adl_music_none.c` is the no-op
  fallback otherwise (see `source.list`'s `#if PULSE` split), so `adlib=1`
  silently does nothing on non-PulseAudio builds rather than erroring.
