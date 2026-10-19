#!/usr/bin/env python3
"""scanner - list every hardcoded color in a theme.

Usage:
    python3 scanner.py <theme-dir> [--json]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from colorconv.common import is_transparent, scan_theme  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description="List hardcoded colors in an ES-DE theme")
    parser.add_argument("theme", help="Path to the theme directory")
    parser.add_argument("--json", action="store_true", help="Emit JSON instead of text")
    parser.add_argument("--show-transparent", action="store_true",
                        help="Include colors with a non-opaque alpha channel")
    args = parser.parse_args()

    root = Path(args.theme).resolve()
    if not root.is_dir():
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 1

    scan = scan_theme(root)
    grouped = scan.colors()

    if args.json:
        payload = {
            "theme": str(root),
            "errors": scan.errors,
            "declaredColorSchemes": scan.declared_schemes,
            "colors": {
                color: {
                    "count": len(uses),
                    "transparent": is_transparent(color),
                    "uses": [
                        {
                            "file": str(use.file.relative_to(root)),
                            "line": use.line,
                            "view": use.view,
                            "element": use.element,
                            "elementName": use.element_name,
                            "property": use.property,
                        }
                        for use in uses
                    ],
                }
                for color, uses in grouped.items()
                if args.show_transparent or not is_transparent(color)
            },
        }
        print(json.dumps(payload, indent=2, ensure_ascii=False))
        return 0

    print(f"Theme: {root}")
    if scan.declared_schemes:
        print(f"Declared color schemes: {', '.join(scan.declared_schemes)}")
    else:
        print("Declared color schemes: (none)")
    print()

    visible = [
        (c, u) for c, u in grouped.items()
        if args.show_transparent or not is_transparent(c)
    ]

    if not visible:
        print("No hardcoded opaque colors found.")
        return 0

    print(f"{'COLOR':10s} {'N':>3s}  {'VIEWS':<28s} USED AS")
    print("-" * 78)
    for color, uses in visible:
        views = ", ".join(sorted({u.view for u in uses if u.view})) or "(root)"
        if len(views) > 26:
            views = views[:23] + "..."
        elements = ", ".join(sorted({f"{u.element}[{u.element_name}]" if u.element_name else u.element
                                     for u in uses})[:3])
        print(f"{color:10s} {len(uses):3d}  {views:<28s} {elements}")

    trans = [c for c in grouped if is_transparent(c)]
    if trans and not args.show_transparent:
        print()
        print(f"Note: {len(trans)} transparent color(s) excluded (use --show-transparent): "
              f"{', '.join(trans[:8])}")

    for err in scan.errors:
        print(f"error: {err}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
