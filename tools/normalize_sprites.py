#!/usr/bin/env python3
"""Normalize sprite art into a predictable tree.

Source (untouched): <repo>/spritesheets assets/<folder>/<messy>.png
Target (created):   <repo>/assets/sprites/characters/<skin>/<weapon>/<anim>.png

Skins:   curtis, curtis1, ellie, namgoong
Weapons: axe, sword
Anims:   idle, run, jump, attack, hit_react, death, gun
Plus:    preview.png (from the "X skin.png" portrait)

Re-runnable: overwrites target files but never touches the source tree.
Skips: spritesheets assets/ellie sword/ellie sword/ (byte-identical duplicate)
       spritesheets assets/final boss/             (handled in a later pass)
"""

import os
import shutil
import sys

# Repo root: this script lives at <repo>/tools/normalize_sprites.py
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SOURCE_ROOT = os.path.join(REPO, "spritesheets assets")
TARGET_ROOT = os.path.join(REPO, "assets", "sprites", "characters")

# source folder name -> (skin, weapon, file-stem prefix to strip)
# The prefix is everything before the "-<state>" or " skin" portion of the
# filename. For "ellie axe/" the artist used "ellie1-" instead of "ellie axe-",
# so the prefix differs from the folder name.
LAYOUT = {
    "curtis 1 axe":   ("curtis1",  "axe",   "curtis 1 axe"),
    "curtis 1 sword": ("curtis1",  "sword", "curtis 1 sword"),
    "curtis axe":     ("curtis",   "axe",   "curtis axe"),
    "curtis sword":   ("curtis",   "sword", "curtis sword"),
    "ellie axe":      ("ellie",    "axe",   "ellie1"),
    "ellie sword":    ("ellie",    "sword", "ellie"),
    "namgoong axe":   ("namgoong", "axe",   "namgoong axe"),
    "namgoong sword": ("namgoong", "sword", "namgoong sword"),
}

# Source state strings -> canonical anim name (no extension).
# Anything not in this map is rejected with a warning.
STATE_MAP = {
    "idle":       "idle",
    "run":        "run",
    "jump":       "jump",
    "attack":     "attack",
    "hit_react":  "hit_react",
    "death":      "death",
    "character_falls_to_the_ground_and_dies": "death",
    "gun":        "gun",
    "skin":       "preview",  # the portrait file (uses a space, not a hyphen)
}

REQUIRED = ["idle", "run", "jump", "attack", "hit_react", "death", "gun",
            "preview"]


def split_state(filename: str, prefix: str):
    """Return the canonical state name for a source filename, or None.

    Handles both "<prefix>-<state>[-v1].png" (anim sheets) and
    "<prefix> skin.png" (preview portrait).
    """
    if not filename.lower().endswith(".png"):
        return None
    stem = filename[:-4]  # strip ".png"

    if stem == f"{prefix} skin":
        return "preview"

    if not stem.startswith(prefix):
        return None
    rest = stem[len(prefix):]

    # anim files use "-<state>" or "-<state>-v1"
    if rest.startswith("-"):
        rest = rest[1:]
    elif rest.startswith(" "):
        # the only " " case is " skin", handled above
        return None
    else:
        return None

    if rest.endswith("-v1"):
        rest = rest[:-3]

    return STATE_MAP.get(rest)


def normalize():
    if not os.path.isdir(SOURCE_ROOT):
        print(f"[err] source not found: {SOURCE_ROOT}", file=sys.stderr)
        return 1

    copied = 0
    skipped_unknown = []      # (src_path, reason)
    skipped_explicit = []     # paths we deliberately ignored

    for src_folder, (skin, weapon, prefix) in LAYOUT.items():
        src_dir = os.path.join(SOURCE_ROOT, src_folder)
        if not os.path.isdir(src_dir):
            print(f"[warn] missing source folder: {src_folder}")
            continue

        dst_dir = os.path.join(TARGET_ROOT, skin, weapon)
        os.makedirs(dst_dir, exist_ok=True)

        for entry in sorted(os.listdir(src_dir)):
            full = os.path.join(src_dir, entry)
            if os.path.isdir(full):
                # the nested "ellie sword/ellie sword/" duplicate lives here
                rel = os.path.relpath(full, SOURCE_ROOT)
                skipped_explicit.append(rel + "/")
                continue
            if not entry.lower().endswith(".png"):
                continue

            state = split_state(entry, prefix)
            if state is None:
                skipped_unknown.append(
                    (os.path.relpath(full, SOURCE_ROOT),
                     f"no canonical state for prefix '{prefix}'"))
                continue

            dst = os.path.join(dst_dir, state + ".png")
            shutil.copy2(full, dst)
            copied += 1
            print(f"  cp  {os.path.relpath(full, SOURCE_ROOT)}")
            print(f"      -> {os.path.relpath(dst, REPO)}")

    print()
    print(f"copied: {copied} files")
    if skipped_explicit:
        print(f"skipped (explicit): {len(skipped_explicit)}")
        for s in skipped_explicit:
            print(f"  - {s}")
    if skipped_unknown:
        print(f"skipped (unrecognized): {len(skipped_unknown)}")
        for path, why in skipped_unknown:
            print(f"  - {path}  ({why})")

    # ---- verify every <skin>/<weapon> folder has the required set ---------
    print()
    print("verification:")
    missing_total = 0
    extra_total = 0
    for src_folder, (skin, weapon, _) in LAYOUT.items():
        dst_dir = os.path.join(TARGET_ROOT, skin, weapon)
        have = set()
        if os.path.isdir(dst_dir):
            have = {n[:-4] for n in os.listdir(dst_dir)
                    if n.lower().endswith(".png")}
        missing = [r for r in REQUIRED if r not in have]
        extra = sorted(have - set(REQUIRED))
        marker = "OK " if not missing and not extra else "!! "
        print(f"  {marker}{skin}/{weapon}: "
              f"{len(have)}/{len(REQUIRED)} files"
              + (f"   missing={missing}" if missing else "")
              + (f"   extra={extra}"     if extra   else ""))
        missing_total += len(missing)
        extra_total   += len(extra)

    print()
    if missing_total == 0 and extra_total == 0:
        print(f"all 8 folders complete ({len(LAYOUT) * len(REQUIRED)} files)")
        return 0
    else:
        print(f"INCOMPLETE: missing={missing_total} extra={extra_total}")
        return 2


if __name__ == "__main__":
    sys.exit(normalize())
