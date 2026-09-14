#!/usr/bin/env python3
"""
Package the Hebrew localization as a drop-in zip: every file
build_heb.py installs into bin/data/, and nothing else -- so it can be
unzipped directly into any existing Dune II data directory (original
PAK/CFG/etc. files already there) without overwriting a single one.
Every file here is a new, distinctly-named asset (`*.HEB`, `*h.fnt`,
Hebrew-suffixed `.CPS`/`.SHP` -> `.HEB`, `INTRO1H.WSA`, `MAPMACHH.CPS`,
`BLDINGH.VOC`/`DYNASTYH.VOC`) -- none of them share a name with an
original game file.

Deliberately excludes REGIONA.INI/REGIONH.INI/REGIONO.INI
(hebrew/tools/build_regions.py): those are generated from your own
locally-extracted, copyrighted original .INI files with new Hebrew
lines spliced in among the existing English/French/German ones, so the
output itself contains copyrighted text and isn't safe to redistribute
this way -- run that script yourself, against your own legally-owned
game files, if you also want in-mission strategic-map narration.

Runs build_heb.py first (so the zip always reflects the current
translation source), then zips exactly the STRING_JOBS/ASSET_JOBS
output filenames out of bin/data/.

Usage: python3 hebrew/tools/package_heb.py [output.zip]
Default output: hebrew/dist/dune2-hebrew.zip
"""
import subprocess
import sys
import zipfile
from pathlib import Path

HEBREW_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = HEBREW_DIR.parent
DATA_DIR = REPO_ROOT / "bin" / "data"

sys.path.insert(0, str(HEBREW_DIR / "tools"))
import build_heb  # noqa: E402

README_TEXT = """\
OpenDUNE Hebrew localization -- drop-in data files
===================================================

Copy every file in this archive into your Dune II data directory (the
same folder as DUNE.PAK, ATRE.PAK, etc. -- OpenDUNE's bin/data/ if
that's where you installed it). Nothing here shares a filename with an
original game file, so this will never overwrite anything already
there.

Then set `language=hebrew` in opendune.ini and launch the game.

Requires an OpenDUNE build with Hebrew support (this project's
`hebrew` branch) -- these files alone do nothing against a stock
OpenDUNE build or the original DOS DUNE2.EXE.

Not included: in-mission strategic-map narration (REGIONA/H/O.INI).
That file is generated from your own copy of the original game data
and isn't redistributable the same way -- see hebrew/README.md /
hebrew/tools/build_regions.py in the project source if you want it.
"""


def main():
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else HEBREW_DIR / "dist" / "dune2-hebrew.zip"

    subprocess.run([sys.executable, str(HEBREW_DIR / "tools" / "build_heb.py")],
                    check=True, cwd=REPO_ROOT)

    names = sorted(outname for outname, _builder in build_heb.STRING_JOBS.values())
    names += sorted(outname for _subdir, outname in build_heb.ASSET_JOBS.values())

    missing = [name for name in names if not (DATA_DIR / name).is_file()]
    if missing:
        print(f"missing built files (did build_heb.py fail?): {missing}")
        sys.exit(1)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        for name in names:
            z.write(DATA_DIR / name, arcname=name)
        z.writestr("README-HEBREW.txt", README_TEXT)

    print(f"wrote {out_path} ({len(names)} data files + README)")


if __name__ == "__main__":
    main()
