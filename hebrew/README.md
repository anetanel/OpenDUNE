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
- `graphics/*.heb.{cps,shp}` — hand-edited button/title graphics.
- `graphics/choam.eng-fallback.cps` — **not translated**, a byte-for-byte
  copy of the original English `CHOAM.ENG`. It holds the "BUILD THIS"/
  "RESUME GAME"/"UPGRADE"/scroll-arrow button graphics for the Construction
  Yard/Starport full-screen build modal (`GUI_DisplayFactoryWindow`).
  Every officially supported language ships its own `CHOAM.<suffix>`, and
  there's no working fallback if it's missing — `Sprites_Load`'s fallback
  filename argument (`CHOAMSHP.SHP`) isn't an actual shipped file, so
  without *some* `CHOAM.HEB` those buttons silently fail to draw (found by
  testing the modal — see git history). Someone should draw a proper
  Hebrew-labeled version eventually; this keeps the buttons functional
  until then.
- `intro1.wsa` + `INTRO1-00049.{png,psd}` — flying-logo intro animation
  source, installed by `build_heb.py` as `INTRO1H.WSA` (not `INTRO1.WSA`,
  since `cutscene.c`/`houseanimation.c` hardcode that base lookup with no
  per-language suffix at all, unlike every other asset here). Instead,
  `GameLoop_PlayAnimation()` (`src/cutscene.c`) picks `INTRO1H.WSA` over
  the stock `INTRO1.WSA` only when `language == HEBREW` and the file is
  actually present, so the stock English WSA plays untouched for every
  other language.
- `audio/{BLDING,DYNASTY}.VOC` — a title correction to the intro
  narration, not a translation. The US release's narration says "Dune...
  the building of a dynasty", but the EU/HitSquad release (the one this
  project's Hebrew support otherwise targets) was titled "Dune II: The
  Battle for Arrakis" — and `INTROVOC.PAK`'s narration audio turns out to
  be byte-identical across every 1.07 release, so the EU release never
  actually re-recorded it to match its own title. These two files replace
  just the mismatched words ("the building of a dynasty" → "the battle
  for Arrakis"), spliced from the same narrator's voice reading other
  intro lines via voice conversion, then matched back to the original's
  exact format (14705Hz/8-bit u8) and tape hiss.

  Unlike every other asset here, these aren't installed as loose files —
  `src/table/sound.c` hardcodes `-BLDING.VOC`/`-DYNASTY.VOC` the same
  no-per-language-suffix way `INTRO1.WSA` is hardcoded, but the
  loose-file-overrides-PAK lookup that works for `INTRO1.WSA` turned out
  not to apply to VOC playback (confirmed in-game — only a fragment of
  "for" played), so `hebrew/tools/pack_introvoc.py` patches them directly
  into a copy of `INTROVOC.PAK` instead. They play for every language,
  not just Hebrew (relevant here since Hebrew plays the English narration
  under its own subtitles rather than going silent, see git history for
  `cutscene.c`).
- `tools/eng.py` — codec for the `.ENG`/`.HEB`-style string table format,
  reverse-engineered directly from this repo's own
  `String_DecompressAndTranslate()` (`src/string.c`) — see its docstring.
- `tools/build_heb.py` — run this after editing any translation JSON, then
  just relaunch the game (no recompile needed). Encodes the JSON into
  `DUNE.HEB`, `MESSAGE.HEB`, `INTRO.HEB`, `TEXTH.HEB`, `TEXTA.HEB`,
  `TEXTO.HEB`, `PROTECT.HEB`, and copies the font/graphics assets under
  their Hebrew-suffixed names, all into `bin/data/` (where `File_Init()`
  looks by default). Usage: `python3 hebrew/tools/build_heb.py`, or name
  specific jobs (`dune`, `texta`, …; `list` shows valid names).
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
- `tools/pack_introvoc.py` — patches `audio/{BLDING,DYNASTY}.VOC` (see
  above) directly into a copy of `INTROVOC.PAK`, written to `bin/data/`.
  A separate script from `build_heb.py` for the same reason as
  `build_intro1_animation.py`: it needs the pristine, copyrighted
  original `INTROVOC.PAK` as input, expected at
  `hebrew/extracted/dune2_eu_1.07/INTROVOC.PAK` (US 1.0/1.07, EU 1.07,
  and HitSquad 1.07 are all byte-identical, so any of those works — copy
  it in from your own legally-owned copy of the game). Doesn't need the
  heavier `build_intro1_animation.py` dependencies, just stdlib. Usage:
  `python3 hebrew/tools/pack_introvoc.py`.

## Known gaps (translated source exists, but nothing loads it — yet)

dunedynasty has three more translation categories with no equivalent
loading code anywhere in this repo (confirmed by grep — zero hits):

- Mentat per-house advice database (`MENTATH`/`MENTATA`/`MENTATO` files) —
  this subsystem isn't implemented in OpenDUNE at all.
- End-game scrolling credits (`CREDITS`) — the credits scroll in
  `src/cutscene.c` doesn't load a language-suffixed strings file the way
  the rest of the UI does.
- `engine_strings.json`-equivalent — dunedynasty's own mechanism for UI
  strings *it* added with no original PAK slot; doesn't apply here.

Porting these would mean building the missing feature first. Not attempted
in this pass.
