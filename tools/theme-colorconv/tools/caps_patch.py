#!/usr/bin/env python3
"""caps_patch - declare the color schemes in capabilities.xml.

Adds a <colorScheme name="..."> block with labels for every scheme defined in mapping.yaml,
and makes sure theme.xml includes colors.xml.

Usage:
    python3 caps_patch.py <theme-dir> [--mapping mapping.yaml] [--dry-run]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from colorconv import miniyaml  # noqa: E402
from colorconv.common import declared_label_order  # noqa: E402


def render_scheme_block(name: str, label: dict, indent: str = "    ") -> str:
    lines = [f'{indent}<colorScheme name="{name}">']
    for lang in declared_label_order():
        if lang in label:
            lines.append(f'{indent}    <label language="{lang}">{label[lang]}</label>')
    lines.append(f"{indent}</colorScheme>")
    return "\n".join(lines)


def patch_capabilities(path: Path, schemes: dict) -> tuple[str, list[str]]:
    text = path.read_text(encoding="utf-8")
    notes: list[str] = []

    # Remove any previously generated blocks so the operation is idempotent.
    text = re.sub(
        r"[ \t]*<!-- colorconv:begin -->.*?<!-- colorconv:end -->\n?",
        "",
        text,
        flags=re.S,
    )
    text = re.sub(r"\n?[ \t]*<colorScheme name=\"[^\"]+\">.*?</colorScheme>\n?", "\n", text, flags=re.S)

    blocks = "\n".join(
        "    <!-- colorconv:begin -->"
        f'\n    <colorScheme name="{name}">\n'
        + "\n".join(
            f'        <label language="{lang}">{(blk or {}).get("label", {}).get(lang, name)}</label>'
            for lang in declared_label_order()
            if lang in ((blk or {}).get("label") or {})
        )
        + "\n    </colorScheme>"
        f'\n    <!-- colorconv:end -->\n'
        for name, blk in schemes.items()
    )

    # Insert right after the opening <themeCapabilities> tag.
    match = re.search(r"(<themeCapabilities>\s*\n)", text)
    if not match:
        print(f"error: no <themeCapabilities> tag in {path}", file=sys.stderr)
        return text, ["capabilities.xml has no <themeCapabilities> tag"]

    text = text[: match.end()] + blocks + "\n" + text[match.end() :]
    notes.append(f"declared {len(schemes)} color scheme(s)")
    return text, notes


def ensure_include(theme_file: Path) -> tuple[str, list[str]]:
    text = theme_file.read_text(encoding="utf-8")
    if re.search(r"<include>\s*\./colors\.xml\s*</include>", text):
        return text, ["colors.xml already included"]

    match = re.search(r"(<theme>\s*\n)", text)
    if not match:
        return text, ["no <theme> tag in theme.xml"]

    insert = "    <include>./colors.xml</include>\n"
    text = text[: match.end()] + insert + text[match.end() :]
    return text, ["added <include>./colors.xml</include> to theme.xml"]


def main() -> int:
    parser = argparse.ArgumentParser(description="Declare color schemes in capabilities.xml")
    parser.add_argument("theme")
    parser.add_argument("--mapping", default="mapping.yaml")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    root = Path(args.theme).resolve()
    if not root.is_dir():
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 1

    mapping_path = Path(args.mapping)
    if not mapping_path.is_absolute():
        mapping_path = Path(__file__).resolve().parent.parent / mapping_path

    schemes = (miniyaml.load(str(mapping_path)).get("schemes")) or {}
    if not schemes:
        print("error: no schemes in mapping", file=sys.stderr)
        return 1

    all_notes: list[str] = []

    caps = root / "capabilities.xml"
    if not caps.exists():
        print(f"error: missing {caps}", file=sys.stderr)
        return 1
    text, notes = patch_capabilities(caps, schemes)
    if not args.dry_run:
        caps.write_text(text, encoding="utf-8")
    all_notes += notes

    theme_file = root / "theme.xml"
    if theme_file.exists():
        text, notes = ensure_include(theme_file)
        if not args.dry_run:
            theme_file.write_text(text, encoding="utf-8")
        all_notes += notes

    print(f"{'Would patch' if args.dry_run else 'Patched'} {root.name}:")
    for note in all_notes:
        print(f"  - {note}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
