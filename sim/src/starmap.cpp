#include <sn/sim/starmap.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sn::sim
{
namespace
{
constexpr Hex DIRECTIONS[6] = {{1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}};

// A number from a hex, the same everywhere (no floats, no platform's hash).
u32 Mix(u32 seed, i32 q, i32 r)
{
	u32 h = seed ^ 0x9e3779b9u;
	for (const u32 v : {u32(q), u32(r)})
	{
		h ^= v + 0x7f4a7c15u + (h << 6) + (h >> 2);
		h *= 0x85ebca6bu;
		h ^= h >> 13;
	}
	return h;
}
} // namespace

u32 RingOf(Hex hex)
{
	return u32((std::abs(hex.q) + std::abs(hex.r) + std::abs(hex.q + hex.r)) / 2);
}

u32 Distance(Hex a, Hex b) { return RingOf({a.q - b.q, a.r - b.r}); }

Hex Neighbor(Hex hex, u32 direction)
{
	const Hex step = DIRECTIONS[direction % 6];
	return {hex.q + step.q, hex.r + step.r};
}

std::vector<Hex> RingHexes(u32 ring)
{
	if (ring == 0)
		return {Hex{}};
	std::vector<Hex> hexes;
	hexes.reserve(6 * ring);
	// From `ring` steps in direction 4, around through every side.
	Hex hex = {DIRECTIONS[4].q * i32(ring), DIRECTIONS[4].r * i32(ring)};
	for (u32 side = 0; side < 6; ++side)
	{
		for (u32 step = 0; step < ring; ++step)
		{
			hexes.push_back(hex);
			hex = Neighbor(hex, side);
		}
	}
	return hexes;
}

Node NodeAt(const Catalog& catalog, Hex hex)
{
	const MapDesc& map = catalog.map;
	Node node;
	node.hex = hex;
	node.ring = RingOf(hex);
	node.seed = Mix(map.seed, hex.q, hex.r);
	if (node.ring == 0)
		return node; // the colony
	f32 total = 0.0f;
	for (const NodeKindDesc& kind : map.kinds)
		total += kind.weight;
	f32 pick = f32((node.seed >> 8) % 10000u) / 10000.0f * total;
	for (u32 k = 0; k < map.kinds.size(); ++k)
	{
		node.kind = k;
		pick -= map.kinds[k].weight;
		if (pick < 0.0f)
			break;
	}
	const NodeKindDesc& kind = map.kinds[node.kind];
	const EnemiesDesc& enemies = catalog.enemies;
	const f32 count = f32(enemies.base + node.ring / enemies.perRings) * kind.enemies;
	node.enemies = std::min(enemies.max, std::max(1u, u32(count + 0.5f)));
	node.strength = std::pow(enemies.growth, f32(node.ring - 1));
	node.loot = kind.loot;
	return node;
}
} // namespace sn::sim
