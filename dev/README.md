# Custom JA2 development

## Git history and local assets

The `custom/vanilla-enhancements` branch starts at upstream tag `v0.22.1`.
`upstream` points to the original Stracciatella repository; `origin` points to
the personal GitHub fork. Existing customizations were imported as a snapshot;
their original editing sequence is not reconstructed history.

Source, configuration, visual mappings, import scripts and tests are tracked.
`_bin`, Python caches and imported 1.13 sprite/palette binaries are ignored.
The original JA2 game data and save games remain outside this repository.
On a fresh checkout, recreate the cosmetic assets before building:

```powershell
python dev/import_visual_equipment.py "F:\JA2 1.13 game files"
python dev/import_nwss.py "F:\JA2 1.13 game files"
```

Use `git status` and `git diff` to review changes before each commit. Keep new
features and fixes in separate commits so they can be compared or reverted.

## New Weapon Sound System (NWSS)

The 1.13 NWSS audio path is adapted for vanilla Stracciatella weapons. The
importer copies 1.13's `Base/AltSounds` plus individual-shot recordings for
the missing .38/.357/.45 calibre folders. It creates sound-only mappings for
38 vanilla guns (including the M14 copy). Rocket rifles, dart guns, flame
weapons, knives and explosives keep their original dedicated audio.

`assets/externalized/nwss/config.json` enables/disables the system and controls
shot, echo, room, casing and empty-magazine playback volumes. Rebuild after
editing that source configuration. To adjust the current build immediately,
edit `_bin/build/play/externalized/nwss/config.json` and restart the game.
Set `enabled` to false for original weapon audio. The audio
binaries are local/ignored and recreated by `dev/import_nwss.py`; source paths
are recorded in `nwss/coverage.json`. Attribution: sevenfm's 1.13 NWSS and the
sound authors credited by the 1.13 asset pack. System/modding description:
https://thepit.ja-galaxy-forum.com/index.php?prevloaded=1&start=40&t=msg&th=24289

Every actual shot plays once, including individual burst bullets and both
hands. Burst echoes occur at the last shot, including a shortened magazine.
Room/underground effects and town/hill/forest/open-area echoes use the existing
map data. Suppressed shots omit acoustic effects. Cases play quietly on hard
ground/roofs with a 50% chance; revolvers and manually cycled vanilla guns do
not eject cases automatically. Missing samples retain original playback. Burst
fallback is selected once to avoid mixing two complete firing sound systems.

The mixer has 64 channels for layered shots plus voices and ambient sounds.
Sound variation uses its own random generator. AP, ammunition, simulation
noise, damage, accuracy and both combat random streams are unchanged.

`Nwss.*` and `NwssAssets.*` cover resolution/fallback, suppression, burst
boundaries, surface effects, all mappings, actual Ogg decoding/mixing and
combat-state/RNG preservation. Mixer tests use SDL's silent dummy device.
In-game, compare pistol/rifle shots, short/full bursts, dual pistols, silenced
fire and indoor/outdoor/rooftop/underground acoustics using a separate save.

## LOBOT hit, death and throw animations

Complete body replacements remain active when an animation intentionally hides
held weapons. Missing weapon art on a sheet that draws weapons still uses the
original fallback. The importer selects the matching 7-frame-per-facing bloody
death layers for large males. Female overarm throws explicitly retime LOBOT's
18 frames per facing onto vanilla's 14-frame timeline, preserving directions
and endpoints. Other frame-count mismatches remain rejected.

Regression checks cover all frames of nine hit/death/throw animations for all
four supported body types with rifles, dual pistols, and grenades. Pixel checks
exercise normal and multi-tile world rendering in every direction.

## Voluntary rooftop positions

Alerted enemy soldiers and militia can consider nearby rooftop landing tiles
as firing positions. This supplements the existing pursuit/climbing AI.
The `ai.rooftop_position_preference` value in `assets/externalized/game.json`
defaults to 5 (range 0–20); 0 disables voluntary climbing.

The search uses recent personal/team sightings, tests firing sight lines and
bullet obstruction in both directions, and charges approach/climb AP. It keeps
enough AP for a shot and rejects substantially more exposed approaches or
landings. It runs at the start of a turn, within six tiles of a climb point,
and does not override emergency actions, stationary/scripted guards, or a
reasonably good immediate attack. Existing ground cover is an alternative
when the BLACK AI has already found one. It does not reserve roofs or change
save-game data. `RooftopAI.*` tests cover scoring, eligibility, enemy memory,
AP reserves, and the real firing-geometry evaluation.

For an in-game check, use a separate save in a surface sector with flat roofs.
Alert armed patrols or militia to enemies near a building. Observe whether
they use useful roof edges when their ground shot is blocked or poor, retain
AP to shoot, and avoid climbing after taking fire. Try the same encounter
with preference 0 to compare. The debug log records selected climb locations.

## Selected upstream bug fixes

Three fixes are cherry-picked with upstream authorship and original commit IDs:

- `e2fcb2d13`: clear a reused autoresolve hit slot's damage when scheduling a
  shot, so a miss cannot inherit an earlier bullet's damage.
- `eab886c41`: clamp negative hit accuracy and random-bonus ranges, preventing
  unsigned wraparound and inflated damage against unconscious or high-defence targets.
- `445451514`: clear the previous-meanwhile flag when loading a sector or
  initializing tactical saves, so a cutscene cannot suppress the next sector save.

The regular test command includes autoresolve slot/accuracy regressions and
sector-save flag regressions. Saving tests create an isolated temporary
filesystem rather than operating on the player's campaign files.

## Visual equipment

Vanilla worn equipment and held weapons use JA2 1.13 LOBOT artwork on regular/
stocky male, large male and female tactical sprites. Equipment changes appear
immediately. Item statistics, AP costs, combat rules, inventory and saves are
unchanged by this visual feature.

Supported worn items are Steel/Kevlar/Spectra helmets; Flak/Kevlar/Spectra/
Guardian vests; Kevlar/Spectra leggings; leather jacket sleeves; night and UV
goggles; sunglasses; gas masks; and extended ears. Treated armour variants share
their base family's appearance. Leather sleeves retain the merc's clothing
colour, as in LOBOT. T-shirts retain the merc shirt artwork. Ceramic plates are
inside the vest and have no separate external layer. Creature hides are not
human wearable equipment.

Held weapons use specific sheets where provided by LOBOT, with its generic
weapon-family sheets otherwise. This includes both hands when dual wielding.
To remove the original generic gun cleanly, the renderer replaces the full
coloured body with compatible LOBOT body parts before adding gear and weapons.
If body parts or weapon frames are missing/incompatible, it keeps the original
sprite and applies only compatible worn overlays. This leaves vanilla animation
timing intact. Some rare animations therefore have no new equipment visual.

The imported artwork and mappings are in `assets/externalized/visual-equipment`.
Set `enabled` to `false` in `vanilla-gear.json`, rebuild and restart to disable it.
Set `held_weapons` to `false` for worn overlays on original vanilla sprites only.
The build copies this data into the playable directory. No 1.13 installation is
needed at runtime. `dev/import_visual_equipment.py` reproduces the import from
the 1.13 game folder; it imports artwork, palettes and visual mappings only.
`coverage.json` records the vanilla item groups. The old Steel Helmet prototype
files are superseded by this manifest. Decoded sprite sheets use a 128 MiB cache;
active frames remain valid during eviction.

## Visible bullet trails

Ordinary gunfire and buckshot use the alternate bullet graphics approach from
JA2 1.13 (`Tactical/Bullets.cpp`, afp's trail implementation): repeat the existing
bullet sprite along its latest movement segment instead of displaying a single
dot and ground shadow. No additional artwork import is required. Both sides use
the effect; knives, rockets, flames and creature spit retain their own graphics.

The trail is capped at 60 samples, follows projectile height, and uses ordinary
world depth rendering. Each bullet owns its visual nodes, which are removed on
movement, impact, stopping, loading or sector teardown. The trail persists between
timed projectile updates. No accuracy, damage, ammunition, lighting, visibility,
projectile speed or save-format rules are changed.

Playtest single shots, bursts and buckshot in several directions, including
rooftop shots and shots into cover. Check that streaks vanish after impacts and
that crossing shots do not remove one another's visuals.

## Maximum default aim

In turn-based combat, selecting a ranged target starts at the highest affordable
aim level (up to the vanilla maximum of four extra aiming AP). The calculation
includes turning and readying the weapon. If there are insufficient AP for any
extra aim, the original minimum is used. Burst fire and real-time firing retain
their existing behavior. Throwing knives also use maximum affordable aim.

The existing right-click aim cycle still works: at maximum, right-click twice
to return to minimum, then add aim normally. No save format was changed.

## Quieter acknowledgements

Rubber-band selection, Alt-click multi-selection, multi-selection movement and
all-squad movement request one reply from a randomly chosen eligible merc.
Movement replies come only from mercs who received a valid path. The change
also removes the extra selection reply when the active merc changes within a
group. Group size does not increase the chance of a reply.

Selection/attention and OK acknowledgements (including low-morale variants)
now pass a 50% playback gate. Existing cooldowns and dialogue rules still apply.
Individual and group acknowledgement voices both use the quieter volume level.
The mute-confirmations option takes precedence. Hits, deaths, enemy alerts,
locked-door warnings and other battle sounds retain their existing behavior.
No orders, paths, AP costs or combat rules are changed.

To playtest, select several mercs with a selection box or Alt-click, then issue
several movement orders. Expect at most one reply per action, with roughly half
the actions silent; the speaker should vary. Try single-merc orders too. Check
mute confirmations, and try a group with a blocked path: a merc unable to move
should not provide the movement acknowledgement.

## Tactical mouse-wheel zoom

Scroll up over the battlefield to zoom in; scroll down to zoom out. Zoom runs
from 100% to 300% in 25% steps, centred on the current view. It lasts for the
session and is remembered when returning from the map or laptop. The bottom
panel and top message bar keep their normal size. The cursor stays at native
size and moves smoothly; battlefield clicks and cursor warps use the zoom
transform, while HUD clicks and edge scrolling use displayed coordinates.
Camera limits follow the visible crop at each zoom level. Zooming out clamps
the camera to the wider view before rendering, so it stays inside the map.

The wheel is consumed only over a loaded tactical battlefield. Other screens
and the bottom panel retain their previous wheel handling. This enlarges the
existing rendered battlefield without changing world coordinates or combat.

Playtest selecting mercs, firing and plotting paths at several zoom levels,
drag selection, screen-edge and arrow-key scrolling, inventory panel changes,
and opening/closing the map and laptop.

## Map and laptop scaling

The strategic map and laptop enlarge their centered 640x480 interface using
nearest-neighbour scaling. Tactical rendering keeps the full native resolution.
Mouse motion, clicks, touch positions, cursor warps and the software cursor use
the same coordinate transform. Confirmation dialogs inherit the parent scale.

The default selects the largest whole-number scale that fits, up to 3x. At
1920x1080 this is 2x (a 1280x960 interface). A complete 3x interface is 1920x1440,
so it requires at least that much internal resolution. Oversized requests fall
back to the largest fitting scale; controls are never cropped to force 3x.

While on the map or laptop, Ctrl+Alt+1/2/3 requests 1x/2x/3x for the current
session. These shortcuts do not affect tactical mode. The startup preference is
`scale` in `assets/externalized/interface-scaling.json`; build after editing it
to copy it into the playable directory. No source rebuild is required for that
data-only setting.

Playtest opening/closing the laptop and strategic map, selecting sectors and
mercs, entering a laptop site and dismissing a confirmation dialog. Hover/click
targets should follow the enlarged graphics, including after changing scale.

## Build and play

- Double-click `Build Custom JA2.cmd` in the project root to rebuild.
- Double-click `Play Custom JA2.cmd` to run `_bin/build/play/ja2.exe` against the
  original assets at `F:\GOG Games\Jagged Alliance 2`.
- The custom launcher uses a 1920x1080 window through Stracciatella's native
  `-window -res 1920x1080` options. Changing these options requires no rebuild.
- The launcher uses the normal Stracciatella preferences and saves directory.
  Use a separate save slot when testing the custom build.
- The original GOG executable and assets are not replaced.

`dev/build.ps1` configures CMake/Ninja with the installed Visual Studio C++
14.44 tools and a project-local Rust 1.75.0 installation. Downloaded dependencies,
compiler products, and the original UI_Cursors.cc backup live under `_bin`.
`dev/patch.cmd` applies the source project's bundled dependency patch using
native Git, avoiding an MSYS runtime requirement on Windows.

## Verify

After building, double-click `Test Visual Gear.cmd` to run the rendering, aim,
acknowledgement and interface-scaling regressions and generate `_bin/gear-tests.xml` and
`_bin/vanilla-gear-preview.bmp`.
This validates animation frames and a simulated wall/viewport at 1920x1080;
it does not drive an in-game save. It also checks scanline padding for clipping
overruns and compares foreground depth with ordinary/obscured rendering.
The prone regression additionally runs the actual world renderer's multi-tile
merc path for every prone frame/direction and compares the framebuffer with the
replacement composite. This catches the original body being drawn underneath.
Scaling checks also run SDL's real video-refresh path with a headless software
renderer, checking the enlarged interface, cursor in black margins, parent
dialogs, scale shortcuts and restoration of the full tactical canvas.
Run `dev\test-visual-equipment.ps1 -OpenGL` to repeat these checks using the
desktop OpenGL renderer. This also covers the SDL 2.0.20 startup regression:
the display texture uses ARGB8888 with RGB565 uploads converted on lock, since
changing the filter on an OpenGL RGB565 conversion texture crashes that SDL version.

From PowerShell in the project folder:

```powershell
$env:GTEST_FILTER = 'VisualEquipment.*:VanillaGearAssets.*:DefaultTargetAimTest.*:MercAcknowledgement.*:InterfaceScaling.*'
$env:JA2_VISUAL_TEST_GAMEDIR = 'F:\GOG Games\Jagged Alliance 2'
Push-Location .\_bin\build\play
try { .\ja2.exe -gamedir $env:JA2_VISUAL_TEST_GAMEDIR -unittests }
finally { Pop-Location }
Remove-Item Env:GTEST_FILTER
Remove-Item Env:JA2_VISUAL_TEST_GAMEDIR
```

For an optional rendered before/after contact sheet, set
`JA2_VISUAL_TEST_OUTPUT` to an absolute BMP filename before running the tests.
Rows are regular male, large male and female, each standing, crouched and prone
with a Desert Eagle, then the same stances with a Desert Eagle/Beretta pair.
Each direction has an unarmoured/armoured pair using the new weapon renderer.

For an in-game check, load an existing save and try a complete armour set plus
two compatible face items. Rotate, walk, run, crouch, go prone and aim. Remove
items and swap guns, including mixed dual handguns. Carrying gear in a pocket
must not wear it. Check movement past walls/mercs, night lighting, selection
effects, and scrolling for leftover pixels. Use a separate save slot for testing.

In a turn-based fight, select a merc with sufficient AP and aim at a target.
Check that aim begins at maximum and that its displayed AP cost includes aiming.
Repeat after spending most AP: aim should begin at the best remaining affordable
level. Right-click through the existing cycle to choose a lower level. Check
another target and a second shot, and confirm that burst fire works normally.
