#pragma once

#include "Soldier_Control.h"
#include <vector>

bool IsMercAcknowledgement(BattleSound);
bool ShouldPlayMercBattleSound(BattleSound, bool muteConfirmations, UINT32 acknowledgementRoll);
SOLDIERTYPE* PickMercAcknowledgementSpeaker(std::vector<SOLDIERTYPE*> const&, UINT32 (*pickIndex)(UINT32));
void DoMercGroupAcknowledgement(std::vector<SOLDIERTYPE*> const&, BattleSound, bool lowerVolume);
