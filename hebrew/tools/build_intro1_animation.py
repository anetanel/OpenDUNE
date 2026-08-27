#!/usr/bin/env python3
"""Reconstruct the 50-frame INTRO1.WSA "flying logo" animation in Hebrew from
a single hand-edited final frame (hebrew/INTRO1-00049.png).

The original animation isn't hand-drawn per frame: it's one flat logo under a
per-frame 2D perspective transform (the logo receding into/flying out of the
screen). This script recovers that per-frame transform from the *original*
English INTRO1.WSA (by chaining OpenCV homography registration between
consecutive frames) and re-applies it to the edited Hebrew logo, then
re-encodes the result as a new INTRO1.WSA using wsa_encode.py.

Ported from Dune2-Heb's utils/build_intro1_animation.py -- see that repo for
the original, and CONTRIBUTING notes on why the *extraction* tooling stays
there while only *built output* normally gets copied into this repo. This
script is the one exception: it's copied here (not just its output) because
regenerating hebrew/intro1.wsa from an edited frame is part of the day-to-day
Hebrew translation workflow, same as build_heb.py is for strings/fonts/
graphics -- but it still needs the pristine, copyrighted original game files
as input (see Prerequisites), which is why hebrew/extracted/ is gitignored
and must never be committed.

Prerequisites:
  - opencv-python and numpy and Pillow (`pip install --user
    opencv-python-headless numpy pillow` if not already present -- these
    aren't needed by build_heb.py and aren't part of OpenDUNE's normal
    toolchain).
  - hebrew/extracted/<version>/INTRO/INTRO1.WSA and INTRO.PAL, extracted
    from your own legally-owned copy of the original game (e.g. via the
    `dunepak` tool -- see Dune2-Heb). Never commit this directory; it's
    gitignored for exactly that reason.
  - hebrew/INTRO1-00049.png: the hand-edited final frame, pixel-aligned to
    the original frame 49 (i.e. edited in place on top of it, not redrawn
    from scratch).

Output: hebrew/intro1.wsa -- the same file build_heb.py's ASSET_JOBS installs
as INTRO1.WSA. Also writes the 50 intermediate frame PNGs to
hebrew/tools/_wsa_debug_frames/ for inspection (gitignored, safe to delete).

NOTE ON REUSE: the corner-extrapolation constants below (REF_CONTENT_BBOX,
RELIABLE_FRAME_LO, EXTRAPOLATION_FIT_RANGE) are tuned for this specific
animation's content and the fact that frames 0-19 or so are too sparse for
ECC to register reliably. Reapplying this to a different WSA will need those
re-checked against that asset's own frames.
"""
import argparse
import os
import sys

import cv2
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wsa_decode import WSA
from wsa_encode import build_wsa

HEBREW_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Bounding box (x0,y0,x1,y1) of the logo's actual content within the 304x120
# reference frame, in frame-49 coordinates. Used to track a stable set of
# points through the registration chain instead of the full canvas corners
# (which blow up numerically).
REF_CONTENT_BBOX = (8, 54, 303, 101)

# Frames below this index have too little visible content (a handful of
# pixels or fewer) for cv2.findTransformECC to register reliably -- it
# silently "locks" onto a stale transform instead of failing loudly.
RELIABLE_FRAME_LO = 20
# Frame range used to fit the extrapolation for the unreliable low frames.
EXTRAPOLATION_FIT_RANGE = (20, 34)


def recover_frame_transforms(original_wsa_path, n_frames):
    """Cumulative homography per frame (frame k's space -> frame 49's space),
    recovered by chaining ECC registration between consecutive frames."""
    w = WSA(original_wsa_path)
    decoded = w.decode_all_frames()
    frames_gray = [
        np.frombuffer(d, dtype=np.uint8).reshape(w.height, w.width).astype(np.float32) / 255.0
        for d in decoded
    ]

    criteria = (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 2000, 1e-7)
    P = {n_frames - 1: np.eye(3, dtype=np.float32)}
    for k in range(n_frames - 1, 0, -1):
        template, inp = frames_gray[k - 1], frames_gray[k]
        warp = np.eye(3, dtype=np.float32)
        try:
            _, M_k = cv2.findTransformECC(template, inp, warp, cv2.MOTION_HOMOGRAPHY, criteria)
        except cv2.error:
            M_k = np.eye(3, dtype=np.float32)
        P[k - 1] = P[k] @ M_k
    P = np.stack([P[i] for i in range(n_frames)])

    # Fix the unreliable low frames via corner-trajectory extrapolation.
    x0, y0, x1, y1 = REF_CONTENT_BBOX
    corners_ref = np.array([[x0, y0, 1], [x1, y0, 1], [x1, y1, 1], [x0, y1, 1]], dtype=np.float64).T
    fit_lo, fit_hi = EXTRAPOLATION_FIT_RANGE

    pts_all = {}
    for k in range(fit_lo, fit_hi + 1):
        pts = np.linalg.inv(P[k]) @ corners_ref
        pts_all[k] = (pts[:2] / pts[2]).T

    ks_fit = np.arange(fit_lo, fit_hi + 1)
    coeff = {}
    for ci in range(4):
        for xy in range(2):
            y = np.array([pts_all[k][ci, xy] for k in ks_fit])
            coeff[(ci, xy)] = np.polyfit(ks_fit, y, deg=2)

    src = corners_ref[:2].T.astype(np.float32)
    for k in range(0, RELIABLE_FRAME_LO):
        dst = np.zeros((4, 2), dtype=np.float32)
        for ci in range(4):
            for xy in range(2):
                dst[ci, xy] = np.polyval(coeff[(ci, xy)], k)
        P[k] = np.linalg.inv(cv2.getPerspectiveTransform(src, dst))

    return P, w.width, w.height, decoded


def load_game_palette(pal_path):
    """Raw Dune II .PAL: 256 * 3 bytes, 6-bit VGA DAC values (0-63)."""
    raw = np.frombuffer(open(pal_path, "rb").read(), dtype=np.uint8).reshape(256, 3)
    return (raw.astype(np.float32) * 255.0 / 63.0)


def allowed_indices_from_original(decoded_frames):
    """The exact set of palette indices the original animation ever uses --
    matching new artwork against the full 256-color palette picks up
    unrelated, visually-similar-but-unused ramps elsewhere in it."""
    used = set()
    for d in decoded_frames:
        used.update(np.frombuffer(d, dtype=np.uint8).tolist())
    return np.array(sorted(used), dtype=np.uint8)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default="dune2_eu_1.07")
    ap.add_argument("--pak-name", default="INTRO")
    ap.add_argument("--wsa-file", default="INTRO1.WSA", help="original WSA filename under hebrew/extracted/<version>/<pak-name>/")
    ap.add_argument("--pal-file", default="INTRO.PAL", help="palette filename under hebrew/extracted/<version>/<pak-name>/")
    ap.add_argument("--edited-frame", default=None, help="defaults to hebrew/INTRO1-00049.png")
    ap.add_argument("--output", default=None, help="defaults to hebrew/intro1.wsa")
    ap.add_argument("--frames-out", default=None, help="defaults to hebrew/tools/_wsa_debug_frames/")
    args = ap.parse_args()

    extracted_dir = os.path.join(HEBREW_DIR, "extracted", args.version, args.pak_name)
    wsa_base = os.path.splitext(args.wsa_file)[0]

    original_wsa = os.path.join(extracted_dir, args.wsa_file)
    pal_path = os.path.join(extracted_dir, args.pal_file)
    edited_frame = args.edited_frame or os.path.join(HEBREW_DIR, "INTRO1-00049.png")
    output_wsa = args.output or os.path.join(HEBREW_DIR, "intro1.wsa")
    frames_out = args.frames_out or os.path.join(HEBREW_DIR, "tools", "_wsa_debug_frames")

    P, width, height, decoded_original = recover_frame_transforms(original_wsa, n_frames=50)
    n = len(decoded_original)

    real_palette = load_game_palette(pal_path)
    allowed_indices = allowed_indices_from_original(decoded_original)
    allowed_palette = real_palette[allowed_indices]
    print(f"legal palette indices (from original {args.wsa_file}): {allowed_indices.tolist()}")

    ref = Image.open(edited_frame)
    heb_rgba = np.array(ref.convert("RGBA"))

    def quantize(rgba):
        rgb = rgba[..., :3].astype(np.float32)
        a = rgba[..., 3:4].astype(np.float32) / 255.0
        eff = rgb * a  # composite over black (index 0 is black in-game too)
        flat = eff.reshape(-1, 3)
        d = ((flat[:, None, :] - allowed_palette[None, :, :]) ** 2).sum(axis=2)
        return allowed_indices[d.argmin(axis=1)].reshape(rgba.shape[0], rgba.shape[1])

    pal_bytes = real_palette.round().astype(np.uint8).flatten().tolist()

    os.makedirs(frames_out, exist_ok=True)
    frame_pngs = []
    for k in range(n):
        warped = cv2.warpPerspective(
            heb_rgba, P[k], (width, height),
            flags=cv2.INTER_NEAREST + cv2.WARP_INVERSE_MAP,
            borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0, 0),
        )
        idx_img = quantize(warped)
        im = Image.fromarray(idx_img, mode="P")
        im.putpalette(pal_bytes)
        out_path = os.path.join(frames_out, f"{wsa_base}-{k:05d}.png")
        im.save(out_path, transparency=0)
        frame_pngs.append(out_path)
    print(f"wrote {n} frames to {frames_out}")

    frames_pixels = [np.array(Image.open(p)).tobytes() for p in frame_pngs]
    wsa_bytes = build_wsa(frames_pixels, width, height, use_lz=True, allow_match=True)

    os.makedirs(os.path.dirname(output_wsa), exist_ok=True)
    with open(output_wsa, "wb") as f:
        f.write(wsa_bytes)
    print(f"wrote {output_wsa} ({len(wsa_bytes)} bytes)")


if __name__ == "__main__":
    main()
