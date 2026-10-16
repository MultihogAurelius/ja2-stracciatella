#include "gtest/gtest.h"
#include "Merc_Acknowledgement.h"
#include "GameSettings.h"
#include "Random.h"
#include <array>

namespace
{
UINT32 chosenIndex{}, requestedRange{}, randomCalls{};
UINT32 PickIndex(UINT32 range)
{
	requestedRange = range;
	++randomCalls;
	return chosenIndex;
}
SOLDIERTYPE AwakeMerc()
{
	SOLDIERTYPE merc{};
	merc.bActive = TRUE; merc.bInSector = TRUE; merc.bLife = 100;
	return merc;
}
}

TEST(MercAcknowledgement, SelectionAndOrderRepliesHaveHalfTheOpportunities)
{
	for (auto sound : {BATTLE_SOUND_OK1,BATTLE_SOUND_OK2,BATTLE_SOUND_ATTN1,
		BATTLE_SOUND_LOWMORALE_OK1,BATTLE_SOUND_LOWMORALE_OK2,BATTLE_SOUND_LOWMORALE_ATTN1})
	{
		EXPECT_TRUE(ShouldPlayMercBattleSound(sound,false,0));
		EXPECT_FALSE(ShouldPlayMercBattleSound(sound,false,1));
		EXPECT_FALSE(ShouldPlayMercBattleSound(sound,true,0));
		EXPECT_FALSE(ShouldPlayMercBattleSound(sound,true,1));
	}
}

TEST(MercAcknowledgement, OtherBattleSoundsAreUnaffectedByConfirmationSettings)
{
	for (int i = 0; i < NUM_MERC_BATTLE_SOUNDS; ++i)
	{
		auto sound = static_cast<BattleSound>(i);
		if (IsMercAcknowledgement(sound)) continue;
		for (bool muted : {false,true})
		for (UINT32 roll : {0,1}) EXPECT_TRUE(ShouldPlayMercBattleSound(sound,muted,roll)) << i;
	}
}

TEST(MercAcknowledgement, GroupChoosesExactlyOneEligibleSpeaker)
{
	std::array<SOLDIERTYPE,8> mercs;
	for (auto& merc : mercs) merc = AwakeMerc();
	mercs[3].uiStatusFlags |= SOLDIER_MUTE;
	mercs[4].fMercAsleep = TRUE;
	mercs[5].bLife = OKLIFE - 1;
	mercs[6].bActive = FALSE;
	mercs[7].bInSector = FALSE;
	std::vector<SOLDIERTYPE*> group{nullptr};
	for (auto& merc : mercs) group.push_back(&merc);
	for (chosenIndex = 0; chosenIndex < 3; ++chosenIndex)
	{
		randomCalls = 0;
		EXPECT_EQ(PickMercAcknowledgementSpeaker(group,PickIndex),&mercs[chosenIndex]);
		EXPECT_EQ(requestedRange,3);
		EXPECT_EQ(randomCalls,1);
	}
}

TEST(MercAcknowledgement, EveryGroupMemberHasTheSameChance)
{
	std::array<SOLDIERTYPE,6> mercs;
	std::vector<SOLDIERTYPE*> group;
	for (auto& merc : mercs) { merc = AwakeMerc(); group.push_back(&merc); }
	std::array<int,6> picked{};
	for (chosenIndex = 0; chosenIndex < mercs.size(); ++chosenIndex)
	{
		auto* speaker = PickMercAcknowledgementSpeaker(group,PickIndex);
		ASSERT_NE(speaker,nullptr);
		++picked[speaker - mercs.data()];
		EXPECT_EQ(requestedRange,mercs.size());
	}
	for (int count : picked) EXPECT_EQ(count,1);
}

TEST(MercAcknowledgement, EmptyOrIneligibleGroupsDoNotRequestASpeaker)
{
	randomCalls = 0;
	EXPECT_EQ(PickMercAcknowledgementSpeaker({},PickIndex),nullptr);
	auto merc = AwakeMerc(); merc.uiStatusFlags |= SOLDIER_MUTE;
	EXPECT_EQ(PickMercAcknowledgementSpeaker({nullptr,&merc},PickIndex),nullptr);
	EXPECT_EQ(randomCalls,0);
}

TEST(MercAcknowledgement, AGroupWithOneMemberStillUsesTheFrequencyGate)
{
	auto merc = AwakeMerc(); chosenIndex = 0;
	EXPECT_EQ(PickMercAcknowledgementSpeaker({&merc},PickIndex),&merc);
	EXPECT_EQ(requestedRange,1);
	EXPECT_TRUE(ShouldPlayMercBattleSound(BATTLE_SOUND_OK1,false,0));
	EXPECT_FALSE(ShouldPlayMercBattleSound(BATTLE_SOUND_OK1,false,1));
}

TEST(MercAcknowledgement, PlaybackRollsOncePerGroupAndSuppressionDoesNotTouchCooldowns)
{
	struct Restore
	{
		std::mt19937 random = gRandomEngine;
		BOOLEAN muted = gGameSettings.fOptions[TOPTION_MUTE_CONFIRMATIONS];
		~Restore() { gRandomEngine = random; gGameSettings.fOptions[TOPTION_MUTE_CONFIRMATIONS] = muted; }
	} restore;
	gGameSettings.fOptions[TOPTION_MUTE_CONFIRMATIONS] = FALSE;
	std::array<SOLDIERTYPE,6> mercs;
	for (auto& merc : mercs)
	{
		merc = AwakeMerc(); merc.bOldBattleSnd = BATTLE_SOUND_COOL1; merc.uiTimeSameBattleSndDone = 1234;
	}
	for (UINT32 size : {1,3,6})
	{
		std::vector<SOLDIERTYPE*> group;
		for (UINT32 i = 0; i < size; ++i) group.push_back(&mercs[i]);
		std::mt19937 before, after;
		bool found = false;
		for (UINT32 seed = 0; seed < 256; ++seed)
		{
			gRandomEngine.seed(seed); before = gRandomEngine;
			Random(size); // One speaker choice.
			if (Random(2) != 1) continue; // Choose a suppressed reply, avoiding audio I/O.
			after = gRandomEngine; found = true; break;
		}
		ASSERT_TRUE(found);
		gRandomEngine = before;
		DoMercGroupAcknowledgement(group,BATTLE_SOUND_OK1,true);
		EXPECT_TRUE(gRandomEngine == after) << "Group size " << size;
		for (auto const& merc : mercs)
		{
			EXPECT_EQ(merc.bOldBattleSnd,BATTLE_SOUND_COOL1);
			EXPECT_EQ(merc.uiTimeSameBattleSndDone,1234);
		}
	}
}
