#include "Weapon_Sound_System.h"

#include "Buildings.h"
#include "Campaign_Types.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "Isometric_Utils.h"
#include "Items.h"
#include "Json.h"
#include "Logger.h"
#include "MapScreen.h"
#include "Overhead.h"
#include "Render_Fun.h"
#include "SGPFile.h"
#include "Soldier_Control.h"
#include "Sound_Control.h"
#include "SoundMan.h"
#include "StrategicMap.h"
#include "Strategic_Movement.h"
#include "Structure.h"
#include "TileDef.h"
#include "WeaponModels.h"
#include "WorldMan.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <random>
#include <vector>

namespace
{
struct NwssWeapon
{
	ST::string calibre, type, name;
	bool automatic{}, cases{}, lastRound{};
};
struct NwssSettings
{
	bool enabled{};
	int shot = 127, echo = 85, room = 85, casing = 25, empty = 50, casingChance = 50;
	std::map<UINT16, NwssWeapon> weapons;
};
struct NwssContext
{
	bool silenced{}, burst{}, first = true, last = true, empty{}, room{}, underground{}, hardGround{};
	ST::string echo = "echo";
};
struct NwssSample
{
	ST::string path;
	int volume;
};
using NwssExists = std::function<bool(const ST::string&)>;
using NwssPick = std::function<unsigned(unsigned)>;

NwssSettings settings;
bool loaded{};
std::map<ST::string, bool> existence;
// Cosmetic variation must never consume either combat random-number stream.
std::mt19937 soundRandom{std::random_device{}()};
enum class BurstRoute { NONE, NWSS, VANILLA };
std::array<std::array<BurstRoute,2>,TOTAL_SOLDIERS> burstRoutes{};

unsigned PickSound(unsigned count)
{
	return count > 1 ? std::uniform_int_distribution<unsigned>(0,count - 1)(soundRandom) : 0;
}

bool SoundExists(const ST::string& path)
{
	auto found = existence.find(path);
	if (found == existence.end()) found = existence.emplace(path,GCM->doesGameResExists(path)).first;
	return found->second;
}

void LoadNwss()
{
	if (loaded) return;
	loaded = true;
	try
	{
		if (!GCM->doesGameResExists("nwss/config.json")) return;
		auto file = GCM->openGameResForReading("nwss/config.json");
		auto json = JsonValue::deserialize(file->readStringToEnd()).toObject();
		settings.enabled = json.getOptionalBool("enabled",false);
		settings.shot = std::clamp(json.getOptionalInt("shot_volume",127),0,127);
		settings.echo = std::clamp(json.getOptionalInt("echo_volume",85),0,127);
		settings.room = std::clamp(json.getOptionalInt("room_volume",85),0,127);
		settings.casing = std::clamp(json.getOptionalInt("casing_volume",25),0,127);
		settings.empty = std::clamp(json.getOptionalInt("empty_volume",50),0,127);
		settings.casingChance = std::clamp(json.getOptionalInt("casing_chance_percent",50),0,100);
		for (auto const& value : json["weapons"].toVec())
		{
			auto row = value.toObject();
			settings.weapons.emplace(row.GetUInt("item"),NwssWeapon{row.GetString("calibre"),
				row.GetString("type"),row.getOptionalString("sound_name"),
				row.getOptionalBool("automatic"),row.getOptionalBool("cases"),row.getOptionalBool("last_round")});
		}
	}
	catch (const std::exception& error)
	{
		settings = {};
		SLOGW("NWSS disabled: {}",error.what());
	}
}

ST::string SoundVariant(const ST::string& stem, const NwssExists& exists, const NwssPick& pick)
{
	std::vector<ST::string> files;
	if (exists(stem + ".ogg")) files.push_back(stem + ".ogg");
	for (unsigned i = 1; i <= 10; ++i)
	{
		auto path = stem + ST::string::from_uint(i) + ".ogg";
		if (exists(path)) files.push_back(std::move(path));
	}
	return files.empty() ? ST::string{} : files[pick(static_cast<unsigned>(files.size()))];
}

ST::string CalibreSound(const NwssWeapon& weapon, const char* sound,
	const NwssExists& exists, const NwssPick& pick)
{
	const ST::string folder = "nwss/audio/caliber/" + weapon.calibre + "/";
	std::vector<ST::string> prefixes;
	if (!weapon.name.empty()) prefixes.push_back(weapon.name + "_");
	if (!weapon.type.empty()) prefixes.push_back(weapon.type + "_");
	prefixes.push_back("");
	for (const auto& prefix : prefixes)
	{
		auto path = SoundVariant(folder + prefix + sound,exists,pick);
		if (!path.empty()) return path;
	}
	return {};
}

ST::string EffectSound(const NwssWeapon& weapon, const char* effect,
	const NwssExists& exists, const NwssPick& pick)
{
	auto path = SoundVariant("nwss/audio/caliber/" + weapon.calibre + "/" + effect,exists,pick);
	return path.empty() ? SoundVariant(ST::string("nwss/audio/common/") + effect,exists,pick) : path;
}

std::vector<NwssSample> PlanNwssShot(const NwssWeapon& weapon, const NwssContext& context,
	const NwssSettings& levels, const NwssExists& exists, const NwssPick& pick)
{
	std::vector<NwssSample> result;
	ST::string shot;
	bool noEcho = false, noRoom = false;
	if (!context.burst && !weapon.automatic)
	{
		if (!context.silenced)
		{
			shot = CalibreSound(weapon,"single_noecho",exists,pick);
			noEcho = !shot.empty();
			if (shot.empty())
			{
				shot = CalibreSound(weapon,"single_noroom",exists,pick);
				noRoom = !shot.empty();
			}
		}
		if (shot.empty()) shot = CalibreSound(weapon,context.silenced ? "single_s" : "single",exists,pick);
	}
	if (shot.empty()) shot = CalibreSound(weapon,context.silenced ? "loop_s" : "loop",exists,pick);
	if (shot.empty()) return result; // Original audio remains the complete fallback.
	result.push_back({shot,levels.shot});
	auto add = [&](ST::string path, int volume)
	{
		if (!path.empty() && volume > 0) result.push_back({std::move(path),volume});
	};
	if (!context.silenced && !noEcho)
	{
		if (!noRoom && context.first)
		{
			if (context.room) add(EffectSound(weapon,"room",exists,pick),levels.room);
			else if (context.underground) add(EffectSound(weapon,"underground",exists,pick),levels.room);
		}
		if (context.last)
		{
			auto echo = SoundVariant("nwss/audio/common/" + context.echo,exists,pick);
			if (echo.empty()) echo = SoundVariant("nwss/audio/common/echo",exists,pick);
			add(echo,levels.echo);
		}
	}
	if (weapon.cases && context.hardGround && pick(100) < static_cast<unsigned>(levels.casingChance))
		add(EffectSound(weapon,"case",exists,pick),levels.casing);
	if (weapon.lastRound && context.empty) add(EffectSound(weapon,"empty",exists,pick),levels.empty);
	return result;
}

NwssContext WeaponSoundContext(const SOLDIERTYPE& s, const WeaponModel& weapon)
{
	NwssContext context;
	const OBJECTTYPE& gun = s.inv[s.ubAttackingHand];
	context.silenced = FindAttachment(&gun,SILENCER) != NO_SLOT || weapon.ubAttackVolume <= 10;
	context.burst = s.bDoBurst > 0;
	context.first = !context.burst || s.bDoBurst == 1;
	context.last = !context.burst || s.bDoBurst >= weapon.ubShotsPerBurst || gun.ubGunShotsLeft <= 1;
	context.empty = gun.ubGunShotsLeft == 1;
	context.underground = gWorldSector.z > 0;
	const UINT8 terrain = GetTerrainType(s.sGridNo);
	const UINT8 room = GetRoom(s.sGridNo);
	context.hardGround = s.bLevel > 0 || terrain == FLAT_FLOOR || terrain == PAVED_ROAD;
	context.room = s.bLevel == 0 && room != NO_ROOM && terrain == FLAT_FLOOR &&
		FindStructure(s.sGridNo,STRUCTURE_ROOF) != nullptr;
	if (context.underground) context.echo = "echo_underground";
	else if (s.bLevel == 0 && s.bTargetLevel == 0 && room != NO_ROOM &&
		s.sTargetGridNo >= 0 && s.sTargetGridNo < WORLD_MAX &&
		GetRoom(s.sTargetGridNo) != NO_ROOM &&
		(room == GetRoom(s.sTargetGridNo) || SameBuilding(s.sGridNo,s.sTargetGridNo)))
		context.echo = "echo_building";
	else if (gWorldSector.x >= 1 && gWorldSector.x <= 16 && gWorldSector.y >= 1 && gWorldSector.y <= 16)
	{
		const UINT8 strategicTerrain = SectorInfo[gWorldSector.AsByte()].ubTraversability[THROUGH_STRATEGIC_MOVE];
		if (GetTownIdForSector(gWorldSector) != BLANK_SECTOR) context.echo = "echo_town";
		else if (strategicTerrain == HILLS || strategicTerrain == HILLS_ROAD) context.echo = "echo_hills";
		else if (strategicTerrain == DENSE || strategicTerrain == DENSE_ROAD ||
			strategicTerrain == SWAMP || strategicTerrain == SWAMP_ROAD) context.echo = "echo_dense";
	}
	return context;
}
}

bool PlayNewWeaponSound(SOLDIERTYPE& soldier)
{
	LoadNwss();
	if (!settings.enabled || soldier.ubID >= TOTAL_SOLDIERS || soldier.sGridNo < 0 || soldier.sGridNo >= WORLD_MAX ||
		(soldier.ubAttackingHand != HANDPOS && soldier.ubAttackingHand != SECONDHANDPOS)) return false;
	auto found = settings.weapons.find(soldier.usAttackingWeapon);
	if (found == settings.weapons.end()) return false;
	const auto context = WeaponSoundContext(soldier,*GCM->getWeapon(soldier.usAttackingWeapon));
	BurstRoute& route = burstRoutes[soldier.ubID][soldier.ubAttackingHand == SECONDHANDPOS ? 1 : 0];
	if (context.first) route = BurstRoute::NONE;
	else if (route != BurstRoute::NWSS) return false;
	const auto plan = PlanNwssShot(found->second,context,settings,SoundExists,PickSound);
	if (plan.empty())
	{
		if (context.first) route = BurstRoute::VANILLA;
		return route == BurstRoute::NWSS;
	}
	const UINT32 main = PlayLocationJA2SampleFromFile(soldier.sGridNo,plan[0].path.c_str(),plan[0].volume,1);
	if (main == SOUND_ERROR)
	{
		if (context.first) route = BurstRoute::VANILLA;
		return route == BurstRoute::NWSS;
	}
	route = BurstRoute::NWSS;
	if (context.burst && context.first) soldier.uiBurstSoundID = NO_SAMPLE;
	for (size_t i = 1; i < plan.size(); ++i)
		PlayLocationJA2SampleFromFile(soldier.sGridNo,plan[i].path.c_str(),plan[i].volume,1);
	return true;
}

void ResetNewWeaponSounds()
{
	settings = {};
	loaded = false;
	existence.clear();
	burstRoutes = {};
}

#ifdef WITH_UNITTESTS
#include "gtest/gtest.h"
#include "DefaultContentManagerUT.h"
#include "Random.h"
#include <SDL.h>
#include <memory>
#include <set>

namespace
{
const NwssPick firstVariant = [](unsigned) { return 0U; };
const NwssWeapon testWeapon{"test","pistol","custom",false,true,true};

TEST(Nwss, PrefersWeaponThenTypeThenCalibreAndHonoursNoEcho)
{
	std::set<ST::string> files{"nwss/audio/caliber/test/custom_single_noecho.ogg",
		"nwss/audio/caliber/test/pistol_single_noecho.ogg","nwss/audio/caliber/test/single_noecho.ogg",
		"nwss/audio/common/echo1.ogg","nwss/audio/common/room.ogg"};
	const NwssExists exists = [&](const ST::string& name) { return files.count(name) != 0; };
	NwssContext c; c.room = true;
	for (const char* prefix : {"custom_","pistol_",""})
	{
		const auto plan = PlanNwssShot(testWeapon,c,NwssSettings{},exists,firstVariant);
		ASSERT_EQ(plan.size(),1);
		const ST::string expected = ST::string("nwss/audio/caliber/test/") + prefix + "single_noecho.ogg";
		EXPECT_EQ(plan[0].path,expected);
		files.erase(expected);
	}
}

TEST(Nwss, SilencedFireUsesOnlySuppressedRecordingsAndNoAcousticLayers)
{
	const std::set<ST::string> files{"nwss/audio/caliber/test/loop.ogg","nwss/audio/caliber/test/loop_s.ogg",
		"nwss/audio/common/echo1.ogg","nwss/audio/common/room.ogg","nwss/audio/common/underground.ogg"};
	const NwssExists exists = [&](const ST::string& name) { return files.count(name) != 0; };
	NwssContext c; c.silenced = c.room = c.underground = true;
	const auto plan = PlanNwssShot(testWeapon,c,NwssSettings{},exists,firstVariant);
	ASSERT_EQ(plan.size(),1);
	EXPECT_EQ(plan[0].path,"nwss/audio/caliber/test/loop_s.ogg");
	const NwssExists noSuppressed = [&](const ST::string& name) { return exists(name) && !name.contains("_s."); };
	EXPECT_TRUE(PlanNwssShot(testWeapon,c,NwssSettings{},noSuppressed,firstVariant).empty());
}

TEST(Nwss, BurstsPlayOneShotPerBulletAndEchoOnlyAtTheirActualEnd)
{
	const NwssExists exists = [](const ST::string& name)
	{
		return name == "nwss/audio/caliber/test/loop.ogg" || name == "nwss/audio/common/echo1.ogg" ||
			name == "nwss/audio/common/room.ogg";
	};
	for (int shots : {1,2,5})
	{
		int gunshots = 0, rooms = 0, echoes = 0;
		for (int shot = 1; shot <= shots; ++shot)
		{
			NwssContext c; c.burst = c.room = true; c.first = shot == 1; c.last = shot == shots;
			const auto plan = PlanNwssShot(testWeapon,c,NwssSettings{},exists,firstVariant);
			for (const auto& sample : plan)
			{
				gunshots += sample.path.contains("loop.ogg");
				rooms += sample.path.contains("room.ogg");
				echoes += sample.path.contains("echo1.ogg");
			}
		}
		EXPECT_EQ(gunshots,shots); EXPECT_EQ(rooms,1); EXPECT_EQ(echoes,1);
	}
}

TEST(Nwss, CasesNeedHardGroundAndAWeaponThatEjectsThem)
{
	const NwssExists exists = [](const ST::string& name)
	{
		return name == "nwss/audio/caliber/test/loop.ogg" || name == "nwss/audio/common/case1.ogg" ||
			name == "nwss/audio/caliber/test/empty.ogg";
	};
	NwssContext c; c.hardGround = c.empty = true;
	auto plan = PlanNwssShot(testWeapon,c,NwssSettings{},exists,firstVariant);
	ASSERT_EQ(plan.size(),3); EXPECT_EQ(plan[1].volume,25);
	NwssWeapon revolver = testWeapon; revolver.cases = revolver.lastRound = false;
	EXPECT_EQ(PlanNwssShot(revolver,c,NwssSettings{},exists,firstVariant).size(),1);
	c.hardGround = false;
	EXPECT_EQ(PlanNwssShot(testWeapon,c,NwssSettings{},exists,firstVariant).size(),2);
	const NwssPick noCase = [](unsigned count) { return count - 1; };
	c.hardGround = true;
	EXPECT_EQ(PlanNwssShot(testWeapon,c,NwssSettings{},exists,noCase).size(),2);
}

TEST(Nwss, EnvironmentEchoFallsBackAndNoRoomIsRespected)
{
	std::set<ST::string> files{"nwss/audio/caliber/test/single_noroom.ogg","nwss/audio/common/echo_dense1.ogg",
		"nwss/audio/common/echo1.ogg","nwss/audio/common/room.ogg"};
	const NwssExists exists = [&](const ST::string& name) { return files.count(name) != 0; };
	NwssContext c; c.echo = "echo_dense"; c.room = true;
	auto plan = PlanNwssShot(testWeapon,c,NwssSettings{},exists,firstVariant);
	ASSERT_EQ(plan.size(),2); EXPECT_EQ(plan[1].path,"nwss/audio/common/echo_dense1.ogg");
	files.erase(plan[1].path);
	plan = PlanNwssShot(testWeapon,c,NwssSettings{},exists,firstVariant);
	ASSERT_EQ(plan.size(),2); EXPECT_EQ(plan[1].path,"nwss/audio/common/echo1.ogg");
}

TEST(Nwss, MissingPrimarySoundNeverProducesOrphanEffects)
{
	const NwssExists effectsOnly = [](const ST::string& name) { return name.contains("/common/"); };
	NwssContext c; c.hardGround = c.empty = c.room = true;
	EXPECT_TRUE(PlanNwssShot(testWeapon,c,NwssSettings{},effectsOnly,firstVariant).empty());
}

class NwssAssets : public testing::Test
{
protected:
	void SetUp() override
	{
		previous = GCM;
		content.reset(DefaultContentManagerUT::createDefaultCMForTesting());
		ASSERT_TRUE(content->loadGameData());
		GCM = content.get();
		ResetNewWeaponSounds();
		LoadNwss();
		ASSERT_TRUE(settings.enabled);
	}
	void StartAudio()
	{
		if (const char* driver = SDL_getenv("SDL_AUDIODRIVER")) { oldDriver = driver; hadDriver = true; }
		oldEnabled = IsSoundEnabled();
		SDL_setenv("SDL_AUDIODRIVER","dummy",1);
		SoundEnableSound(TRUE);
		InitializeSoundManager();
		audioStarted = true;
	}
	void TearDown() override
	{
		if (audioStarted)
		{
			ShutdownSoundManager();
			SoundEnableSound(oldEnabled);
			SDL_setenv("SDL_AUDIODRIVER",hadDriver ? oldDriver.c_str() : "",1);
		}
		ResetNewWeaponSounds();
		GCM = previous;
	}
	SOLDIERTYPE Gun(UINT16 item)
	{
		SOLDIERTYPE s{}; s.ubID = 0; s.sGridNo = 80 * WORLD_COLS + 80;
		s.sTargetGridNo = s.sGridNo + 5; s.usAttackingWeapon = item; s.ubAttackingHand = HANDPOS;
		s.inv[HANDPOS].usItem = item; s.inv[HANDPOS].ubGunShotsLeft = 5;
		return s;
	}
	ContentManager* previous{};
	std::unique_ptr<DefaultContentManagerUT> content;
	bool audioStarted{}, oldEnabled{}, hadDriver{};
	ST::string oldDriver;
};

TEST_F(NwssAssets, EveryMappedVanillaGunHasAnAudiblePrimarySample)
{
	ASSERT_EQ(settings.weapons.size(),38);
	for (const auto& entry : settings.weapons)
	{
		SCOPED_TRACE(entry.first);
		NwssContext c;
		const auto plan = PlanNwssShot(entry.second,c,settings,SoundExists,firstVariant);
		ASSERT_FALSE(plan.empty());
		EXPECT_TRUE(GCM->getItem(entry.first)->isGun());
	}
	EXPECT_EQ(settings.weapons.count(DART_GUN),0);
	EXPECT_EQ(settings.weapons.count(ROCKET_RIFLE),0);
}

TEST_F(NwssAssets, ImportedOggFilesDecodeThroughTheActualMixer)
{
	StartAudio();
	auto file = GCM->openGameResForReading("nwss/coverage.json");
	auto json = JsonValue::deserialize(file->readStringToEnd()).toObject();
	const auto sources = json["sources"].toObject();
	for (const auto& name : sources.keys())
	{
		const ST::string path = "nwss/audio/" + name;
		const UINT32 id = SoundPlay(path.c_str(),0,MIDDLEPAN,1,nullptr,nullptr);
		ASSERT_NE(id,SOUND_ERROR) << path.c_str();
		// Unit tests have no main loop to reclaim stopped channels.
		SoundStopAll();
	}
}

TEST_F(NwssAssets, LayeredAudioDoesNotExhaustTheOriginalSixteenChannels)
{
	StartAudio();
	for (int i = 0; i < 24; ++i)
		ASSERT_NE(SoundPlay("nwss/audio/common/echo1.ogg",0,MIDDLEPAN,LOOPING,nullptr,nullptr),SOUND_ERROR) << i;
	SoundStopAll();
	auto s = Gun(GLOCK_17);
	EXPECT_TRUE(PlayNewWeaponSound(s));
}

TEST_F(NwssAssets, BurstFallbackIsStableAndSoundDoesNotChangeCombatStateOrRandomness)
{
	StartAudio();
	auto s = Gun(GLOCK_18);
	s.bDoBurst = 1; s.bActionPoints = 20;
	const auto original = settings.weapons[GLOCK_18].calibre;
	settings.weapons[GLOCK_18].calibre = "not-installed";
	EXPECT_FALSE(PlayNewWeaponSound(s));
	settings.weapons[GLOCK_18].calibre = original;
	s.bDoBurst = 2;
	EXPECT_FALSE(PlayNewWeaponSound(s)); // Do not layer NWSS over an existing vanilla burst.
	s.bDoBurst = 1;
	const auto randomBefore = gRandomEngine;
	const auto preRandomBefore = guiPreRandomIndex;
	EXPECT_TRUE(PlayNewWeaponSound(s));
	s.bDoBurst = 2; s.inv[HANDPOS].ubGunShotsLeft = 1;
	EXPECT_TRUE(PlayNewWeaponSound(s)); // Short magazine: this is the last audible shot.
	EXPECT_EQ(s.bDoBurst,2); EXPECT_EQ(s.bActionPoints,20);
	EXPECT_EQ(s.inv[HANDPOS].ubGunShotsLeft,1);
	EXPECT_EQ(gRandomEngine,randomBefore); EXPECT_EQ(guiPreRandomIndex,preRandomBefore);
	SoundStopAll();
	s = Gun(GLOCK_17); s.ubAttackingHand = SECONDHANDPOS;
	s.inv[SECONDHANDPOS] = s.inv[HANDPOS];
	EXPECT_TRUE(PlayNewWeaponSound(s));
	SoundStopAll();
	s.inv[SECONDHANDPOS].usAttachItem[0] = SILENCER;
	s.inv[SECONDHANDPOS].bAttachStatus[0] = 100;
	EXPECT_TRUE(WeaponSoundContext(s,*GCM->getWeapon(GLOCK_17)).silenced);
	EXPECT_TRUE(PlayNewWeaponSound(s));
	SoundStopAll();
	settings.enabled = false;
	EXPECT_FALSE(PlayNewWeaponSound(s));
}

TEST_F(NwssAssets, ActualSectorRoomAndRoofStateSelectAcoustics)
{
	auto s = Gun(GLOCK_17);
	const INT16 grid = s.sGridNo, target = s.sTargetGridNo;
	const SGPSector sector(1,1);
	struct Restore
	{
		SGPSector sector = gWorldSector;
		SECTORINFO info = SectorInfo[0];
		UINT8 town = StrategicMap[SGPSector(1,1).AsStrategicIndex()].bNameId;
		INT16 grid, target;
		UINT8 room, targetRoom;
		MAP_ELEMENT map;
		Restore(INT16 g, INT16 t) : grid(g), target(t), room(gubWorldRoomInfo[g]),
			targetRoom(gubWorldRoomInfo[t]), map(gpWorldLevelData[g]) {}
		~Restore()
		{
			gWorldSector = sector; SectorInfo[0] = info;
			StrategicMap[SGPSector(1,1).AsStrategicIndex()].bNameId = town;
			gubWorldRoomInfo[grid] = room; gubWorldRoomInfo[target] = targetRoom;
			gpWorldLevelData[grid] = map;
		}
	} restore(grid,target);
	gWorldSector = SGPSector(1,1,1);
	EXPECT_EQ(WeaponSoundContext(s,*GCM->getWeapon(GLOCK_17)).echo,"echo_underground");
	gWorldSector = sector;
	gpWorldLevelData[grid] = {};
	gpWorldLevelData[grid].ubTerrainID = FLAT_FLOOR;
	gubWorldRoomInfo[grid] = gubWorldRoomInfo[target] = 1;
	STRUCTURE roof{}; roof.fFlags = STRUCTURE_NORMAL_ROOF;
	gpWorldLevelData[grid].pStructureHead = &roof;
	auto c = WeaponSoundContext(s,*GCM->getWeapon(GLOCK_17));
	EXPECT_TRUE(c.room); EXPECT_TRUE(c.hardGround); EXPECT_EQ(c.echo,"echo_building");
	s.bLevel = 1;
	c = WeaponSoundContext(s,*GCM->getWeapon(GLOCK_17));
	EXPECT_FALSE(c.room); EXPECT_TRUE(c.hardGround);
	s.bLevel = 0;
	gubWorldRoomInfo[grid] = gubWorldRoomInfo[target] = NO_ROOM;
	gpWorldLevelData[grid].pStructureHead = nullptr;
	gpWorldLevelData[grid].ubTerrainID = FLAT_GROUND;
	StrategicMap[sector.AsStrategicIndex()].bNameId = DRASSEN;
	EXPECT_EQ(WeaponSoundContext(s,*GCM->getWeapon(GLOCK_17)).echo,"echo_town");
	StrategicMap[sector.AsStrategicIndex()].bNameId = BLANK_SECTOR;
	SectorInfo[0].ubTraversability[THROUGH_STRATEGIC_MOVE] = HILLS;
	EXPECT_EQ(WeaponSoundContext(s,*GCM->getWeapon(GLOCK_17)).echo,"echo_hills");
	SectorInfo[0].ubTraversability[THROUGH_STRATEGIC_MOVE] = DENSE;
	EXPECT_EQ(WeaponSoundContext(s,*GCM->getWeapon(GLOCK_17)).echo,"echo_dense");
	SectorInfo[0].ubTraversability[THROUGH_STRATEGIC_MOVE] = PLAINS;
	c = WeaponSoundContext(s,*GCM->getWeapon(GLOCK_17));
	EXPECT_EQ(c.echo,"echo"); EXPECT_FALSE(c.hardGround);
}
}
#endif
