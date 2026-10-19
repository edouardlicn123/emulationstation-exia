"""Minimal YAML subset parser.

Only what mapping.yaml needs: block mappings, block sequences and scalars.
Avoids a third-party dependency so the tools run on a stock Python 3.
"""

from __future__ import annotations

import re


def _strip_comment(line: str) -> str:
    out = []
    quote = None
    for ch in line:
        if quote:
            out.append(ch)
            if ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
            out.append(ch)
            continue
        if ch == "#":
            break
        out.append(ch)
    return "".join(out).rstrip()


def _scalar(text: str):
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "\"'":
        return text[1:-1]
    low = text.lower()
    if low in ("true", "yes"):
        return True
    if low in ("false", "no"):
        return False
    if text in ("null", "~", ""):
        return None
    if text.startswith("[") and text.endswith("]"):
        inner = text[1:-1].strip()
        if not inner:
            return []
        return [_scalar(p) for p in inner.split(",")]
    # Values that look like ES-DE hex colors must stay strings. "000000" would otherwise
    # become the integer 0 and "00000080" (a dimming alpha) would become 80.
    if re.fullmatch(r"[0-9A-Fa-f]{6}", text) or re.fullmatch(r"[0-9A-Fa-f]{8}", text):
        return text
    try:
        return int(text)
    except ValueError:
        pass
    return text


class _Line:
    __slots__ = ("indent", "text", "number")

    def __init__(self, indent: int, text: str, number: int):
        self.indent = indent
        self.text = text
        self.number = number

    @property
    def is_item(self) -> bool:
        return self.text == "-" or self.text.startswith("- ")


def _tokenize(text: str) -> list[_Line]:
    lines: list[_Line] = []
    for number, raw in enumerate(text.splitlines(), start=1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        stripped = _strip_comment(raw)
        if not stripped.strip():
            continue
        indent = len(stripped) - len(stripped.lstrip())
        lines.append(_Line(indent, stripped.strip(), number))
    return lines


def _parse_block(lines: list[_Line], pos: int, indent: int):
    """Parse a block at the given indent, returning (value, next_pos)."""
    if pos >= len(lines):
        return None, pos

    if lines[pos].is_item:
        items = []
        while pos < len(lines) and lines[pos].indent == indent and lines[pos].is_item:
            body = lines[pos].text[1:].strip()
            if body == "":
                # Nested block under the dash.
                pos += 1
                if pos < len(lines) and lines[pos].indent > indent:
                    value, pos = _parse_block(lines, pos, lines[pos].indent)
                    items.append(value)
                else:
                    items.append(None)
            elif ":" in body and not body.startswith(("'", '"')):
                # Inline first key of a mapping item; handled as a nested mapping.
                synthetic = [_Line(indent + 2, body, lines[pos].number)]
                pos += 1
                while pos < len(lines) and lines[pos].indent > indent:
                    synthetic.append(lines[pos])
                    pos += 1
                value, _ = _parse_block(synthetic, 0, indent + 2)
                items.append(value)
            else:
                items.append(_scalar(body))
                pos += 1
        return items, pos

    mapping: dict = {}
    while pos < len(lines):
        line = lines[pos]
        if line.indent < indent:
            break
        if line.indent > indent:
            pos += 1
            continue
        if line.is_item or ":" not in line.text:
            break

        key, _, value = line.text.partition(":")
        key = key.strip()
        value = value.strip()
        pos += 1

        if value == "":
            if pos < len(lines) and lines[pos].indent > indent:
                child_indent = lines[pos].indent
                child, pos = _parse_block(lines, pos, child_indent)
                mapping[key] = child
            elif pos < len(lines) and lines[pos].is_item and lines[pos].indent == indent:
                child, pos = _parse_block(lines, pos, indent)
                mapping[key] = child
            else:
                mapping[key] = None
        else:
            mapping[key] = _scalar(value)

    return mapping, pos


def loads(text: str):
    lines = _tokenize(text)
    if not lines:
        return {}
    value, _ = _parse_block(lines, 0, lines[0].indent)
    return value


def load(path: str):
    with open(path, "r", encoding="utf-8") as handle:
        return loads(handle.read())
