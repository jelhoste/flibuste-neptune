#!/usr/bin/env python3
"""Regenere adapter-juce/src/FacadeSpec.h a partir des deux documents de specification des styles de la Facade.

A relancer chaque fois que docs/specification-styles-facade.md ou docs/style-pack-specification.en.md change :
    python3 scripts/gen_facade_spec_header.py
Le texte est decoupe en morceaux de moins de 6000 octets (MSVC refuse les litteraux de plus de 16 Ko).
"""
import pathlib

root = pathlib.Path(__file__).resolve().parent.parent
docs = {"french": root / "docs" / "specification-styles-facade.md", "english": root / "docs" / "style-pack-specification.en.md"}


def chunks(text, limit=6000):
    out, cur = [], ""
    for line in text.splitlines(keepends=True):
        if len((cur + line).encode("utf-8")) > limit and cur:
            out.append(cur)
            cur = ""
        cur += line
    if cur:
        out.append(cur)
    return out


parts = ["#pragma once\n// GENERE par scripts/gen_facade_spec_header.py : ne pas modifier a la main (modifier les documents de docs/ puis relancer le script).\n",
         "#include <juce_core/juce_core.h>\n\nnamespace facade_spec\n{\n"]
for name, path in docs.items():
    parts.append(f"inline juce::String {name}()\n{{\n    return juce::String::fromUTF8 (\n")
    pieces = chunks(path.read_text(encoding="utf-8"))
    for i, piece in enumerate(pieces):
        assert ')FSPEC"' not in piece
        parts.append(f'        R"FSPEC({piece})FSPEC"' + ("\n" if i + 1 < len(pieces) else ");\n"))
    parts.append("}\n\n")
parts.append("} // namespace facade_spec\n")
(root / "adapter-juce" / "src" / "FacadeSpec.h").write_text("".join(parts), encoding="utf-8")
print("FacadeSpec.h generated")
