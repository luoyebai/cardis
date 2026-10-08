#!/usr/bin/env python3
"""Rebuild assets/fonts/glyphs.txt from the text the interface can display.

Mirrors tools/update_glyphs.ps1, and additionally verifies every character
against the font's cmap so a missing glyph is caught here instead of showing up
as a blank box in the client.

Usage: python3 tools/update_glyphs.py [--check]
    --check   report missing glyphs and drift without writing the file
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEXT_FILES = [
    ROOT / "apps/client/main.cpp",
    ROOT / "apps/client/effects.cpp",
    ROOT / "apps/client/effects.hpp",
    ROOT / "apps/client/layout.hpp",
    ROOT / "src/core/game.cpp",
    ROOT / "assets/cards.json",
    ROOT / "assets/characters.json",
]
GLYPHS = ROOT / "assets/fonts/glyphs.txt"
FONT = ROOT / "assets/fonts/NotoSansCJKsc-Regular.otf"


def collect() -> str:
    characters = {chr(code) for code in range(32, 127)}
    for path in TEXT_FILES:
        characters.update(path.read_text(encoding="utf-8"))
    return "".join(sorted(character for character in characters if ord(character) >= 32))


def font_coverage() -> set[str]:
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        return set()
    with TTFont(FONT, fontNumber=0, lazy=True) as font:
        return set(font.getBestCmap().keys())


def main() -> int:
    wanted = collect()
    coverage = font_coverage()
    if coverage:
        missing = [character for character in wanted if ord(character) not in coverage]
        if missing:
            print("Characters absent from the font (they would render as blank boxes):")
            print("  " + " ".join(f"{character!r}(U+{ord(character):04X})" for character in missing))
            return 1
    else:
        print("fontTools unavailable: skipped the font coverage check")

    current = GLYPHS.read_text(encoding="utf-8") if GLYPHS.exists() else ""
    added = "".join(character for character in wanted if character not in current)
    removed = "".join(character for character in current if character not in wanted)
    if "--check" in sys.argv:
        if added or removed:
            print(f"glyphs.txt is stale: +{len(added)} -{len(removed)}")
            return 1
        print(f"glyphs.txt is current ({len(wanted)} characters)")
        return 0

    GLYPHS.write_text(wanted, encoding="utf-8", newline="")
    print(f"Wrote {GLYPHS.relative_to(ROOT)} with {len(wanted)} characters "
          f"(+{len(added)} added, -{len(removed)} dropped)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
