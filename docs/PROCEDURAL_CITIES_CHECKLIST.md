# Procedural Cities Implementation Checklist

Medieval/Tudor style settlements with modular kit buildings, generated at build-time.

---

## Milestone 1: Single Building Prototype

- [ ] Obtain/create cottage mesh (modular pieces: walls, roof, door)
- [ ] Source textures from opengameart.org (thatch, timber, wattle)
- [ ] Create `BuildingSystem` with InitInfo pattern
- [ ] Load settlement points from `settlements.json`
- [ ] Place one cottage at first settlement point
- [ ] Query `TerrainSystem` for ground height
- [ ] Integrate with `ShadowSystem`
- [ ] Verify renders correctly with terrain

---

## Milestone 2: Multiple Building Types

- [ ] Add building types: Cottage, Barn, Church, Inn, Shop
- [ ] Create/obtain mesh for each type
- [ ] Add textures for each type (atlas or per-type)
- [ ] Map settlement type to building mix:
  - Hamlet: cottages, barn
  - Village: + church, inn
  - Town: + shops, market stalls
  - FishingVillage: + boat sheds, net racks
- [ ] Random selection weighted by settlement type

---

## Milestone 3: Simple Layout

- [ ] Generate positions in ring around settlement center
- [ ] Minimum 8m spacing between buildings
- [ ] Random rotation (0°, 90°, 180°, 270°)
- [ ] Collision check to prevent overlap
- [ ] Larger buildings (church) at center
- [ ] Suppress grass under building footprints via `GrassSystem`

---

## Milestone 4: Street Network

- [ ] Connect settlement center to incoming roads
- [ ] Generate main street along primary road
- [ ] Add side lanes branching from main street
- [ ] Buildings face streets (orient doors toward road)
- [ ] Rasterize streets into virtual texture tiles

---

## Milestone 5: Plot Subdivision

- [ ] Voronoi subdivision of settlement area
- [ ] Assign plot sizes by building type
- [ ] Add fences/walls at plot boundaries
- [ ] Gardens/yards behind houses
- [ ] Market square for towns (open central plot)

---

## Milestone 6: LOD System

- [ ] LOD0: Full detail (<50m)
- [ ] LOD1: Simplified geometry (50-200m)
- [ ] LOD2: Impostor billboards (>200m)
- [ ] Integrate with `HiZSystem` for occlusion culling
- [ ] Batch draw calls per LOD level

---

## Milestone 7: Texture Variety

- [ ] 3-4 color variations per building type
- [ ] Weathering overlay (moss, dirt, wear)
- [ ] Procedural tint based on settlement seed
- [ ] Roof material variation (thatch, tile, slate)

---

## Milestone 8: Settlement Character

- [ ] Coastal: fishing huts, harbour wall, nets
- [ ] Inland: more farms, larger barns
- [ ] Town: denser layout, taller buildings
- [ ] Landmark: church spire visible at distance
- [ ] Settlement signs at road entrances
