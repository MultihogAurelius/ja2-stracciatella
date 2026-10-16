#include "Merc_Acknowledgement.h"
#include "Random.h"

bool IsMercAcknowledgement(BattleSound sound)
{
	return sound == BATTLE_SOUND_OK1 || sound == BATTLE_SOUND_OK2 ||
		sound == BATTLE_SOUND_ATTN1 ||
		sound == BATTLE_SOUND_LOWMORALE_OK1 || sound == BATTLE_SOUND_LOWMORALE_OK2 ||
		sound == BATTLE_SOUND_LOWMORALE_ATTN1;
}

bool ShouldPlayMercBattleSound(BattleSound sound, bool muteConfirmations, UINT32 acknowledgementRoll)
{
	// Only routine selection/order replies get the new 50% gate.
	return !IsMercAcknowledgement(sound) || (!muteConfirmations && acknowledgementRoll == 0);
}

SOLDIERTYPE* PickMercAcknowledgementSpeaker(std::vector<SOLDIERTYPE*> const& group, UINT32 (*pickIndex)(UINT32))
{
	std::vector<SOLDIERTYPE*> eligible;
	for (auto* soldier : group)
	{
		if (!soldier || !soldier->bActive || !soldier->bInSector || soldier->bLife < OKLIFE ||
			soldier->fMercAsleep || (soldier->uiStatusFlags & SOLDIER_MUTE)) continue;
		eligible.push_back(soldier);
	}
	return eligible.empty() ? nullptr : eligible[pickIndex(static_cast<UINT32>(eligible.size()))];
}

void DoMercGroupAcknowledgement(std::vector<SOLDIERTYPE*> const& group, BattleSound sound, bool lowerVolume)
{
	// One request for the whole action. The shared playback function applies
	// the frequency gate once, independent of the size of the selection.
	if (auto* speaker = PickMercAcknowledgementSpeaker(group, Random))
		InternalDoMercBattleSound(speaker, sound, lowerVolume);
}
