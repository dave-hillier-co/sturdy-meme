# Procedural Settlements — Requirements

Requirements for the procedural settlement system. These are the durable design targets,
independent of which mechanism delivers them. For current integration status and next
steps see the "Procedural Cities" section of [PLAN.md](PLAN.md).

## Setting

- Period and region: High Medieval England, c. 1100–1300 AD, South Coast (Sussex,
  Hampshire, Dorset coastal landscapes). All generated content must be authentic to this
  time and place. The coast is a contested defensive frontier (Norman castles, French
  raids, Cinque Ports).
- One system must scale from 5-building hamlets to 200+ building towns, with
  artist-controllable parameters.
- Settlement archetypes, each with distinct layout logic: hamlet (no defenses), village
  (church refuge, optional castle), town (walls, gates, towers), fishing village
  (beach/quay oriented), port town (full walls, harbor defenses, up to Cinque Port status).
- Landscape drives the settlement pattern: chalk downs → nucleated valley/spring-line
  villages; coastal plain → linear villages; Weald → dispersed hamlets; river valleys →
  mill towns at fords/bridges; harbours → fishing villages. Village plan types:
  nucleated, linear, green, polyfocal.
- Architectural style follows wealth, age, and era: Saxon survival, Norman/Romanesque
  (round arches, thick walls, small windows), Transitional, Early English Gothic
  (pointed arches, lancets, buttresses).
- Materials are biome/region-driven via a per-biome palette (primary wall / secondary
  wall / roofing / decorative): flint and clunch on chalk, timber frame with
  wattle-and-daub in the Weald, thatch throughout, clay tile for wealth, cob, tarred
  timber on the coast, Purbeck marble for church decoration.
- Building catalogue (availability gated by settlement type): cottages, longhouses,
  cruck halls, stone/manor houses; church, chapel, market cross; tithe barn, granary,
  byre, dovecote; inn, smithy, bakehouse, water/post mills, market hall, warehouse;
  maritime set (net loft, boat shed, fish market, salthouse, customs house, shipyard,
  ropewalk, sail loft, cooperage, smokehouse); defensive set (walls, gates, towers,
  castles, beacons). Size and material quality reflect social hierarchy.

## Settlement layout

- Fully procedural generation first; artist/parameter modifiers applied afterwards.
  Settlements are static once generated (no growth simulation).
- Layout is terrain- and biome-aware: analyze height/slope, identify buildable areas,
  avoid water and cliffs, respect existing roads.
- Zoning: residential, commercial, industrial, religious, administrative, agricultural,
  maritime, defensive, and open (greens, squares, commons), distributed per settlement
  template.
- Required landmarks must always be placed and reachable: prominent church with
  graveyard, inn on the main street near a junction, central market cross, optional
  green/pond.
- Walled settlements adapt: streets align to gates, some buildings back onto walls,
  wall-walk access is preserved.

## Streets and roads

- Two connected tiers: inter-settlement roads (main road, lane, footpath) and
  intra-settlement streets. Every settlement must be reachable from every other
  ("roamable world" is the critical integration milestone).
- Streets form organic, terrain-following patterns — never grids. Follow contours,
  avoid steep slopes, driven by points of interest and connections.
- Hierarchy with decreasing width: main street → street → lane → alley → path. The main
  street continues the external road through the settlement.
- Water crossings are generated wherever roads meet water, typed by route importance
  and viability: ford, stepping stones, clapper bridge, timber bridge, stone bridge.
  Fords where shallow/narrow with gentle banks; bridges become control points
  (bridge towns, tolls, defenses).

## Lots and building placement

- Blocks between streets subdivide into street-aligned lots, every lot with valid
  street frontage.
- Burgage plots: deep narrow lots 5–10 m wide × 30–60 m deep; corner plots larger;
  irregular fills at street junctions.
- Buildings face the street (gable-end or parallel by lot width), outbuildings to the
  rear, yard between, period-appropriate setbacks.

## Buildings

- Exterior: generated from a data-driven type definition — footprint (rect/L/T/U/
  irregular with min/max dimensions), floors, roof (gable, hipped, half-hipped, shed;
  steep pitch for thatch), facade (era-appropriate doors/windows: shutters, oiled linen,
  lancets), exposed timber framing, chimneys rare before 1300. Building function must be
  recognizable at distance (church tower, mill sails, buttresses, visible frames).
- Interior (required, buildings are not solid volumes): floor plans per type
  (single-room cottage, longhouse cross-passage with byre, hall + services + chambers,
  nave/chancel/aisles, shop-front + back); structural elements (crucks, posts, pillars);
  central hearth with smoke hole and staining; furniture per room type with clearance
  rules; lighting from hearth/candles/windows responsive to time of day.
- Interior/exterior transitions use a portal system for visibility culling.
- Every generated building emits a low-poly blockout, a metadata record of its
  generation parameters, and a stable asset ID so hand-crafted replacements can be
  swapped in by the runtime.

## Defensive structures

- Town walls follow terrain with wall-walk, crenellations, arrow loops; interval towers
  every 30–50 m; gates at road entries (simple/towered/barbican/postern/water) with
  portcullis and murder holes.
- Castles at key settlements: motte-and-bailey, shell/rectangular/round keep,
  concentric, coastal; keep plus bailey buildings (great hall, chapel, kitchen,
  stables, barracks, well); optional motte and dry/wet/tidal ditch or moat.

## Ports and waterfront

- Coastal settlements classify as fishing harbor / coastal port / major port /
  Cinque Port, with harbor type (natural bay, river mouth, constructed, tidal).
- Harbor infrastructure: quays (steps, bollards, optional crane), jetties with berths,
  slipways, beach landing, entrance channel; harbor defenses (chain boom between anchor
  towers, watchtower, beacon).
- Waterfront layout: wider quayside street, perpendicular cart lanes, quay-aligned lots,
  working areas. Maritime buildings keep correct proportions (ropewalks extremely long,
  net lofts with upper cargo doors, open-sided fish markets).
- Tidal handling: significant range, exposed flats at low tide, tidal basins with gates,
  causeways. Salt production sites. Fishing villages are beach-oriented: boat parks
  above high water, net-drying areas, maritime chapel.

## Agriculture

- Open-field system radiating from villages: strip fields (~200 m × 20–30 m) with
  visible ridge-and-furrow, common fields, fenced pasture, grid-planted orchards.
- Field boundaries: hedgerows, ditches, low walls. Fields traversable but slower.

## Props and details

- Zone-aware prop placement: agricultural (haystacks, carts, hurdles, skeps), domestic
  (well, rain barrel, washing line, woodpile, midden), commercial (stalls, hanging
  signs, anvil), maritime (nets, lobster pots, boats, rope coils).
- Street furniture: wells/pumps/troughs spaced ≤ ~60 m, signposts, stocks, market
  cross, mounting blocks.
- Fences: picket, post-and-rail, dry-stone, hedgerow, wattle.
- Ground-cover variation (mud, hay, manure, grass/moss patches) and settlement
  vegetation integrated with the tree system, including tree-exclusion masks so trees
  never grow through buildings or streets.

## Terrain integration

- Buildings adapt to terrain rather than flattening it globally: per-lot foundations —
  surface, leveled, cut, cut-and-fill, terraced, raised, piled — with retaining walls
  and blend-back to natural terrain.
- Subterranean spaces cut into terrain: cellars with stairs/trapdoors, church crypts,
  wells to the water table, tunnels. Roads flatten/blend the ground beneath them.
- The generator exports terrain height-modification layers and material masks for the
  settlement footprint.

## Quality ladder (breadth-first)

Fill the whole world at low fidelity first, then refine. Each milestone covers the
entire world before the next begins. Quality tiers allow uneven polish: hero areas to
M10, primary towns to M8–9, secondary villages to M6–7, background hamlets to M4–5.

| M | "Done" means |
|---|---|
| M1 Markers | Settlement centers, radii, and roads visible across the terrain |
| M2 Footprints | Lots, streets, walls, key buildings as flat 2D shapes |
| M2.5 Roamable world | Roads + streets + subdivided plots + fields + blockout boxes + navigation, all connected; a character can walk between any two settlements |
| M3 Blockout | Correct 3D mass and scale with usable collision |
| M4 Silhouettes | Recognizable medieval skyline from distance; impostor-ready |
| M5 Structure | Building types distinguishable (frames, buttresses, openings, sails) |
| M6 Material | Regional color/material identity with weathering tint |
| M7 Facade | Close-up detail: window/door geometry, 3D timbers, chimneys, signs |
| M8 Props | Lived-in feel: yard props, fences, street furniture, ground variation |
| M9 Interiors | Enterable buildings: floor plan, furniture, lit hearth, door portals |
| M10 Polish | Full PBR, weathering, hand-crafted hero assets, baked lighting |

Milestones must unblock parallel workstreams (gameplay, AI/NPCs, exploration) as early
as M2/M2.5.

## Runtime and performance

- Streaming: settlements load/unload around the camera (targets: load ~500 m, unload
  ~700 m), async with no frame hitching.
- LOD ladder: LOD0 full detail (0–50 m) → LOD1 simplified (50–150 m) → LOD2 impostor
  (150–400 m) → LOD3 merged settlement silhouette (400 m+), with dithered cross-fade
  transitions. Multi-view impostor atlases whose silhouettes match full geometry.
- Frustum + Hi-Z occlusion culling per building.
- Integration with time-of-day (window glow, chimney smoke), weather (wet roofs, snow,
  puddles), and audio ambience.
- Budgets (targets; optimize after functionality): < 100 draw calls and < 500 K
  triangles per settlement at LOD0, < 50 MB texture / < 200 MB mesh memory per
  settlement, < 2 s load, < 2 ms frame-time impact, 60 FPS with 5+ settlements in view.

## Determinism, tooling, testing

- Seeded determinism at every level: identical seed + parameters reproduce identical
  settlements, buildings, and lots.
- Data-driven configuration: JSON settlement templates and building-type definitions,
  schema-validated. Exposed per-settlement parameters: seed, type, density, organicness
  (0 = grid … 1 = organic), wealth, age.
- Fast 2D preview iteration without a 3D rebuild: layout work must be inspectable as
  rendered maps/SVG with quick regeneration (live parameters, layer toggles,
  click-to-inspect lots).
- Automated layout validation on every generation: all lots have street frontage, key
  buildings present and reachable, no overlapping buildings, street network fully
  connected, settlement connects to external roads.
- Visual correctness checks: floor heights 2.4–2.8 m, thatch pitch 35–50°, windows
  taller than wide, doors 1.9–2.1 m, no obvious texture repetition, smooth LOD.
- Test coverage: unit tests for generators, per-archetype end-to-end tests across
  seeds, performance benchmarks, manual walkthroughs against reference photos of
  English villages.

## Committed numeric parameters

- Streets: main street 8 m, street 5 m, lane 3.5 m, alley 2 m, quayside 6 m; max grade
  ~15% (lanes 25%). Lot setbacks: front ~2 m, rear ~3 m, side ~1.5 m.
- Building sizes (width × length, m): cottage 4–7 × 8–12 (pitch 45–55°); longhouse
  5–7 × 15–25; cruck hall 6–8 × 12–20; stone house 6–10 × 10–15; tithe barn
  10–15 × 30–50; church nave 6–10 × 15–25 with 12–20 m tower; inn 6–10 × 12–20; smithy
  5–7 × 8–12; watermill 6–8 × 10–15; post-mill ~5 m square; net loft 5–8 × 8–12;
  warehouse 10–15 × 20–40; ropewalk 3–5 × 200–400; customs house 12–18 × 15–20.
  Floor heights ~2.2–2.8 m; church walls ~0.9 m thick.
- Defensive: wall height 4–6 m (to 10 m for major ports), thickness 2–3 m; interval
  towers every 30–50 m, 5–8 m across; ≤ 4 gates, ≥ ~100 m apart; gate passage 3–5 m
  wide × 4–5 m high; town ditch 6–10 m × 3–5 m deep; moat 10–20 m × 3–6 m deep; motte
  5–10 m high, 30–50 m base; rectangular keep 15–30 m square, 20–30 m tall, walls
  3–4 m; watchtower 4–6 m square, 10–15 m tall.
- Ports: quay ~100 m with ~4 berths, harbor depth ~3 m, channel ~15 m, slipway ~1:10,
  tidal range ~4 m.
- Water-crossing pathfinding cost tiers: ford 100–300, clapper 200–400, timber bridge
  500–800, stone bridge 800–1200; ford viability: depth < 0.5 m, width < 20 m, bank
  slope < 15°.
