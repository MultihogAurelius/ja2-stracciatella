#pragma once

#include "Types.h"
#include <vector>

struct LEVELNODE;

struct BulletTrailPosition { int x, y, z; }; // World coordinates; z is in height units.
std::vector<BulletTrailPosition> MakeBulletTrail(BulletTrailPosition previous, BulletTrailPosition current);

// Presentation only. Kept outside BULLET and the save-game representation.
class BulletTrail
{
public:
	void Clear();
	void Draw(BulletTrailPosition previous, BulletTrailPosition current);
	bool Empty() const { return nodes.empty(); }
private:
	struct Node { UINT32 grid; LEVELNODE* node; };
	std::vector<Node> nodes;
};
