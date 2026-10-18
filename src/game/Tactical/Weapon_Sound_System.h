#pragma once

#include "JA2Types.h"

// True when NWSS owns this shot's audio; otherwise retain vanilla playback.
// This operates only on presentation, never AP, ammunition, noise, or hit rolls.
bool PlayNewWeaponSound(SOLDIERTYPE& soldier);
void ResetNewWeaponSounds();
