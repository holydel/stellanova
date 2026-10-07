#pragma once

#include <sn/sim/catalog.h>

// The endless star map (docs/solo-loop.md): hexes in rings around the
// colony, each node made from the map's seed and its place, harder ring by
// ring. Axial coordinates (q, r); the colony is (0, 0).
namespace sn::sim
{
using ph::i32;

struct Hex
{
	i32 q = 0;
	i32 r = 0;

	bool operator==(const Hex& other) const { return q == other.q && r == other.r; }
};

// Steps from the colony.
u32 RingOf(Hex hex);
u32 Distance(Hex a, Hex b);
// One of the six neighbors, by direction 0 to 5.
Hex Neighbor(Hex hex, u32 direction);
// Ring `ring`'s hexes, in order around it (6 * ring of them; the colony for 0).
std::vector<Hex> RingHexes(u32 ring);

struct Node
{
	Hex hex;
	u32 ring = 0;
	u32 kind = 0; // in the catalog's map.kinds
	u32 seed = 0; // its field and its enemies
	u32 enemies = 0;
	f32 strength = 1.0f; // their hull, shield and damage, times this
	f32 loot = 1.0f;     // crates' loot, times this
};

// The node at a hex, the same for everyone in a season.
Node NodeAt(const Catalog& catalog, Hex hex);
} // namespace sn::sim
