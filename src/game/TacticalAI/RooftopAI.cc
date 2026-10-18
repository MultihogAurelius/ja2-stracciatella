#include "RooftopAI.h"

#include "AI.h"
#include "AIInternals.h"
#include "Animation_Control.h"
#include "Buildings.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "GamePolicy.h"
#include "Isometric_Utils.h"
#include "Items.h"
#include "LOS.h"
#include "Logger.h"
#include "OppList.h"
#include "Overhead.h"
#include "PathAI.h"
#include "Points.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "Soldier_Profile.h"
#include "StrategicMap.h"
#include "Structure.h"
#include "Weapons.h"

#include <algorithm>
#include <array>
#include <vector>

namespace
{
constexpr int ROOFTOP_SEARCH_RADIUS = 6;
constexpr int MAX_ROOFTOP_ROUTES = 8;
constexpr int MIN_ROOFTOP_SHOT = 15;
constexpr int MIN_POSITION_GAIN = 5;
constexpr int MAX_EXTRA_EXPOSURE = 15;

struct KnownRoofThreat
{
	const SOLDIERTYPE* soldier;
	INT16 grid;
	INT8 level;
};

struct RoofPositionValue
{
	int shot = 0;
	int exposure = 0;
	int Score() const { return shot - exposure / 2; }
};

struct RoofRoute
{
	INT16 ground;
	INT16 roof;
	int cost;
};

bool RooftopSoldierEligible(const SOLDIERTYPE& s, bool turnBased, bool interrupt, int preference)
{
	return preference > 0 && turnBased && !interrupt &&
		(s.bTeam == ENEMY_TEAM || s.bTeam == MILITIA_TEAM) &&
		IS_MERC_BODY_TYPE(&s) && !(s.uiStatusFlags & (SOLDIER_PC | SOLDIER_BOXER)) &&
		s.bLevel == 0 && s.bOrders != STATIONARY && s.ubProfile == NO_PROFILE &&
		s.bAlertStatus >= STATUS_RED && s.bAIMorale >= MORALE_NORMAL &&
		!s.bUnderFire && !s.bShock && !s.bCollapsed &&
		s.bLife >= OKLIFE && s.bLife >= s.bLifeMax / 2 && s.bBreath >= 50 &&
		s.bActionPoints > 0 && s.bActionPoints >= s.bInitialActionPoints;
}

// Use exactly the same personal/public knowledge precedence as the cover AI.
// Neither current enemy coordinates nor sounds alone supply rooftop targets.
bool GetKnownRoofThreat(const SOLDIERTYPE& s, const SOLDIERTYPE& opponent, KnownRoofThreat& threat)
{
	if (opponent.bLife < OKLIFE || CONSIDERED_NEUTRAL(&s, &opponent) || s.bSide == opponent.bSide)
		return false;
	const INT8 personal = s.bOppList[opponent.ubID];
	const INT8 shared = gbPublicOpplist[s.bTeam][opponent.ubID];
	const bool usePersonal = personal == shared ||
		gubKnowledgeValue[shared - OLDEST_HEARD_VALUE][personal - OLDEST_HEARD_VALUE] > 0;
	const INT8 knowledge = usePersonal ? personal : shared;
	if (knowledge < SEEN_CURRENTLY || knowledge > SEEN_LAST_TURN) return false;
	threat = {&opponent,
		usePersonal ? gsLastKnownOppLoc[s.ubID][opponent.ubID] : gsPublicLastKnownOppLoc[s.bTeam][opponent.ubID],
		usePersonal ? gbLastKnownOppLevel[s.ubID][opponent.ubID] : gbPublicLastKnownOppLevel[s.bTeam][opponent.ubID]};
	return threat.grid >= 0 && threat.grid < WORLD_MAX && threat.level >= 0 && threat.level <= 1;
}

void PlaceRoofProjection(SOLDIERTYPE& s, INT16 grid, INT8 level)
{
	s.sGridNo = grid;
	s.bLevel = level;
	s.dXPos = CenterX(grid);
	s.dYPos = CenterY(grid);
	s.usAnimState = STANDING;
	s.bDoBurst = 0;
	s.ubAttackingHand = HANDPOS;
	s.usAttackingWeapon = s.inv[HANDPOS].usItem;
	s.bWeaponMode = WM_NORMAL;
	s.bTargetCubeLevel = 0;
	s.CTGTTarget = nullptr;
}

int RoofShotValue(SOLDIERTYPE& shooter, INT16 target, INT8 level, const SOLDIERTYPE* targetSoldier)
{
	if (!SoldierToVirtualSoldierLineOfSightTest(&shooter, target, level, ANIM_STAND,
		MaxDistanceVisible(), TRUE)) return 0;
	shooter.bTargetLevel = level;
	const int accuracy = CalcChanceToHitGun(&shooter, target, 0, AIM_SHOT_TORSO, FALSE);
	const int through = SoldierToLocationChanceToGetThrough(&shooter, target, level, 2, targetSoldier);
	return accuracy * through / 100;
}

RoofPositionValue EvaluateRoofPosition(const SOLDIERTYPE& soldier, INT16 grid, INT8 level,
	const std::vector<KnownRoofThreat>& threats)
{
	// All projections are local copies. Evaluating a tile must not teleport the
	// real soldier, change its target, wear attachments, or update enemy memory.
	SOLDIERTYPE projected = soldier;
	PlaceRoofProjection(projected, grid, level);
	RoofPositionValue value;
	for (const KnownRoofThreat& threat : threats)
	{
		value.shot = std::max(value.shot, RoofShotValue(projected, threat.grid, threat.level, threat.soldier));
		if (!GCM->getItem(threat.soldier->inv[HANDPOS].usItem)->isGun()) continue;
		SOLDIERTYPE opponent = *threat.soldier;
		PlaceRoofProjection(opponent, threat.grid, threat.level);
		value.exposure += RoofShotValue(opponent, grid, level, &soldier);
	}
	if (!threats.empty()) value.exposure /= static_cast<int>(threats.size());
	return value;
}

// Scores use effective hit-chance points. A small preference breaks close
// decisions, while travel cost and an exposure ceiling prevent blind climbing.
int RoofRouteScore(const RoofPositionValue& current, const RoofPositionValue& approach,
	const RoofPositionValue& roof, int alternativeScore, int cost, int reserve, int aps, int preference)
{
	if (cost < 0 || cost + reserve > aps || roof.shot < MIN_ROOFTOP_SHOT ||
		approach.exposure > current.exposure + MAX_EXTRA_EXPOSURE ||
		roof.exposure > current.exposure + MAX_EXTRA_EXPOSURE) return -1000;
	const int score = roof.Score() + preference - (cost + 1) / 2;
	return score >= alternativeScore + MIN_POSITION_GAIN ? score : -1000;
}
}

INT8 DecideRooftopPosition(SOLDIERTYPE* soldier, INT16 groundCover)
{
	const int preference = gamepolicy(rooftop_position_preference);
	if (!RooftopSoldierEligible(*soldier, gfTurnBasedAI, gfHiddenInterrupt, preference) ||
		gWorldSector.z != 0 || InWaterOrGas(soldier, soldier->sGridNo)) return AI_ACTION_NONE;
	const OBJECTTYPE& gun = soldier->inv[HANDPOS];
	if (!GCM->getItem(gun.usItem)->isGun() || !gun.ubGunShotsLeft || gun.bGunAmmoStatus <= 0)
		return AI_ACTION_NONE;

	std::vector<KnownRoofThreat> threats;
	FOR_EACH_MERC(i)
	{
		KnownRoofThreat threat;
		if (GetKnownRoofThreat(*soldier, **i, threat) &&
			PythSpacesAway(soldier->sGridNo, threat.grid) <= MAX_THREAT_RANGE / CELL_X_SIZE)
			threats.push_back(threat);
	}
	if (threats.empty()) return AI_ACTION_NONE;
	std::sort(threats.begin(), threats.end(), [soldier](const KnownRoofThreat& a, const KnownRoofThreat& b)
	{
		return PythSpacesAway(soldier->sGridNo, a.grid) < PythSpacesAway(soldier->sGridNo, b.grid);
	});
	if (threats.size() > 4) threats.resize(4);

	INT16 origin;
	const int roaming = RoamingRange(soldier, &origin);
	std::array<bool, MAX_BUILDINGS> visited{};
	std::vector<RoofRoute> routes;
	const int x = soldier->sGridNo % WORLD_COLS, y = soldier->sGridNo / WORLD_COLS;
	const UINT16 movement = DetermineMovementMode(soldier, AI_ACTION_MOVE_TO_CLIMB);
	const int climbCost = GetAPsToClimbRoof(soldier, FALSE);
	for (int row = std::max(0, y - ROOFTOP_SEARCH_RADIUS); row <= std::min(WORLD_ROWS - 1, y + ROOFTOP_SEARCH_RADIUS); ++row)
	for (int col = std::max(0, x - ROOFTOP_SEARCH_RADIUS); col <= std::min(WORLD_COLS - 1, x + ROOFTOP_SEARCH_RADIUS); ++col)
	{
		const INT16 tile = row * WORLD_COLS + col;
		const UINT8 buildingID = gubBuildingInfo[tile];
		if (buildingID == NO_BUILDING || buildingID >= visited.size() || visited[buildingID]) continue;
		visited[buildingID] = true;
		const BUILDING* building = FindBuilding(tile);
		if (!building) continue;
		for (int i = 0; i < building->ubNumClimbSpots; ++i)
		{
			const INT16 ground = building->sUpClimbSpots[i], roof = building->sDownClimbSpots[i];
			if (ground < 0 || ground >= WORLD_MAX || roof < 0 || roof >= WORLD_MAX ||
				PythSpacesAway(soldier->sGridNo, ground) > ROOFTOP_SEARCH_RADIUS ||
				PythSpacesAway(origin, roof) > roaming || ground == soldier->sBlackList ||
				(ground != soldier->sGridNo && !NewOKDestination(soldier, ground, TRUE, 0)) ||
				!NewOKDestination(soldier, roof, TRUE, 1) ||
				InWaterOrGas(soldier, ground)) continue;
			SOLDIERTYPE projection = *soldier;
			PlaceRoofProjection(projection, ground, 0);
			projection.bLevel = 1;
			if (InGasOrSmoke(&projection, roof)) continue;
			projection.bLevel = 0;
			UINT8 direction;
			if (!FindHigherLevel(&projection, &direction) || NewGridNo(ground, DirectionInc(direction)) != roof) continue;
			// The existing climb action chooses a roof by facing. Reject ambiguous
			// corners so arrival facing cannot send us to a different landing.
			bool unambiguous = true;
			for (UINT8 facing = 0; facing < 8; facing += 2)
			{
				projection.bDirection = facing;
				if (!FindHigherLevel(&projection, &direction) || NewGridNo(ground, DirectionInc(direction)) != roof)
					unambiguous = false;
			}
			if (!unambiguous) continue;
			const int path = ground == soldier->sGridNo ? 0 :
				PlotPath(soldier, ground, FALSE, FALSE, movement, 0);
			if (ground != soldier->sGridNo && path <= 0) continue;
			if (path + climbCost + AP_MIN_AIM_ATTACK > soldier->bActionPoints) continue;
			routes.push_back({ground, roof, path + climbCost});
		}
	}
	std::sort(routes.begin(), routes.end(), [](const RoofRoute& a, const RoofRoute& b) { return a.cost < b.cost; });
	if (routes.size() > MAX_ROOFTOP_ROUTES) routes.resize(MAX_ROOFTOP_ROUTES);
	if (routes.empty()) return AI_ACTION_NONE;

	const RoofPositionValue current = EvaluateRoofPosition(*soldier, soldier->sGridNo, 0, threats);
	int alternative = current.Score();
	if (groundCover != NOWHERE)
	{
		const int cost = EstimatePlotPath(soldier, groundCover, FALSE, FALSE,
			DetermineMovementMode(soldier, AI_ACTION_TAKE_COVER), 0);
		if (cost > 0) alternative = std::max(alternative,
			EvaluateRoofPosition(*soldier, groundCover, 0, threats).Score() - (cost + 1) / 2);
	}
	int bestScore = -1000;
	INT16 bestClimb = NOWHERE;
	for (const RoofRoute& route : routes)
	{
		SOLDIERTYPE projected = *soldier;
		PlaceRoofProjection(projected, route.roof, 1);
		// Climbing ends crouched. The standing firing position we evaluated
		// must also be affordable, including standing up and readying the gun.
		const int reserve = AP_CROUCH + MinAPsToShootOrStab(projected, threats.front().grid, ADDTURNCOST);
		const int score = RoofRouteScore(current,
			EvaluateRoofPosition(*soldier, route.ground, 0, threats),
			EvaluateRoofPosition(*soldier, route.roof, 1, threats),
			alternative, route.cost, reserve, soldier->bActionPoints, preference);
		if (score > bestScore)
		{
			bestScore = score;
			bestClimb = route.ground;
		}
	}
	if (bestClimb == NOWHERE) return AI_ACTION_NONE;
	soldier->usActionData = bestClimb;
	SLOGD("AI {} chooses rooftop approach {} (position score {})", soldier->ubID, bestClimb, bestScore);
	return AI_ACTION_MOVE_TO_CLIMB;
}

#ifdef WITH_UNITTESTS
#include "gtest/gtest.h"
#include "DefaultContentManagerUT.h"
#include "Map_Information.h"
#include "RenderWorld.h"
#include <memory>

extern BUILDING gBuildings[MAX_BUILDINGS];
extern UINT8 gubNumberOfBuildings;

namespace
{
SOLDIERTYPE RoofTestSoldier()
{
	SOLDIERTYPE soldier{};
	soldier.ubID = 40;
	soldier.bTeam = ENEMY_TEAM;
	soldier.bSide = 1;
	soldier.ubProfile = NO_PROFILE;
	soldier.ubBodyType = REGMALE;
	soldier.bOrders = SEEKENEMY;
	soldier.bAlertStatus = STATUS_RED;
	soldier.bAIMorale = MORALE_NORMAL;
	soldier.bLife = soldier.bLifeMax = 80;
	soldier.bBreath = 100;
	soldier.bActionPoints = soldier.bInitialActionPoints = 25;
	return soldier;
}

TEST(RooftopAI, RestrictsClimbingToCombatTroopsWithATurnToSpare)
{
	SOLDIERTYPE s = RoofTestSoldier();
	EXPECT_TRUE(RooftopSoldierEligible(s, true, false, 5));
	s.bTeam = MILITIA_TEAM;
	EXPECT_TRUE(RooftopSoldierEligible(s, true, false, 5));
	s.bTeam = OUR_TEAM;
	EXPECT_FALSE(RooftopSoldierEligible(s, true, false, 5));
	s.bTeam = CIV_TEAM;
	EXPECT_FALSE(RooftopSoldierEligible(s, true, false, 5));
	s = RoofTestSoldier();
	EXPECT_FALSE(RooftopSoldierEligible(s, false, false, 5));
	EXPECT_FALSE(RooftopSoldierEligible(s, true, true, 5));
	EXPECT_FALSE(RooftopSoldierEligible(s, true, false, 0));
	--s.bActionPoints;
	EXPECT_FALSE(RooftopSoldierEligible(s, true, false, 5));
}

TEST(RooftopAI, EmergencyStatesAndScriptedGuardsDoNotClimbVoluntarily)
{
	const SOLDIERTYPE normal = RoofTestSoldier();
	for (int scenario = 0; scenario < 10; ++scenario)
	{
		SOLDIERTYPE s = normal;
		switch (scenario)
		{
			case 0: s.bUnderFire = TRUE; break;
			case 1: s.bShock = 1; break;
			case 2: s.bBreath = 49; break;
			case 3: s.bLife = 30; break;
			case 4: s.bOrders = STATIONARY; break;
			case 5: s.bAIMorale = MORALE_HOPELESS; break;
			case 6: s.bLevel = 1; break;
			case 7: s.ubBodyType = TANK_NW; break;
			case 8: s.ubProfile = QUEEN; break;
			case 9: s.bCollapsed = TRUE; break;
		}
		EXPECT_FALSE(RooftopSoldierEligible(s, true, false, 5)) << scenario;
	}
}

TEST(RooftopAI, UsefulRoofWinsButTravelAndBetterGroundCoverCanOutweighIt)
{
	const RoofPositionValue ground{20, 40}, approach{15, 40}, roof{60, 20};
	EXPECT_GT(RoofRouteScore(ground, approach, roof, ground.Score(), 12, 8, 25, 5), -1000);
	EXPECT_EQ(RoofRouteScore(ground, approach, roof, 50, 12, 8, 25, 5), -1000);
	// A barely better view is not worth an entire climb and approach.
	EXPECT_EQ(RoofRouteScore(ground, approach, {23, 35}, ground.Score(), 16, 8, 25, 5), -1000);
}

TEST(RooftopAI, KeepsEnoughAPToFireAfterClimbing)
{
	const RoofPositionValue ground{10, 30}, roof{70, 20};
	EXPECT_GT(RoofRouteScore(ground, ground, roof, ground.Score(), 12, 8, 20, 5), -1000);
	EXPECT_EQ(RoofRouteScore(ground, ground, roof, ground.Score(), 12, 8, 19, 5), -1000);
}

TEST(RooftopAI, RejectsBlockedShotsAndExposedApproachesOrLandings)
{
	const RoofPositionValue ground{10, 20}, roof{70, 20};
	EXPECT_EQ(RoofRouteScore(ground, ground, {0, 0}, ground.Score(), 10, 8, 25, 5), -1000);
	EXPECT_EQ(RoofRouteScore(ground, {10, 36}, roof, ground.Score(), 10, 8, 25, 5), -1000);
	EXPECT_EQ(RoofRouteScore(ground, ground, {70, 36}, ground.Score(), 10, 8, 25, 5), -1000);
}

TEST(RooftopAI, PreferenceOnlyTipsClosePositionDecisions)
{
	const RoofPositionValue ground{25, 20}, roof{32, 20};
	EXPECT_EQ(RoofRouteScore(ground, ground, roof, ground.Score(), 10, 8, 25, 0), -1000);
	EXPECT_GT(RoofRouteScore(ground, ground, roof, ground.Score(), 10, 8, 25, 5), -1000);
}

TEST(RooftopAI, UsesRememberedSightingsInsteadOfHiddenCurrentCoordinates)
{
	SOLDIERTYPE s = RoofTestSoldier(), opponent{};
	opponent.ubID = 0;
	opponent.bLife = 80;
	const INT8 oldPublic = gbPublicOpplist[s.bTeam][0];
	const INT16 oldLocation = gsLastKnownOppLoc[s.ubID][0];
	const INT8 oldLevel = gbLastKnownOppLevel[s.ubID][0];
	const INT16 oldPublicLocation = gsPublicLastKnownOppLoc[s.bTeam][0];
	const INT8 oldPublicLevel = gbPublicLastKnownOppLevel[s.bTeam][0];
	gbPublicOpplist[s.bTeam][0] = NOT_HEARD_OR_SEEN;
	KnownRoofThreat threat{};
	EXPECT_FALSE(GetKnownRoofThreat(s, opponent, threat));
	s.bOppList[0] = SEEN_LAST_TURN;
	gsLastKnownOppLoc[s.ubID][0] = 1000;
	gbLastKnownOppLevel[s.ubID][0] = 1;
	opponent.sGridNo = 2000;
	EXPECT_TRUE(GetKnownRoofThreat(s, opponent, threat));
	EXPECT_EQ(threat.grid, 1000);
	EXPECT_EQ(threat.level, 1);
	gbPublicOpplist[s.bTeam][0] = SEEN_CURRENTLY;
	gsPublicLastKnownOppLoc[s.bTeam][0] = 1100;
	gbPublicLastKnownOppLevel[s.bTeam][0] = 0;
	EXPECT_TRUE(GetKnownRoofThreat(s, opponent, threat));
	EXPECT_EQ(threat.grid, 1100);
	s.bOppList[0] = HEARD_THIS_TURN;
	gbPublicOpplist[s.bTeam][0] = NOT_HEARD_OR_SEEN;
	EXPECT_FALSE(GetKnownRoofThreat(s, opponent, threat));
	gbPublicOpplist[s.bTeam][0] = oldPublic;
	gsLastKnownOppLoc[s.ubID][0] = oldLocation;
	gbLastKnownOppLevel[s.ubID][0] = oldLevel;
	gsPublicLastKnownOppLoc[s.bTeam][0] = oldPublicLocation;
	gbPublicLastKnownOppLevel[s.bTeam][0] = oldPublicLevel;
}

TEST(RooftopAI, EvaluatesRealFiringGeometryWithoutChangingCombatants)
{
	struct RestoreContent
	{
		ContentManager* previous = GCM;
		bool loaded = gfWorldLoaded;
		SGPSector sector = gWorldSector;
		~RestoreContent() { GCM = previous; gfWorldLoaded = loaded; gWorldSector = sector; }
	} restore;
	std::unique_ptr<DefaultContentManagerUT> content(DefaultContentManagerUT::createDefaultCMForTesting());
	ASSERT_TRUE(content->loadGameData());
	GCM = content.get();
	gfWorldLoaded = TRUE;
	gWorldSector = SGPSector(9, 1);
	SOLDIERTYPE soldier = RoofTestSoldier(), opponent = RoofTestSoldier();
	opponent.ubID = 0;
	opponent.bSide = 0;
	soldier.sGridNo = 80 * WORLD_COLS + 80;
	opponent.sGridNo = soldier.sGridNo + 8;
	for (SOLDIERTYPE* s : {&soldier, &opponent})
	{
		s->bMarksmanship = s->bDexterity = s->bStrength = s->bWisdom = 80;
		s->bExpLevel = 4;
		s->bBreathMax = 100;
		s->usAnimState = STANDING;
		s->inv[HANDPOS].usItem = GLOCK_17;
		s->inv[HANDPOS].bGunStatus = 100;
		s->inv[HANDPOS].bGunAmmoStatus = 100;
		s->inv[HANDPOS].ubGunShotsLeft = 10;
	}
	// Neither actor has set up an attack. Projection must select its held gun,
	// including when invoked from RED AI before CalcBestShot has run.
	ASSERT_EQ(soldier.usAttackingWeapon, 0);
	const std::vector<KnownRoofThreat> threats{{&opponent, opponent.sGridNo, 0}};
	const auto ground = EvaluateRoofPosition(soldier, soldier.sGridNo, 0, threats);
	const auto roof = EvaluateRoofPosition(soldier, soldier.sGridNo, 1, threats);
	EXPECT_GT(ground.shot, 0);
	EXPECT_GT(roof.shot, 0);
	EXPECT_GT(roof.shot, ground.shot);
	EXPECT_LT(roof.exposure, ground.exposure);
	EXPECT_EQ(soldier.bLevel, 0);
	EXPECT_EQ(soldier.usAttackingWeapon, 0);
	EXPECT_EQ(opponent.usAttackingWeapon, 0);
	EXPECT_EQ(soldier.CTGTTarget, nullptr);
	EXPECT_EQ(opponent.CTGTTarget, nullptr);
	EXPECT_EQ(soldier.inv[HANDPOS].ubGunShotsLeft, 10);
	EXPECT_EQ(opponent.inv[HANDPOS].ubGunShotsLeft, 10);
}

TEST(RooftopAI, ChoosesAnActualClimbActionAndRejectsBlockedOrDisabledRoutes)
{
	const INT16 ground = 80 * WORLD_COLS + 80, roof = ground - WORLD_COLS;
	struct RestoreScenario
	{
		ContentManager* content = GCM;
		UINT32 slots = guiNumMercSlots;
		std::array<SOLDIERTYPE*, 2> firstSlots{MercSlots[0], MercSlots[1]};
		std::vector<UINT8> buildings{gubBuildingInfo, gubBuildingInfo + WORLD_MAX};
		BUILDING building = gBuildings[1];
		UINT8 count = gubNumberOfBuildings;
		MAP_ELEMENT roofTile;
		bool turnBased = gfTurnBasedAI, interrupt = gfHiddenInterrupt;
		SGPSector sector = gWorldSector;
		bool loaded = gfWorldLoaded;
		std::array<INT16, 4> bounds{gsLeftX, gsTopY, gsRightX, gsBottomY};
		INT8 knowledge = gbPublicOpplist[ENEMY_TEAM][0];
		INT16 location = gsLastKnownOppLoc[40][0];
		INT8 level = gbLastKnownOppLevel[40][0];
		explicit RestoreScenario(INT16 tile) : roofTile(gpWorldLevelData[tile]) {}
		~RestoreScenario()
		{
			GCM = content;
			guiNumMercSlots = slots;
			std::copy(firstSlots.begin(), firstSlots.end(), MercSlots);
			std::copy(buildings.begin(), buildings.end(), gubBuildingInfo);
			gBuildings[1] = building;
			gubNumberOfBuildings = count;
			gpWorldLevelData[80 * WORLD_COLS + 80 - WORLD_COLS] = roofTile;
			gfTurnBasedAI = turnBased;
			gfHiddenInterrupt = interrupt;
			gWorldSector = sector;
			gfWorldLoaded = loaded;
			gsLeftX = bounds[0]; gsTopY = bounds[1]; gsRightX = bounds[2]; gsBottomY = bounds[3];
			gbPublicOpplist[ENEMY_TEAM][0] = knowledge;
			gsLastKnownOppLoc[40][0] = location;
			gbLastKnownOppLevel[40][0] = level;
		}
	} restore(roof);
	std::unique_ptr<DefaultContentManagerUT> content(DefaultContentManagerUT::createDefaultCMForTesting());
	ASSERT_TRUE(content->loadGameData());
	GCM = content.get();
	// Exercise route selection separately from balancing the modest default.
	GamePolicy& policy = *const_cast<GamePolicy*>(GCM->getGamePolicy());
	policy.rooftop_position_preference = 20;
	SOLDIERTYPE soldier = RoofTestSoldier(), opponent = RoofTestSoldier();
	opponent.ubID = 0;
	opponent.bSide = 0;
	soldier.sGridNo = ground;
	opponent.sGridNo = ground + 8;
	for (SOLDIERTYPE* s : {&soldier, &opponent})
	{
		s->bActive = TRUE;
		s->bMarksmanship = s->bDexterity = s->bStrength = s->bWisdom = 80;
		s->bExpLevel = 4;
		s->bBreathMax = 100;
		s->usAnimState = STANDING;
		s->inv[HANDPOS].usItem = GLOCK_17;
		s->inv[HANDPOS].bGunStatus = s->inv[HANDPOS].bGunAmmoStatus = 100;
		s->inv[HANDPOS].ubGunShotsLeft = 10;
	}
	soldier.bOppList[0] = SEEN_LAST_TURN;
	gbPublicOpplist[ENEMY_TEAM][0] = NOT_HEARD_OR_SEEN;
	gsLastKnownOppLoc[40][0] = opponent.sGridNo;
	gbLastKnownOppLevel[40][0] = 0;
	guiNumMercSlots = 2;
	MercSlots[0] = &soldier;
	MercSlots[1] = &opponent;
	gWorldSector = SGPSector(9, 1);
	gfWorldLoaded = TRUE;
	gsLeftX = gsTopY = 0;
	gsRightX = 6400; gsBottomY = 3200;
	gfTurnBasedAI = TRUE;
	gfHiddenInterrupt = FALSE;
	std::fill_n(gubBuildingInfo, WORLD_MAX, NO_BUILDING);
	gubBuildingInfo[roof] = 1;
	gubNumberOfBuildings = 1;
	gBuildings[1] = {};
	gBuildings[1].ubNumClimbSpots = 1;
	gBuildings[1].sUpClimbSpots[0] = ground;
	gBuildings[1].sDownClimbSpots[0] = roof;
	PROFILE shape{};
	DB_STRUCTURE data{};
	DB_STRUCTURE_REF reference{&data, nullptr};
	STRUCTURE roofStructure{};
	roofStructure.sGridNo = roof;
	roofStructure.fFlags = STRUCTURE_NORMAL_ROOF;
	roofStructure.pShape = &shape;
	roofStructure.pDBStructureRef = &reference;
	gpWorldLevelData[roof] = {};
	gpWorldLevelData[roof].pStructureHead = &roofStructure;
	EXPECT_EQ(DecideRooftopPosition(&soldier), AI_ACTION_MOVE_TO_CLIMB);
	EXPECT_EQ(soldier.usActionData, ground);
	EXPECT_EQ(soldier.bLevel, 0);
	EXPECT_EQ(soldier.sGridNo, ground);
	// An occupied/blocked roof must not receive another soldier.
	roofStructure.fFlags |= STRUCTURE_OBSTACLE;
	roofStructure.sCubeOffset = STRUCTURE_ON_ROOF;
	EXPECT_EQ(DecideRooftopPosition(&soldier), AI_ACTION_NONE);
	roofStructure.fFlags = STRUCTURE_NORMAL_ROOF;
	roofStructure.sCubeOffset = STRUCTURE_ON_GROUND;
	policy.rooftop_position_preference = 0;
	EXPECT_EQ(DecideRooftopPosition(&soldier), AI_ACTION_NONE);
}
}
#endif
