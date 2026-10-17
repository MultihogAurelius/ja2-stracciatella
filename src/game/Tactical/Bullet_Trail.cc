#include "Bullet_Trail.h"
#include "LOS.h"
#include "TileDat.h"
#include "WorldDef.h"
#include "WorldMan.h"
#include <algorithm>
#include <cstdlib>

std::vector<BulletTrailPosition> MakeBulletTrail(BulletTrailPosition previous, BulletTrailPosition current)
{
	// Adapt 1.13's alternate bullet graphics: repeat the bullet tile along the
	// most recent movement segment, with a maximum of 60 samples. Interpolate
	// height as well, so elevated and descending shots follow their real path.
	int const dx = previous.x - current.x, dy = previous.y - current.y, dz = previous.z - current.z;
	int const distance = std::max({std::abs(dx),std::abs(dy),std::abs(CONVERT_HEIGHTUNITS_TO_PIXELS(dz))});
	std::vector<BulletTrailPosition> points;
	int const count = std::min(60,distance + 1);
	for (int i = 0; i < count; ++i)
	{
		points.push_back(distance == 0 ? current : BulletTrailPosition{
			current.x + dx * i / distance,current.y + dy * i / distance,current.z + dz * i / distance});
	}
	return points;
}

void BulletTrail::Clear()
{
	// Remove our exact nodes: several bullets can share a tile during a burst.
	for (auto const& entry : nodes) RemoveStructFromLevelNode(entry.grid,entry.node);
	nodes.clear();
}

void BulletTrail::Draw(BulletTrailPosition previous, BulletTrailPosition current)
{
	Clear();
	for (auto const& point : MakeBulletTrail(previous,current))
	{
		if (point.x < 0 || point.y < 0 || point.x >= WORLD_COLS * CELL_X_SIZE || point.y >= WORLD_ROWS * CELL_Y_SIZE) continue;
		UINT32 const grid = point.x / CELL_X_SIZE + (point.y / CELL_Y_SIZE) * WORLD_COLS;
		auto* node = AddStructToTail(grid,BULLETTILE1);
		node->ubShadeLevel = DEFAULT_SHADE_LEVEL;
		node->ubNaturalShadeLevel = DEFAULT_SHADE_LEVEL;
		node->uiFlags |= LEVELNODE_USEABSOLUTEPOS | LEVELNODE_IGNOREHEIGHT;
		node->sRelativeX = point.x;
		node->sRelativeY = point.y;
		node->sRelativeZ = CONVERT_HEIGHTUNITS_TO_PIXELS(point.z);
		nodes.push_back({grid,node});
	}
}
