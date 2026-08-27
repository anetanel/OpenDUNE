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
  source, installed by `build_heb.py` as `INTRO1.WSA`. Unlike every other
  asset here, `cutscene.c`/`houseanimation.c` hardcode that filename with
  no per-language suffix at all, so it overwrites the stock English WSA in
  place and plays for every language, not just Hebrew, until someone adds
  real per-language branching there.
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
