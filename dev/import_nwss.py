"""Import 1.13 NWSS audio and map it to vanilla guns without changing weapon stats."""
import argparse
import json
from pathlib import Path
import shutil


CALIBRES = {
    'AMMO38': '38 special', 'AMMO9': '9x19mm', 'AMMO45': '45 acp',
    'AMMO357': '357 magnum', 'AMMO12G': '12 gauge', 'AMMOCAWS': 'caws',
    'AMMO545': '5.45x39mm', 'AMMO556': '5.56x45mm',
    'AMMO762N': '7.62x51mm', 'AMMO762W': '7.62x39mm',
    'AMMO47': '4.7x33mm', 'AMMO57': '5.7x28mm',
}
TYPES = {'PISTOL': 'pistol', 'M_PISTOL': 'mp', 'SMG': 'smg',
         'RIFLE': 'rifle', 'SN_RIFLE': 'sniper', 'ASRIFLE': 'ar',
         'LMG': 'lmg', 'SHOTGUN': 'shotgun'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    base = args.game / 'Base'
    target = root / 'assets/externalized/nwss'
    audio = target / 'audio'
    sources = {}

    def copy(source, relative):
        destination = audio / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        sources[relative] = source.relative_to(base).as_posix()

    for source in sorted((base / 'AltSounds').rglob('*.ogg')):
        copy(source, source.relative_to(base / 'AltSounds').as_posix().lower())

    # These vanilla calibres are absent from the supplied NWSS library.
    # Use the same installation's individual-shot recordings (no stat imports).
    supplements = {
        'caliber/38 special/pistol_single_noecho.ogg': 'Sounds/Weapons/38/38 SPECIAL REVOLVER.ogg',
        'caliber/357 magnum/revolver_single_noecho.ogg': 'Sounds/Weapons/357/357 REVOLVER SINGLE SHOT.ogg',
        'caliber/357 magnum/pistol_single_noecho.ogg': 'Sounds/Weapons/357/357 DE SINGLE SHOT.ogg',
        'caliber/45 acp/pistol_single_noecho.ogg': 'Sounds/Weapons/45/45 CALIBER PISTOL SINGLE SHOT.ogg',
        'caliber/45 acp/smg_loop.ogg': 'Sounds/Weapons/45/45 CALIBER SMG SINGLE SHOT.ogg',
    }
    for relative, source in supplements.items():
        copy(base / source, relative)
    # A short suppressed shot, rather than an unsuppressed fallback for .45.
    copy(base / 'AltSounds/Caliber/9x19mm/loop_s.ogg', 'caliber/45 acp/loop_s.ogg')

    definitions = []
    for weapon in json.loads((root / 'assets/externalized/weapons.json').read_text()):
        calibre = CALIBRES.get(weapon.get('calibre'))
        kind = TYPES.get(weapon['internalType'])
        if not calibre or not kind:
            continue
        name = weapon['internalName']
        definition = {'item': weapon['itemIndex'], 'name': name,
                      'calibre': calibre, 'type': kind,
                      'automatic': weapon.get('ubShotsPerBurst', 0) > 0,
                      'cases': name not in ('SW38', 'BARRACUDA', 'M24', 'M870'),
                      'last_round': name not in ('SW38', 'BARRACUDA', 'M24', 'M870')}
        if name == 'BARRACUDA':
            definition['sound_name'] = 'revolver'
        if name == 'TYPE85':
            definition['calibre'] = '7.62x25mm'
        if name == 'DRAGUNOV':
            definition['calibre'] = '7.62x54mmr'
        definitions.append(definition)
    config = {'enabled': True,
              'source': 'JA2 1.13 NWSS / sevenfm, with matching 1.13 shot recordings for missing vanilla calibres',
              'shot_volume': 127, 'echo_volume': 85, 'room_volume': 85,
              'casing_volume': 25, 'empty_volume': 50, 'casing_chance_percent': 50,
              'weapons': definitions}
    target.mkdir(parents=True, exist_ok=True)
    (target / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
    (target / 'coverage.json').write_text(json.dumps(
        {'weapons': len(definitions), 'audio_files': len(sources),
         'sources': dict(sorted(sources.items()))}, indent=2) + '\n')
    print(f'Imported {len(sources)} audio files for {len(definitions)} vanilla guns.')


if __name__ == '__main__':
    main()
