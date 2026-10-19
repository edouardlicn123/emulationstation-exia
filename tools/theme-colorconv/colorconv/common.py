#!/usr/bin/env python3
"""Shared helpers for the theme-colorconv tools.

Parses ES-DE theme XML files and identifies hardcoded colors. The tools deliberately
operate on the raw text of each file so that formatting, comments and element order are
preserved when writing files back.
"""

from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

# ES-DE colors are 6-digit (RRGGBB) or 8-digit (RRGGBBAA) hex values.
COLOR_RE = re.compile(r"^#?([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$")

# Property names whose value is interpreted as a color. Used to tell a real color
# from e.g. a path or a numeric offset that happens to look like hex.
COLOR_PROPERTIES = {
    "color",
    "backgroundColor",
    "centerColor",
    "dimming",
    "selectedColor",
    "selectorColor",
    "selectorColorEnd",
    "separatorColor",
    "scrollbarColor",
    "opacity",
}

# Elements whose text content is a path rather than a value.
PATH_ELEMENTS = {"path", "default", "fontPath", "defaultImage", "defaultFolderImage", "image"}


@dataclass
class ColorUse:
    """A single hardcoded color occurrence."""

    file: Path
    line: int
    element: str
    element_name: str
    view: str
    property: str
    color: str
    context: str = ""

    @property
    def key(self) -> str:
        return self.color.upper()


@dataclass
class ThemeScan:
    """The result of scanning a theme directory."""

    root: Path
    uses: list[ColorUse] = field(default_factory=list)
    variables: dict[str, dict[str, str]] = field(default_factory=dict)
    declared_schemes: dict[str, str] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)

    def colors(self) -> dict[str, list[ColorUse]]:
        """Group the color uses by normalized color value, most frequent first."""
        grouped: dict[str, list[ColorUse]] = {}
        for use in self.uses:
            grouped.setdefault(use.key, []).append(use)
        return dict(sorted(grouped.items(), key=lambda kv: (-len(kv[1]), kv[0])))


def normalize_color(value: str) -> str | None:
    """Return the normalized upper-case color, or None if the value is not a color."""
    value = value.strip()
    if not COLOR_RE.match(value):
        return None
    return value.lstrip("#").upper()


def alpha_of(color: str) -> int:
    """Return the alpha byte of a color (FF when not specified)."""
    if len(color) == 8:
        return int(color[6:], 16)
    return 0xFF


def is_transparent(color: str) -> bool:
    """True if the color has an alpha channel that is not fully opaque."""
    return alpha_of(color) != 0xFF


def iter_theme_files(root: Path) -> list[Path]:
    """Return the theme's own XML files, following the include chain from theme.xml.

    A theme directory may also contain directories that are not theme files at all, such as
    ``_inc/systems`` which holds per-system metadata with its own color palettes, or ``assets``.
    Following the include chain is therefore the only reliable way to tell theme files apart
    from theme data.
    """
    theme_file = root / "theme.xml"
    if not theme_file.exists():
        return []

    files: set[Path] = set()
    queue: list[Path] = [theme_file]

    while queue:
        current = queue.pop()
        if current in files or not current.exists():
            continue
        files.add(current)

        try:
            tree = ET.parse(current)
        except ET.ParseError:
            continue

        for inc in tree.getroot().iter("include"):
            target = (inc.text or "").strip()
            if not target:
                continue
            resolved = (current.parent / target).resolve()
            if resolved not in files:
                queue.append(resolved)

    return sorted(files)


def find_system_override_files(root: Path) -> list[Path]:
    """Return per-system theme override files, i.e. ``system/<platform>/theme.xml``."""
    system_dir = root / "system"
    if not system_dir.is_dir():
        return []
    return sorted(p for p in system_dir.rglob("theme.xml"))


def find_view_context(root: Path, path: Path) -> str:
    """Return the name of the view a file belongs to, or an empty string."""
    try:
        tree = ET.parse(path)
    except ET.ParseError:
        return ""
    for view in tree.getroot().iter("view"):
        if view.get("name"):
            return view.get("name")
    return ""


def collect_variables(root: Path) -> dict[str, dict[str, str]]:
    """Collect <variables> blocks, keyed by their enclosing colorScheme name."""
    variables: dict[str, dict[str, str]] = {}

    for path in iter_theme_files(root):
        try:
            tree = ET.parse(path)
        except ET.ParseError:
            continue
        for scheme in tree.getroot().iter("colorScheme"):
            name = scheme.get("name", "")
            block = scheme.find("variables")
            if block is None:
                continue
            variables.setdefault(name, {})
            for var in block:
                variables[name][var.tag] = (var.text or "").strip()

    return variables


DECLARED_LABELS_ORDER = ["en_US", "zh_CN"]


def declared_label_order() -> list[str]:
    """Languages emitted first when generating <label> tags."""
    return list(DECLARED_LABELS_ORDER)


def collect_declared_schemes(root: Path) -> dict[str, str]:
    """Return {scheme name: first label} from capabilities.xml."""
    caps = root / "capabilities.xml"
    schemes: dict[str, str] = {}

    if not caps.exists():
        return schemes

    try:
        tree = ET.parse(caps)
    except ET.ParseError:
        return schemes

    for node in tree.getroot().iter("colorScheme"):
        name = node.get("name")
        if not name:
            continue
        label = ""
        for lab in node.iter("label"):
            if lab.text:
                label = lab.text.strip()
                break
        schemes[name] = label or name

    return schemes


def scan_theme(root: Path, include_color_definitions: bool = False) -> ThemeScan:
    """Scan a theme directory and collect every hardcoded color.

    Color values inside <variables> blocks are the color scheme *definitions* rather than
    hardcoded leftovers, so they are skipped unless include_color_definitions is set.
    """
    scan = ThemeScan(root=root)
    scan.variables = collect_variables(root)
    scan.declared_schemes = collect_declared_schemes(root)

    for path in iter_theme_files(root):
        try:
            tree = ET.parse(path)
        except ET.ParseError as exc:
            scan.errors.append(f"{path}: XML parse error: {exc}")
            continue

        # Collect the elements that live inside a <variables> block.
        in_variables: set[int] = set()
        if not include_color_definitions:
            for variables in tree.getroot().iter("variables"):
                for child in variables.iter():
                    in_variables.add(id(child))

        view = find_view_context(root, path)
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()

        for elem in tree.getroot().iter():
            tag = elem.tag
            if tag in PATH_ELEMENTS:
                continue

            # Only leaf nodes with text carry values.
            if len(list(elem)) > 0:
                continue

            if id(elem) in in_variables:
                continue

            text = (elem.text or "").strip()
            color = normalize_color(text)
            if color is None:
                continue

            prop = tag if tag in COLOR_PROPERTIES else "value"
            line = find_line(lines, text)
            scan.uses.append(
                ColorUse(
                    file=path,
                    line=line,
                    element=tag,
                    element_name=elem.get("name", ""),
                    view=view,
                    property=prop,
                    color=color,
                    context=" ".join(t.strip() for t in lines[max(0, line - 4) : line]),
                )
            )

    return scan


def find_line(lines: list[str], needle: str) -> int:
    """Best-effort 1-based line number for a value."""
    target = needle.strip()
    for idx, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith(f"<{needle}") or f">{target}<" in stripped:
            return idx + 1
        if target in stripped and "<" in stripped:
            return idx + 1
    return 1
