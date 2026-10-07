# 0013. Ships, modules and damage from a catalog; turrets and orders

- Status: Proposed
- Date: 2026-10-04

## Context

- The developer's MVP design (2026-10-04, `docs/solo-loop.md`): ship
  sizes, four damage types, the ship's stats, external and internal
  modules, power and a capacitor, six weapon kinds. First a frigate with
  one external and one internal slot, a laser, a plasma gun and its ammo.
- The developer's answers (2026-10-04): kinetic and thermal damage over the
  shield's threshold bleeds through to the hull; control by orders, with
  turrets that fire on their own, while the stick still steers.
- Until now a ship had one gun in its nose (`sim::WeaponClass`), fired by
  a trigger, and one kind of damage. Numbers were C++ defaults.
- The website shows the same ships and modules with their stats (ADR 0015).

## Decision

- **One catalog:** `content/catalog.json` holds every number: hulls,
  modules, ammo, enemies, the colony, the map and the rules. CMake builds
  it into `sn_sim`, which reads it with yyjson (pith's) at startup; the
  website reads the same file. A hash of it travels in Hello: a server
  turns away a client with another catalog, as it does another protocol.
- **Ships are fitted** (`sim/fitting.h`): a hull, a module per slot, and a
  hold. `sim::Fit` makes the ship's working numbers from them (mass,
  accelerations, power, capacitor, damage per second), for the sim, the
  fitting screen and the website alike.
- **Damage has four types** (EM, explosive, kinetic, thermal) and
  resistances. EM and explosive go to the shield while it lasts. Kinetic
  and thermal hits give the shield at most its threshold, and the rest
  goes to the hull. Each hull hit also damages one external module (half
  as much); a broken module stops working.
- **Power:** each tick the capacitor gains the reactor's unused power and
  pays for active modules and shots. While it is empty, a module that needs
  energy waits; an overloaded fit takes modules offline from the last slot
  back.
- **Every weapon is a turret.** It turns at its own speed toward its
  target: the ship's chosen target while in range, else the nearest enemy
  in range and in scanner reach. It fires once on target. The laser is a
  beam: each tick it damages the first rock or ship on the line to its
  target. The plasma gun fires shots from a magazine, which reloads from
  the hold. The nose gun, the trigger and `WeaponClass` go.
- **Orders** (`bots/orders.h`): `bots::FollowOrder` turns an order into a
  tick's controls. Orders are: fly to a point and stop, attack a target
  (close to 80% of the weapon's range and circle there), or none (slow to
  a stop). The client runs it for its own ship, so its controls still go
  through prediction (ADR 0012). The server runs it for bots and escorts.
  Keys, the stick or a held finger steer directly and cancel a move order.
  The target travels in Input.
- **Loot crates are part of the world:** a destroyed enemy drops one; a
  ship within 6 m picks it up if its hold has room. Each pickup is an
  event.
- **Protocol 5:**
  - Welcome carries the ship's working numbers.
  - Snapshots carry each ship's turret angle, its beam's target, and its
    capacitor; the player's own adds its ammo and hold.
  - Events add BeamHit, ModuleBroken, CratePicked and CrateDropped.

## Consequences

- The skirmish and the online match fight with turrets too: the dogfight
  with a nose gun is gone. Hits and shots are the server's alone; only
  the ship's flight is predicted.
- Balance is a matter of editing one JSON file. A rebuild picks it up; a
  server and its clients must be rebuilt together.
- Tests cover each damage rule, the capacitor, overload, turret turning
  and aim, the beam's rock, the magazine and reload, crates and holds, and
  orders reaching their point and circling a target.
- **Not yet:** rigs, sizes past S, the other four weapons, a turret's arc
  (all turn freely), heat, module tiers.
