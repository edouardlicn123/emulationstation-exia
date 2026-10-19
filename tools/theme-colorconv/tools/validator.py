#!/usr/bin/env python3
"""validator - check that a theme's color conversion is complete and consistent.

Checks:
  1. Every scheme declared in capabilities.xml has a block in colors.xml.
  2. Every scheme in colors.xml defines all standard variables.
  3. Variable names match the standard set (no typos).
  4. colors.xml is included from theme.xml.
  5. Report colors that remain hardcoded (mapped or skipped).

Usage:
    python3 validator.py <theme-dir> [--mapping mapping.yaml] [--strict]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from colorconv import miniyaml  # noqa: E402
from colorconv.common import collect_variables, iter_theme_files, scan_theme  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a theme's color conversion")
    parser.add_argument("theme")
    parser.add_argument("--mapping", default="mapping.yaml")
    parser.add_argument("--strict", action="store_true",
                        help="Treat unmapped colors as failures")
    args = parser.parse_args()

    root = Path(args.theme).resolve()
    if not root.is_dir():
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 1

    mapping_path = Path(args.mapping)
    if not mapping_path.is_absolute():
        mapping_path = Path(__file__).resolve().parent.parent / mapping_path

    config = miniyaml.load(str(mapping_path))
    standard = set(config.get("variables") or [])
    schemes_config = config.get("schemes") or {}
    mapping = {k.upper() for k in (config.get("mapping") or {})}
    skip = {c.upper() for c in (config.get("transparent_skip") or [])}

    problems: list[str] = []
    warnings: list[str] = []

    # 1 + 2 + 3: schemes and their variables.
    variables_by_scheme = collect_variables(root)
    if not variables_by_scheme:
        problems.append("no <variables> found for any colorScheme (is colors.xml included?)")

    for name, values in sorted(variables_by_scheme.items()):
        missing = standard - set(values)
        extra = set(values) - standard
        if missing:
            problems.append(f"scheme '{name}' is missing variables: {', '.join(sorted(missing))}")
        if extra:
            problems.append(f"scheme '{name}' has unknown variables: {', '.join(sorted(extra))}")

    declared = scan_theme(root).declared_schemes
    for name in schemes_config:
        if name not in declared:
            problems.append(f"scheme '{name}' is not declared in capabilities.xml")
    for name in declared:
        if name not in schemes_config:
            warnings.append(f"capabilities.xml declares '{name}' but mapping.yaml has no definition")

    # 4: colors.xml included from theme.xml.
    theme_text = (root / "theme.xml").read_text(encoding="utf-8") if (root / "theme.xml").exists() else ""
    if schemes_config and not re.search(r"<include>\s*\./colors\.xml\s*</include>", theme_text):
        problems.append("theme.xml does not include ./colors.xml")

    # 5: the variables must actually be referenced by the theme files. Declaring color schemes
    # without converting any hardcoded color leaves the schemes inert, and that failure is easy
    # to miss because everything still parses and loads.
    if schemes_config:
        theme_files = iter_theme_files(root)
        body = "".join(
            p.read_text(encoding="utf-8", errors="replace") for p in theme_files
        )
        referenced = {v for v in standard if ("${" + v + "}") in body}
        unused = standard - referenced
        if not referenced:
            problems.append(
                "no theme file references any color variable, so the color schemes have no "
                "effect (run extractor.py before colors_gen.py)"
            )
        elif unused:
            warnings.append(
                f"{len(unused)} variable(s) defined in colors.xml but never referenced: "
                f"{', '.join(sorted(unused))}"
            )

    # 6: leftover hardcoded colors.
    scan = scan_theme(root)
    leftovers = {c: len(u) for c, u in scan.colors().items() if c not in skip}
    unmapped = {c: n for c, n in leftovers.items() if c not in mapping}

    print(f"Theme: {root}")
    print(f"Declared schemes: {', '.join(sorted(declared)) or '(none)'}")
    print(f"Schemes with variables: {', '.join(sorted(variables_by_scheme)) or '(none)'}")
    print()

    if unmapped:
        print("Hardcoded colors not covered by mapping.yaml:")
        for c, n in sorted(unmapped.items(), key=lambda kv: -kv[1]):
            print(f"  {c}: {n}")
        print()

    for warning in warnings:
        print(f"warning: {warning}")
    for problem in problems:
        print(f"ERROR: {problem}")

    if not problems and not unmapped:
        print("Validation passed.")
        return 0
    if problems:
        return 1
    if unmapped and args.strict:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
