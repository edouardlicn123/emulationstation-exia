#!/usr/bin/env python3
"""extractor - replace hardcoded colors with theme color variables.

Usage:
    python3 extractor.py <theme-dir> [--mapping mapping.yaml] [--dry-run] [--no-backup]
"""

from __future__ import annotations

import argparse
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from colorconv import miniyaml  # noqa: E402
from colorconv.common import (  # noqa: E402
    iter_theme_files,
    normalize_color,
    scan_theme,
)


def load_mapping(path: Path) -> tuple[dict, dict, set]:
    data = miniyaml.load(str(path))
    mapping = {k.upper(): v for k, v in (data.get("mapping") or {}).items()}
    context = {k: v for k, v in (data.get("context_mapping") or {}).items()}
    skip = {c.upper() for c in (data.get("transparent_skip") or [])}
    return mapping, context, skip


def process_file(path: Path, mapping: dict, context: dict, skip: set, dry_run: bool) -> dict:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    changed = 0
    unmapped: list[str] = []

    out: list[str] = []
    for line in lines:
        stripped = line.strip()
        new_line = line
        # Only touch simple "<tag>VALUE</tag>" style lines.
        if stripped.startswith("<") and not stripped.startswith("<?"):
            start = line.find(">")
            if start != -1:
                tag_part = line[:start]
                tail = line[start + 1 :]
                close = tail.find("<")
                if close != -1:
                    tag = tag_part.strip().lstrip("<").rstrip("/").strip()
                    value = tail[:close].strip()
                    color = normalize_color(value)
                    if color:
                        # The element tag wins over the raw value, so that e.g. <selectedColor>
                        # and <color> can map to different variables even when equal.
                        if tag in context:
                            new_line = line[: start + 1] + "${" + context[tag] + "}" + tail[close:]
                            changed += 1
                        elif color in skip:
                            unmapped.append(f"{color} (skipped)")
                        elif color in mapping:
                            new_line = line[: start + 1] + "${" + mapping[color] + "}" + tail[close:]
                            changed += 1
                        else:
                            unmapped.append(color)
        out.append(new_line)

    if changed and not dry_run:
        path.write_text("".join(out), encoding="utf-8")

    return {"changed": changed, "unmapped": unmapped}


def main() -> int:
    parser = argparse.ArgumentParser(description="Replace hardcoded colors with theme variables")
    parser.add_argument("theme")
    parser.add_argument("--mapping", default="mapping.yaml")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-backup", action="store_true")
    parser.add_argument("--backup-dir", default="~/theme-backups",
                        help="Where to write the automatic backup (default: ~/theme-backups). "
                             "Must not be the user theme directory, or the backup would be "
                             "registered as a duplicate theme.")
    args = parser.parse_args()

    root = Path(args.theme).resolve()
    if not root.is_dir():
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 1

    mapping_path = Path(args.mapping)
    if not mapping_path.is_absolute():
        mapping_path = Path(__file__).resolve().parent.parent / mapping_path
    mapping, context, skip = load_mapping(mapping_path)

    if not mapping:
        print("error: mapping is empty", file=sys.stderr)
        return 1

    files = iter_theme_files(root)
    if not files:
        print(f"error: could not resolve the theme's files from {root/'theme.xml'}", file=sys.stderr)
        return 1

    if not args.dry_run and not args.no_backup:
        # The backup must NOT go next to the theme: ES-DE scans every directory under the user
        # theme directory and would register a duplicate theme if a copy sat beside the original.
        backup_dir = Path(args.backup_dir).expanduser()
        backup_dir.mkdir(parents=True, exist_ok=True)
        backup = backup_dir / f"{root.name}.colorconv-backup-{time.strftime('%Y%m%d-%H%M%S')}"
        shutil.copytree(root, backup)
        print(f"Backup written to {backup}")

    total = 0
    print()
    for path in files:
        result = process_file(path, mapping, context, skip, args.dry_run)
        if result["changed"]:
            total += result["changed"]
            print(f"  {path.relative_to(root)}: {result['changed']} replaced")
        if result["unmapped"]:
            uniq = sorted(set(result["unmapped"]))
            print(f"    unmapped: {', '.join(uniq)}")

    print()
    print(f"{'Would replace' if args.dry_run else 'Replaced'}: {total} occurrence(s)")

    # Report colors still present in the theme after the rewrite.
    scan = scan_theme(root)
    leftovers = {
        c: len(u)
        for c, u in scan.colors().items()
        if c not in skip and c not in mapping
    }
    if leftovers:
        print("\nStill unmapped (add them to mapping.yaml to convert):")
        for color, count in sorted(leftovers.items(), key=lambda kv: -kv[1]):
            print(f"  {color}: {count}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
