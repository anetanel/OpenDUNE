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
  source. Copied over as-is; **not yet wired into the build/load path** —
  OpenDUNE's intro sequence loading wasn't confirmed to reference a
  per-language WSA the way strings/graphics do. Needs investigation before
  it does anything.
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
