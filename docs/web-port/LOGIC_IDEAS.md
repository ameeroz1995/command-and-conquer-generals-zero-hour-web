# Game logic: where the time goes and what could be cheaper (2026-11-24)

Profile: battle benchmark, strike phase, normal build (warm1 = fd8466b), 25 s, RX 580.
Game logic is ~2/3 of the frame. "Exact" = same game, so multiplayer and replays keep working.

| Area | Share of frame (incl.) | Hot functions |
|---|---:|---|
| Spatial index upkeep + collisions (`PartitionManager::update`) | 14.3% | update 4.9% self, `Object::onCollide` 6.2% incl |
| Route searches (`Pathfinder::*`) | 17.6% | `checkForMovement` 5.0% self, `examineNeighboringCells` 2.2%, CPU flow fields 2.2% |
| Range searches (`getClosestObjects`) | 9.6% | 5.7% self, distance procs 2.9% |
| Logic loop itself (`GameLogic::update` self) | 4.5% | sleepy-update heap, inlined |
| Relationship lookups | ~2% | `Player::getRelationship` 1.2%, `Team::getRelationship` |

## Ideas, best first

1. **Range searches skip cells with no enemies (exact).** Most searches look for enemies
   (`AI.cpp` findClosestEnemy: first filter rejects allies). Every unit in every cell in range is
   distance-tested before that filter. Keep a per-cell count of objects per player; when the
   query's first filter is "live enemies of X", skip cells holding only X's allies. Same objects,
   same order, same result. Expected: about half of the 9.6%.
2. **Collision pairs without per-pair allocation, cheap tests first (exact).** Every moving unit
   adds a pair for every other object sharing a cell, each a pool allocation plus a hash probe,
   rebuilt every frame. Then the full geometry test runs before the cheap "no collisions" status
   test. Reorder (both side-effect free) and keep pairs in a reused array with the same order.
   Expected 3-5%.
3. **Relationship cache (exact).** Each `getRelationship` does up to two `std::map` finds;
   route searches call it per examined cell. A small player x player table, cleared whenever a
   relation changes, returns the same answers. Expected 2-3%.
4. **Moving units keep their cells when they stay in the same cells (NOT exact).** Today each
   moving unit removes itself from every cell and re-adds itself every frame. Skipping that
   changes the order inside cells, which changes tie-breaks: single player only, behind a switch.
   Expected 3-4%.
5. **Logic loop self time (needs a closer look).** 4.5% in `GameLogic::update` itself, likely the
   sleepy-update heap. Needs a line-level profile before choosing a fix.

Realistic total for 1-3: roughly 10% of a battle frame, without changing the game.
Already done: flow fields (on), ignore-units routes (switch, off), file reads ahead (on).
Removed as not worth it: parallel path searches, WebGPU flow fields.
