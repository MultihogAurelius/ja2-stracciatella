#include "gtest/gtest.h"
#include "Bullet_Trail.h"
#include "Bullets.h"
#include "Soldier_Control.h"
#include "Timer_Control.h"
#include "TileDat.h"
#include "WorldDef.h"

TEST(BulletTrail, FollowsBothEndsAndHeightInEveryDirection)
{
	for (int dx : {-20,0,20}) for (int dy : {-20,0,20}) for (int dz : {-128,0,128})
	{
		BulletTrailPosition const current{100,100,256}, previous{100 + dx,100 + dy,256 + dz};
		auto const points = MakeBulletTrail(previous,current);
		ASSERT_FALSE(points.empty()); ASSERT_LE(points.size(),60u);
		EXPECT_EQ(points.front().x,current.x); EXPECT_EQ(points.front().y,current.y); EXPECT_EQ(points.front().z,current.z);
		EXPECT_EQ(points.back().x,previous.x); EXPECT_EQ(points.back().y,previous.y); EXPECT_EQ(points.back().z,previous.z);
		for (auto const& p : points)
		{
			EXPECT_LE(std::min(previous.x,current.x),p.x); EXPECT_LE(p.x,std::max(previous.x,current.x));
			EXPECT_LE(std::min(previous.y,current.y),p.y); EXPECT_LE(p.y,std::max(previous.y,current.y));
			EXPECT_LE(std::min(previous.z,current.z),p.z); EXPECT_LE(p.z,std::max(previous.z,current.z));
		}
	}
	EXPECT_EQ(MakeBulletTrail({0,0,0},{10000,0,0}).size(),60u);
}

TEST(BulletTrail, OverlappingTrailsCleanUpTheirOwnNodesAcrossTiles)
{
	BulletTrail first, second;
	struct Cleanup { BulletTrail& a; BulletTrail& b; ~Cleanup() { a.Clear(); b.Clear(); } } cleanup{first,second};
	auto count = [] {
		int result = 0;
		for (int y = 10; y <= 12; ++y) for (int x = 10; x <= 12; ++x)
			for (auto* n = gpWorldLevelData[y * WORLD_COLS + x].pStructHead; n; n = n->pNext)
				if (n->usIndex == BULLETTILE1) ++result;
		return result;
	};
	int const initial = count();
	first.Draw({100,100,200},{120,120,256});
	EXPECT_EQ(count(),initial + 21);
	second.Draw({100,100,200},{120,120,256});
	EXPECT_EQ(count(),initial + 42);
	first.Clear(); EXPECT_EQ(count(),initial + 21);
	first.Clear(); EXPECT_EQ(count(),initial + 21);
	second.Draw({110,110,256},{120,120,256});
	EXPECT_EQ(count(),initial + 11);
	second.Clear(); EXPECT_EQ(count(),initial);
	first.Draw({-10,-10,0},{-1,-1,0}); EXPECT_TRUE(first.Empty());
}

TEST(BulletTrail, BulletUpdateStopAndRemovalKeepOtherBulletsVisible)
{
	ASSERT_EQ(guiNumBullets,0u);
	SOLDIERTYPE firer{}; firer.bBulletsLeft = 2;
	struct Cleanup { ~Cleanup() { DeleteAllBullets(); } } cleanup;
	int const grid = 10 * WORLD_COLS + 10;
	int const initial = [] (int tile) { int n = 0; for (auto* p = gpWorldLevelData[tile].pStructHead; p; p = p->pNext) ++n; return n; }(grid);
	auto count = [grid] { int n = 0; for (auto* p = gpWorldLevelData[grid].pStructHead; p; p = p->pNext) ++n; return n; };
	auto* first = CreateBullet(&firer,FALSE,0);
	auto* second = CreateBullet(&firer,FALSE,0);
	ASSERT_NE(first,nullptr); ASSERT_NE(second,nullptr);
	for (auto* b : {first,second})
	{
		b->sGridNo = grid;
		b->qCurrX = INT32_TO_FIXEDPT(105); b->qCurrY = INT32_TO_FIXEDPT(105); b->qCurrZ = INT32_TO_FIXEDPT(128);
		b->uiLastUpdate = GetJA2Clock(); b->usClockTicksPerUpdate = 65535;
	}
	UpdateBullets(); EXPECT_EQ(count(),initial + 2);
	UpdateBullets(); EXPECT_EQ(count(),initial + 2); // A timer-only update must not accumulate nodes.
	StopBullet(first); EXPECT_EQ(count(),initial + 1);
	RemoveBullet(second); EXPECT_EQ(count(),initial);
	UpdateBullets(); DeleteAllBullets();
	EXPECT_EQ(firer.bBulletsLeft,0); EXPECT_EQ(guiNumBullets,0u);
}
