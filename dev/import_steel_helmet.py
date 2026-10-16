"""Extract the cosmetic Steel Helmet assets; do not import item or gameplay data."""
import argparse
import json
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET


def xml_fragment(path):
    text = path.read_text(encoding="utf-8-sig")
    text = re.sub(r"<\?xml.*?\?>|<!--.*?-->", "", text, flags=re.S)
    return ET.fromstring("<root>" + text + "</root>")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="The 1.13 installation containing Base")
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    base = args.game / "Base"
    definitions = base / "TableData/LogicalBodyTypes"
    destination = project / "assets/externalized/visual-equipment"
    destination.mkdir(parents=True, exist_ok=True)
    animation_text = (project / "src/game/Tactical/Animation_Data.cc").read_text()
    # The database is indexed by array position. Some historical M() labels
    # are duplicated/misspelled, so their first argument is not authoritative.
    header = (project / "src/game/Tactical/Animation_Data.h").read_text()
    enum_text = re.search(r'enum AnimationSurfaceTypes\s*\{(.*?)\}', header, re.S).group(1)
    enum_text = re.sub(r'/\*.*?\*/|//[^\n]*', '', enum_text, flags=re.S)
    names = [part.strip().split('=')[0].strip() for part in enum_text.split(',') if part.strip()]
    rows = re.findall(
        r'M\(\s*(\w+),\s*ANIMSDIR\s*"([^"]+)"\s*,\s*\w+,\s*[^,]+,\s*(\d+),',
        animation_text,
    )
    assert len(rows) == len(names) - 1, 'Animation table/enum count mismatch'
    vanilla = {
        names[index]: {"base_sprite": "anims" + filename.lower(), "directions": int(directions)}
        for index, (_, filename, directions) in enumerate(rows)
    }
    surfaces = {}
    for path in definitions.rglob("AnimationSurfaces*.xml"):
        if path.parent == definitions:
            continue  # The master document contains external entities.
        for surface in xml_fragment(path).iter("AnimSurface"):
            surfaces[surface.attrib["name"]] = surface.attrib

    mappings = []
    copied = set()
    for path in sorted(definitions.rglob("LogicalBodyType*gear.xml")):
        body = path.parent.name.removeprefix("LBT_")
        for layer in xml_fragment(path).iter("Layer"):
            if layer.attrib.get("name") != "helmet":
                continue
            for prop in layer.findall("LayerProp"):
                selector = prop.attrib.get("filter", "")
                if not selector.startswith("SteelHelmet"):
                    continue
                condition = selector.removeprefix("SteelHelmetAnd") if selector != "SteelHelmet" else "Any"
                for entry in prop.findall("Surface"):
                    name = entry.attrib.get("animsurface")
                    if name not in vanilla:
                        continue
                    surface = surfaces[entry.attrib["name"]]
                    if int(surface["directions"]) != vanilla[name]["directions"]:
                        continue
                    original = base / surface["file"].replace("\\", "/")
                    relative = Path(body) / original.name
                    output = destination / "steel-helmet" / relative
                    output.parent.mkdir(parents=True, exist_ok=True)
                    if output not in copied:
                        shutil.copy2(original, output)
                        copied.add(output)
                    mappings.append({
                        "body": body, "animation": name,
                        "base_sprite": vanilla[name]["base_sprite"],
                        "weapon": condition,
                        "sprite": "visual-equipment/steel-helmet/" + relative.as_posix(),
                        "directions": int(surface["directions"]),
                    })
    shutil.copy2(base / "Palettes/Hats.stp", destination / "steel-helmet-palette.stp")
    manifest = {
        "enabled": True, "item": "STEEL_HELMET", "item_index": 176,
        "palette": "visual-equipment/steel-helmet-palette.stp",
        "source": "JA2 1.13 LOBOT Steel Helmet artwork and animation mappings",
        "mappings": mappings,
    }
    (destination / "steel-helmet.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Imported {len(copied)} unmodified sprites and {len(mappings)} mappings.")


if __name__ == "__main__":
    main()
