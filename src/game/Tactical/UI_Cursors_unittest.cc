#include "gtest/gtest.h"

#include "Animation_Control.h"
#include "DefaultContentManagerUT.h"
#include "GameInstance.h"
#include "Items.h"
#include "Isometric_Utils.h"
#include "Overhead.h"
#include "Points.h"
#include "Soldier_Control.h"
#include "UI_Cursors.h"

#include <memory>

class DefaultTargetAimTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		previousContent = GCM;
		previousFlags = gTacticalStatus.uiFlags;
		previousTarget = gUIFullTarget;
		previousCursor = guiCurrentCursorGridNo;
		previousReexamine = gfUIForceReExamineCursorData;
		previousEvent = guiPendingOverrideEvent;
		content.reset(DefaultContentManagerUT::createDefaultCMForTesting());
		ASSERT_TRUE(content->loadGameData());
		GCM = content.get();
		gTacticalStatus.uiFlags |= INCOMBAT;
		gUIFullTarget = NULL;
		guiCurrentCursorGridNo = 10001;

		soldier.ubProfile = NO_PROFILE;
		soldier.bTeam = OUR_TEAM;
		soldier.bLife = soldier.bLifeMax = 80;
		soldier.bAgility = soldier.bDexterity = soldier.bMarksmanship = 80;
		soldier.bExpLevel = 5;
		soldier.bBreath = soldier.bBreathMax = 100;
		soldier.usAnimState = STANDING;
		soldier.sGridNo = 10000;
		soldier.sLastTarget = NOWHERE;
		CreateItem(GLOCK_17, 100, &soldier.inv[HANDPOS]);
	}

	void TearDown() override
	{
		GCM = previousContent;
		gTacticalStatus.uiFlags = previousFlags;
		gUIFullTarget = previousTarget;
		guiCurrentCursorGridNo = previousCursor;
		gfUIForceReExamineCursorData = previousReexamine;
		guiPendingOverrideEvent = previousEvent;
	}

	SOLDIERTYPE soldier{};
	std::unique_ptr<DefaultContentManagerUT> content;
	ContentManager* previousContent{};
	UINT32 previousFlags{};
	SOLDIERTYPE* previousTarget{};
	GridNo previousCursor{};
	BOOLEAN previousReexamine{};
	UIEventKind previousEvent{};
};

TEST_F(DefaultTargetAimTest, StartsAtMaximumAndAllowsManualAdjustment)
{
	soldier.bActionPoints = 25;
	HandleLeftClickCursor(&soldier);
	EXPECT_EQ(soldier.bShownAimTime, REFINE_AIM_5);
	EXPECT_EQ(soldier.bActionPoints, 25); // Selecting aim does not spend AP.

	// The existing right-click cycle can still return to minimum aim.
	HandleRightClickAdjustCursor(&soldier, guiCurrentCursorGridNo);
	HandleRightClickAdjustCursor(&soldier, guiCurrentCursorGridNo);
	EXPECT_EQ(soldier.bShownAimTime, REFINE_AIM_1);
	HandleLeftClickCursor(&soldier);
	EXPECT_EQ(soldier.bShownAimTime, REFINE_AIM_5);
}

TEST_F(DefaultTargetAimTest, UsesHighestAffordableAimIncludingWeaponReadyCosts)
{
	INT16 const baseCost = CalcTotalAPsToAttack(&soldier, guiCurrentCursorGridNo, TRUE, 0);
	ASSERT_GT(baseCost, 0);
	for (INT8 extraAP = 0; extraAP <= 4; ++extraAP)
	{
		soldier.bActionPoints = baseCost + extraAP;
		HandleLeftClickCursor(&soldier);
		EXPECT_EQ(soldier.bShownAimTime, extraAP * 2) << "Extra AP: " << int(extraAP);
		EXPECT_TRUE(EnoughPoints(&soldier,
			CalcTotalAPsToAttack(&soldier, guiCurrentCursorGridNo, TRUE, soldier.bShownAimTime / 2), 0, FALSE));
	}

	soldier.bActionPoints = baseCost - 1;
	HandleLeftClickCursor(&soldier);
	EXPECT_EQ(soldier.bShownAimTime, REFINE_AIM_1);
}

TEST_F(DefaultTargetAimTest, UsesTheSelectedTargetsTile)
{
	SOLDIERTYPE target{};
	target.sGridNo = 10160;
	gUIFullTarget = &target;
	soldier.bActionPoints = CalcTotalAPsToAttack(&soldier, target.sGridNo, TRUE, 2);
	HandleLeftClickCursor(&soldier);
	EXPECT_EQ(soldier.bShownAimTime, REFINE_AIM_3);
}

TEST_F(DefaultTargetAimTest, DoesNotAddAimToBurstFire)
{
	CreateItem(G11, 100, &soldier.inv[HANDPOS]);
	soldier.bDoBurst = TRUE;
	soldier.bActionPoints = 25;
	HandleLeftClickCursor(&soldier);
	EXPECT_EQ(soldier.bShownAimTime, REFINE_AIM_1);
}

TEST_F(DefaultTargetAimTest, AlsoAppliesToThrowingKnives)
{
	CreateItem(THROWING_KNIFE, 100, &soldier.inv[HANDPOS]);
	soldier.bActionPoints = 25;
	HandleLeftClickCursor(&soldier);
	EXPECT_EQ(soldier.bShownAimTime, REFINE_AIM_5);
}

TEST_F(DefaultTargetAimTest, PreservesRealtimeImmediateFire)
{
	gTacticalStatus.uiFlags &= ~INCOMBAT;
	soldier.bShownAimTime = REFINE_AIM_1;
	HandleLeftClickCursor(&soldier);
	EXPECT_EQ(guiPendingOverrideEvent, CA_MERC_SHOOT);
	EXPECT_EQ(soldier.bShownAimTime, REFINE_AIM_1);
}
