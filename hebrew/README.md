# Hebrew language support

Adds Hebrew (`language=HEBREW` in `opendune.ini`) as a native OpenDUNE
language, alongside the existing English/French/German/Italian/Spanish.

Translation source lives here, ported over from the `hebrew-support` branch
of [dunedynasty](https://github.com/gameflorist/dunedynasty) (a fork of this
project), which already had a working Hebrew translation:

- `translations/*.json` — the source of truth for in-game strings. Edit the
  `"he"` fields here; never hand-edit the built `.HEB` files.
- `fonts/{intro,new8p,new6p}.fnt` — hand-edited Hebrew glyph sets. These are
  *derived* from the commercial Noa Shalev AlefAlefAlef font (rasterized
  elsewhere) — a licensing risk accepted for the rendered glyph shapes, but
  the raw `.otf` itself must never be committed here.
- `graphics/*.heb.{cps,shp}` — hand-edited button/title graphics, including
  `choam.heb.shp` — the "BUILD THIS"/"RESUME GAME"/"UPGRADE"/scroll-arrow
  button graphics for the Construction Yard/Starport full-screen build modal
  (`GUI_DisplayFactoryWindow`). Every officially supported language ships its
  own `CHOAM.<suffix>`, and there's no working fallback if it's missing —
  `Sprites_Load`'s fallback filename argument (`CHOAMSHP.SHP`) isn't an
  actual shipped file, so without *some* `CHOAM.HEB` those buttons silently
  fail to draw (found by testing the modal — see git history).
- `intro1.wsa` + `INTRO1-00049.{png,psd}` — flying-logo intro animation
  source, installed by `build_heb.py` as `INTRO1H.WSA` (not `INTRO1.WSA`,
  since `cutscene.c`/`houseanimation.c` hardcode that base lookup with no
  per-language suffix at all, unlike every other asset here). Instead,
  `GameLoop_PlayAnimation()` (`src/cutscene.c`) picks `INTRO1H.WSA` over
  the stock `INTRO1.WSA` only when `language == HEBREW` and the file is
  actually present, so the stock English WSA plays untouched for every
  other language.
- `audio/{BLDINGH,DYNASTYH}.VOC` — a title correction to the intro
  narration, not a translation. The US release's narration says "Dune...
  the building of a dynasty", but the EU/HitSquad release (the one this
  project's Hebrew support otherwise targets) was titled "Dune II: The
  Battle for Arrakis" — and `INTROVOC.PAK`'s narration audio turns out to
  be byte-identical across every 1.07 release, so the EU release never
  actually re-recorded it to match its own title (and so stayed silent
  over that title card rather than misplay the US's line — see git
  history for `cutscene.c`). These two files replace just the mismatched
  words ("the building of a dynasty" → "the battle for Arrakis"), spliced
  from the same narrator's voice reading other intro lines via voice
  conversion, then matched back to the original's exact format
  (14705Hz/8-bit u8) and tape hiss.

  Installed as plain loose files by `build_heb.py`'s `ASSET_JOBS`, same as
  every other asset here — `src/table/sound.c` hardcodes
  `-BLDING.VOC`/`-DYNASTY.VOC`/`-BLDINGH.VOC`/`-DYNASTYH.VOC` the same
  no-per-language-suffix way `INTRO1.WSA` is hardcoded, and the
  loose-file-overrides-PAK lookup that works for `INTRO1.WSA` works here
  too, as long as the loose file's name doesn't collide with an existing
  packed entry: an earlier version of this tool tried overriding the
  original `BLDING.VOC`/`DYNASTY.VOC` names directly and that failed
  (confirmed in-game — only a fragment of "for" played), which is why the
  correction lives under new names instead — alongside the original
  `BLDING.VOC`/`DYNASTY.VOC` inside `INTROVOC.PAK`, untouched, so both the
  pristine US narration and the corrected EU/HitSquad splice are
  available at once, with no original data file ever modified.
  `Sound_Output_Feedback()` (`src/audio/sound.c`) picks between
  them at runtime via `String_IsUSDuneRelease()` (`src/string.c`, checks
  the installed `INTRO.ENG`'s own title text): the pristine words for
  confirmed US data, the corrected splice otherwise. This plays for every
  language, not just Hebrew — English narrates this line too now, where
  the original game never did for the EU/HitSquad release — and Hebrew's
  intro subtitle for this line (`GameLoop_PlaySubtitle()`) switches to a
  "building of a dynasty" translation (`ENGINE_STR_INTRO_BUILDING_OF_A_DYNASTY`
  in `hebrew/translations/engine_strings.json`) under confirmed US data,
  so the Hebrew text and the (English) narration always agree.
- `tools/eng.py` — codec for the `.ENG`/`.HEB`-style string table format,
  reverse-engineered directly from this repo's own
  `String_DecompressAndTranslate()` (`src/string.c`) — see its docstring.
- `tools/mentat_eng.py` — codec for `MENTATA.ENG`/`MENTATH.ENG`/
  `MENTATO.ENG`: the per-house Mentat "encyclopedia" (the topic list under
  the in-game MENTAT sidebar button — Houses/Structures/Vehicles/Specials,
  each with a WSA picture and description). A completely different IFF
  FORM container from `eng.py`'s offset-table format — see its docstring.
  Ported from dunedynasty's `hebrew-support` branch, whose docstring was
  already written against this repo's own `GUI_Mentat_Draw()`/
  `GUI_Mentat_ShowHelp()` (`src/gui/mentat.c`), since that loading code is
  a faithful, unmodified part of OpenDUNE and needed no porting itself —
  only the Hebrew data pipeline was missing (see "Known gaps" below,
  formerly).
- `translations/mentata.json`/`mentath.json`/`mentato.json` — source for
  the above, decoded from this repo's own `bin/data/ENGLISH.PAK` (via
  `dunepak`) with `mentat_eng.py decode`, not yet translated (`"he"`
  fields still equal `"en"` throughout — same starting state as
  `texth`/`texto`/`message`).
- `translations/engine_strings.json` — source for `ENGINE.HEB`, an
  OpenDUNE-only file with no original-game counterpart, for text this
  repo's own C code hardcodes as a plain English string literal instead
  of looking up by `STR_*` index — `EngineStringID` in `src/string.h`
  documents why each entry exists (house names read straight out of
  `g_table_houseInfo[].name`; a couple of modal error messages that never
  got a `STR_*` slot). **Entry order must match the `EngineStringID` enum
  exactly** — it's a plain position-indexed offset table like `DUNE.HEB`,
  with no per-entry ID stored in the file itself. `EngineString_Get()`
  (`src/string.c`) only loads `ENGINE.<suffix>` if it exists for the
  active language, falling back to the caller's English literal
  otherwise, so this is safe for every language, not just Hebrew.
- `translations/regions.json` — source for the strategic-map narration
  text shown between missions (e.g. "The Atreides claimed strategic
  regions."). Lives in `REGIONA.INI`/`REGIONH.INI`/`REGIONO.INI` (one per
  starting house), plain-text `.INI` files packed inside `SCENARIO.PAK`,
  keyed per scenario group as `<LANGSUFFIX>TXT<region>` (`ENGTXT13`,
  `FRETXT13`, `GERTXT13`, ...) — see `GUI_StrategicMap_ShowProgression()`
  (`src/gui/gui.c`). Unlike every other string table here, this isn't a
  `String_Load()`-managed file at all: `Sprites_CPS_LoadRegionClick()`
  (`src/sprites.c`) reads it straight off disk via `File_ReadFile()` into
  `g_fileRegionINI`, which does go through the normal loose-file-overrides-
  PAK lookup — so, unlike `INTRO1.WSA`/`MAPMACH.CPS`, this needs no new
  filename convention or C code change, just a loose file that shadows the
  copy inside `SCENARIO.PAK`. No `HEBTXT*` keys exist in the original, so
  `Ini_GetString()` returns `NULL` for Hebrew and the affected narration
  lines were silently skipped rather than erroring — that's the bug this
  closes. `tools/region_ini.py` (the codec) + `tools/build_regions.py`
  (the entry point) are separate from `build_heb.py`/`STRING_JOBS` for the
  same reason as `build_intro1_animation.py`: building
  requires the pristine original `REGION*.INI` files as input (copyrighted,
  not committed), expected at
  `hebrew/extracted/dune2_eu_1.07/REGION{A,H,O}.INI` (extract with
  `dunepak unpak bin/data/SCENARIO.PAK .` against your own legally-owned
  copy of the game). `region_ini.py` splices new `HEBTXTn = ...` lines in
  next to each file's existing `ENGTXTn` lines without touching anything
  else byte-for-byte (French/German text included) — run
  `python3 hebrew/tools/build_regions.py` after editing this JSON, it
  writes straight to `bin/data/REGION{A,H,O}.INI`.
- `tools/build_heb.py` — run this after editing any translation JSON, then
  just relaunch the game (no recompile needed). Encodes the JSON into
  `DUNE.HEB`, `MESSAGE.HEB`, `INTRO.HEB`, `TEXTH.HEB`, `TEXTA.HEB`,
  `TEXTO.HEB`, `PROTECT.HEB`, `MENTATA.HEB`, `MENTATH.HEB`, `MENTATO.HEB`,
  `ENGINE.HEB`, and copies the font/graphics/audio assets (including
  `BLDINGH.VOC`/`DYNASTYH.VOC`) under their Hebrew-suffixed names, all
  into `bin/data/` (where `File_Init()` looks by default) — none of it
  touches an original data file. Usage: `python3 hebrew/tools/build_heb.py`,
  or name specific jobs (`dune`, `texta`, …; `list` shows valid names).
- `tools/build_intro1_animation.py` + `wsa_encode.py`/`wsa_decode.py` —
  regenerates `hebrew/intro1.wsa` from `hebrew/INTRO1-00049.png` (the
  hand-edited final frame). Ported from
  [Dune2-Heb](https://github.com/anetanel/Dune2-Heb)'s `utils/`, which is
  otherwise where this project's extraction/reconstruction tooling stays —
  this one script is the exception, copied here (not just its output)
  because regenerating the animation is a normal part of the day-to-day
  Hebrew translation workflow.

  Unlike `build_heb.py`, it needs heavier dependencies (`opencv-python`,
  `numpy`, `Pillow` — `pip install --user opencv-python-headless numpy
  pillow`) **and** the pristine, copyrighted original game files as input:
  `hebrew/extracted/dune2_eu_1.07/INTRO/INTRO1.WSA` and `INTRO.PAL`,
  extracted from your own legally-owned copy of the game (e.g. via
  `dunepak`). `hebrew/extracted/` is gitignored — never commit it. Usage:
  `python3 hebrew/tools/build_intro1_animation.py`, then run
  `build_heb.py` as usual to install the result.

## Known gaps (translated source exists, but nothing loads it — yet)

- End-game scrolling credits (`CREDITS`) — the credits scroll in
  `src/cutscene.c` doesn't load a language-suffixed strings file the way
  the rest of the UI does.

Porting this would mean building the missing feature first. Not attempted
in this pass.
