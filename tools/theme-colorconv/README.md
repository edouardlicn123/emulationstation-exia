# theme-colorconv

Converts ES-DE themes so that they gain switchable color schemes (ES-DE's equivalent of
Batocera's colorset mechanism).

## Requirements

Python 3.9+. No third-party packages — the small YAML subset parser is included.

## Quick start

```bash
T=~/themes/my-theme

python3 tools/scanner.py    $T              # 1. list hardcoded colors
# 2. edit mapping.yaml -> map each color to a semantic variable
python3 tools/extractor.py  $T --dry-run    # 3. preview the rewrite
python3 tools/extractor.py  $T              # 4. apply (backs up automatically)
python3 tools/colors_gen.py $T              # 5. generate colors.xml
python3 tools/caps_patch.py $T              # 6. declare schemes + include colors.xml
python3 tools/validator.py  $T --strict     # 7. validate (exit 0 = pass)
```

## Layout

| Path | Purpose |
|---|---|
| `mapping.yaml` | **The reusable asset.** Variable list, color→variable mapping, scheme definitions |
| `colorconv/common.py` | XML parsing, include-chain resolution, color scanning |
| `colorconv/miniyaml.py` | Minimal YAML subset parser (no dependencies) |
| `tools/scanner.py` | Report every hardcoded color with its location |
| `tools/extractor.py` | Rewrite colors to `${variable}` references |
| `tools/colors_gen.py` | Generate `colors.xml` from the scheme definitions |
| `tools/caps_patch.py` | Patch `capabilities.xml` and `theme.xml` |
| `tools/validator.py` | Verify the conversion is complete and consistent |

## Adding a new theme

Nothing in `tools/` needs to change. Fill in the `mapping` section of `mapping.yaml` for
the new theme and run the seven steps above.

See `~/eos/devdoc/11-主题配色转换机制.md` for the full mechanism description and checklist.
