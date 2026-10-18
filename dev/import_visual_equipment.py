"""Import JA2 1.13 LOBOT art for vanilla items, without importing gameplay data."""
import argparse
from collections import defaultdict
from functools import lru_cache
import json
from pathlib import Path
import re
import shutil

from import_steel_helmet import xml_fragment

# Explicit vanilla identities: never interpret a new 1.13 item ID as vanilla gear.
GEAR = {
    "SteelHelmet": ("HELMETPOS", [176]),
    "KevlarHelmet": ("HELMETPOS", [177, 178, 179]),
    "SpectraHelmet": ("HELMETPOS", [180, 181, 182]),
    "FlakVest": ("VESTPOS", [161, 162, 163]),
    "KevlarVest": ("VESTPOS", [164, 165, 166]),
    "SpectraVest": ("VESTPOS", [167, 168, 169]),
    "GuardianVest": ("VESTPOS", [196, 197, 198]),
    "KevlarLeggings": ("LEGPOS", [170, 171, 172]),
    "SpectraLeggings": ("LEGPOS", [173, 174, 175]),
    "Longsleeves": ("VESTPOS", [188, 189, 190, 191]),
    "Gasmask": ("FACE", [213]),
    "NVG": ("FACE", [211, 246]),
    "Goggles": ("FACE", [212]),
    "HeadSet": ("FACE", [210]),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    base = args.game / "Base"
    definitions = base / "TableData/LogicalBodyTypes"
    destination = project / "assets/externalized/visual-equipment"
    destination.mkdir(parents=True, exist_ok=True)
    animation_text = (project / "src/game/Tactical/Animation_Data.cc").read_text()
    header = (project / "src/game/Tactical/Animation_Data.h").read_text()
    enum_text = re.search(r'enum AnimationSurfaceTypes\s*\{(.*?)\}', header, re.S).group(1)
    enum_text = re.sub(r'/\*.*?\*/|//[^\n]*', '', enum_text, flags=re.S)
    names = [part.strip().split('=')[0].strip() for part in enum_text.split(',') if part.strip()]
    rows = re.findall(r'M\(\s*(\w+),\s*ANIMSDIR\s*"([^"]+)"\s*,\s*\w+,\s*[^,]+,\s*(\d+),', animation_text)
    assert len(rows) == len(names) - 1, 'Animation table/enum count mismatch'
    vanilla = {names[i]: ("anims" + filename.lower(), int(directions))
               for i, (_, filename, directions) in enumerate(rows)}
    surfaces = {}
    for path in definitions.rglob("AnimationSurfaces*.xml"):
        if path.parent != definitions:
            for surface in xml_fragment(path).iter("AnimSurface"):
                surfaces[surface.get("name")] = surface.attrib
    palettes = {e.get('name'): e.get('filename') for e in xml_fragment(definitions / 'Palettes.xml').iter('Palette')}
    filters = {e.get('name'): e for e in xml_fragment(definitions / 'Filters.xml').iter('Filter')}
    weapon_models = {e['itemIndex']: e for e in json.loads((project / 'assets/externalized/weapons.json').read_text())}
    weapons = {index: item['internalType'] for index, item in weapon_models.items()}
    class_by_type = {
        'PISTOL': 'HANDGUNCLASS', 'M_PISTOL': 'HANDGUNCLASS', 'SMG': 'SMGCLASS',
        'RIFLE': 'RIFLECLASS', 'SN_RIFLE': 'RIFLECLASS', 'ASRIFLE': 'RIFLECLASS',
        'LMG': 'MGCLASS', 'SHOTGUN': 'SHOTGUNCLASS', 'BLADE': 'KNIFECLASS',
        'THROWINGBLADE': 'KNIFECLASS',
    }
    type_by_type = {'SN_RIFLE': 'GUN_SN_RIFLE', 'ASRIFLE': 'GUN_AS_RIFLE'}

    def evaluate(node, right, left):
        tag, value = node.tag, (node.text or '').strip()
        if tag in ('Filter', 'AND'):
            result = all(evaluate(child, right, left) for child in node)
        elif tag == 'OR':
            result = any(evaluate(child, right, left) for child in node)
        elif tag == 'FILTER':
            result = match(value, right, left)
        elif tag in ('HANDPOS', 'SECONDHANDPOS'):
            result = (right if tag == 'HANDPOS' else left) in [int(x.strip()) for x in value.split(',')]
        elif tag in ('WEAPON_TYPE', 'LEFT_WEAPON_TYPE'):
            item = right if tag == 'WEAPON_TYPE' else left
            kind = weapons[item]
            # Vanilla permits one-handed MP5K, MAC-10 and P90 animations. LOBOT
            # uses its machine-pistol family for these one-handed poses, where
            # an exact model sheet does not exist. Do not change weapon flags.
            if kind == 'SMG' and not weapon_models[item].get('bTwoHanded', False):
                kind = 'M_PISTOL'
            actual = type_by_type.get(kind, 'GUN_' + kind)
            result = actual == value or (value == 'NOT_GUN' and kind not in ('PISTOL', 'M_PISTOL', 'SMG', 'RIFLE', 'SN_RIFLE', 'ASRIFLE', 'LMG', 'SHOTGUN'))
        elif tag == 'WEAPON_CLASS':
            result = class_by_type.get(weapons[right], 'NOGUNCLASS') == value
        else:
            # Inventory expansions, backpacks, new attachments and NPC filters
            # are outside this vanilla-only visual import.
            result = False
        return not result if 'not' in node.attrib else result

    @lru_cache(None)
    def match(name, right, left):
        if not name or name in GEAR:
            return True
        if name == 'HasBluntMeleeWeaponInHand':
            return right == 64  # Vanilla crowbar, not the new 1.13 blunt weapons.
        return evaluate(filters[name], right, left)

    conditions, condition_ids = [], {}

    def condition_id(name):
        if name in condition_ids:
            return condition_ids[name]
        groups = defaultdict(list)
        for right in range(66):
            lefts = tuple(left for left in range(66) if match(name, right, left))
            if lefts:
                groups[lefts].append(right)
        if not groups:
            condition_ids[name] = None
            return None
        index = len(conditions)
        conditions.append({'source_filter': name, 'hands': [
            {'right': rights, 'left': list(lefts)} for lefts, rights in groups.items()]})
        condition_ids[name] = index
        return index

    copied, used_palettes, mappings = set(), set(), []
    layer_order = [e.get('name') for e in xml_fragment(definitions / 'Layers.xml').iter('Layer')]
    layers = {e.get('name'): e.attrib for e in xml_fragment(definitions / 'Layers.xml').iter('Layer')}
    for body in ('RGM', 'BGM', 'RGF'):
        folder = definitions / ('LBT_' + body)
        body_file = next(p for p in folder.glob('LogicalBodyType*.xml') if p.stem in ('LogicalBodyType_LBT_' + body, 'LogicalBodyType_' + body))
        paths = [folder / f'LogicalBodyType_{body}_gun_paletteswaps.xml',
                 folder / f'LogicalBodyType_{body}_guns.xml',
                 folder / f'LogicalBodyType_{body}_gear.xml', body_file]
        for path in paths:
            for layer in xml_fragment(path).iter('Layer'):
                layer_name = layer.get('name')
                for prop in layer.findall('LayerProp'):
                    selector = prop.get('filter', '')
                    gear = selector.split('And')[0] if path.name.endswith('_gear.xml') else None
                    if gear is not None and gear not in GEAR:
                        continue
                    condition = condition_id(selector)
                    if condition is None:
                        continue
                    palette = prop.get('palette', '')
                    if palette:
                        used_palettes.add(palette)
                    for entry in prop.findall('Surface'):
                        animation = entry.get('animsurface')
                        if animation not in vanilla:
                            continue
                        surface = surfaces[entry.get('name')]
                        if int(surface['directions']) != vanilla[animation][1]:
                            continue
                        original = base / surface['file'].replace('\\', '/')
                        # LOBOT's large-male bloody-death definition reuses
                        # 24-frame body layers from the normal death, while
                        # vanilla and its armour layers use 7 frames per facing.
                        # The matching bloody body layers exist in the pack.
                        if (body == 'BGM' and animation == 'BGMHITHARDBLOOD'
                                and original.name.upper().startswith('M_DIEHARD_')):
                            bloody = original.with_name('M_DIEHARDB_' + original.name[len('M_DIEHARD_'):])
                            if bloody.exists():
                                original = bloody
                        relative = Path('sprites') / body / original.name
                        output = destination / relative
                        output.parent.mkdir(parents=True, exist_ok=True)
                        if relative not in copied:
                            shutil.copy2(original, output)
                            copied.add(relative)
                        mapping = {'body': body, 'animation': animation, 'base_sprite': vanilla[animation][0],
                                   'layer': layer_name, 'condition': condition,
                                   'sprite': 'visual-equipment/' + relative.as_posix(),
                                   'palette': 'visual-equipment/palettes/' + Path(palettes[palette]).name if palette else '',
                                   'shadow': layers[layer_name]['shadow'] == '1', 'gear': gear or ''}
                        # Vanilla female throws have 14 frames per facing;
                        # LOBOT's corresponding overarm throw has 18. Explicitly
                        # retime that complete set of layers, not other sheets.
                        if body == 'RGF' and animation == 'RGFTHROW':
                            mapping['frame_remap'] = {'base_frames_per_direction': 14,
                                                      'sprite_frames_per_direction': 18,
                                                      'directions': 8}
                        if gear:
                            mapping['slot'], mapping['items'] = GEAR[gear]
                        mappings.append(mapping)
    (destination / 'palettes').mkdir(exist_ok=True)
    for palette in used_palettes:
        original = base / palettes[palette].replace('\\', '/')
        shutil.copy2(original, destination / 'palettes' / original.name)
    manifest = {'enabled': True, 'held_weapons': True,
                'source': 'JA2 1.13 Base LOBOT artwork, palettes and animation mappings',
                'layer_order': layer_order, 'conditions': conditions, 'mappings': mappings}
    (destination / 'vanilla-gear.json').write_text(json.dumps(manifest, indent=2) + '\n')
    report = {'sprites': len(copied), 'mappings': len(mappings), 'conditions': len(conditions),
              'worn_items': {name: {'slot': slot, 'items': items} for name, (slot, items) in GEAR.items()},
              'notes': ['T-shirts retain the merc shirt artwork.',
                        'Leather jackets use long sleeves with the merc clothing palette, as in LOBOT.',
                        'Ceramic plates and armour treatments have no separate external layer.',
                        'Creature hides are not human wearable equipment.',
                        'Missing or incompatible animation art falls back to vanilla.',
                        'One-handed SMGs use LOBOT machine-pistol art when no specific one-handed sheet exists.',
                        'M14COPY uses the vanilla M14 visual identity; 1.13 item 331 is a different rifle.']}
    (destination / 'coverage.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Imported {len(copied)} sprites, {len(mappings)} mappings and {len(conditions)} hand conditions.')


if __name__ == '__main__':
    main()
