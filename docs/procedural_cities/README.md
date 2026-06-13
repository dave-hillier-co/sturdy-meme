# Procedural Cities & Settlements

> **Status note.** These documents describe an *aspirational* settlement-generation
> architecture (frontage/burgage lot subdivision, space-colonization streets, runtime
> streaming with impostor LOD). They are a design target, **not** a description of code
> that exists. The `IMPLEMENTATION_CHECKLIST.md` reflects this design and is largely
> unimplemented.
>
> A separate, **working** 2D town-layout generator already exists at `tools/town_generator`
> (Watabou-style: Voronoi/DCEL geometry, ward system, curtain walls, SVG output, unit
> tests). It is a different lineage from the design below and is not yet engine-integrated.
> See the "Procedural Cities" section of `docs/PLAN.md` for the reconciled status and next
> steps.

Comprehensive documentation for the procedural settlement generation system.

**Target Quality**: AAA-standard procedural generation with artist-controllable parameters
**Scale**: Settlements from 5-building hamlets to 200+ building towns
**Setting**: High Medieval (c. 1100-1300 AD) South Coast of England
**Region**: Inspired by Sussex, Hampshire, and Dorset coastal landscapes

## Documentation Structure

### Planning Documents

| Document | Description |
|----------|-------------|
| [00_OVERVIEW](00_OVERVIEW.md) | Executive summary, design decisions, visual milestones |
| [01_CONTEXT](01_CONTEXT.md) | Historical & regional context for High Medieval South Coast |
| [02_FOUNDATION](02_FOUNDATION.md) | Phase 1: Data structures, core types, configuration |
| [03_LAYOUT](03_LAYOUT.md) | Phase 2: Settlement layout, zones, lot generation |
| [04_BUILDINGS](04_BUILDINGS.md) | Phase 3 & 3b: Building exteriors and interiors |
| [05_INFRASTRUCTURE](05_INFRASTRUCTURE.md) | Phase 4, 4b, 4c: Streets, defensive structures, ports |
| [06_DETAILS](06_DETAILS.md) | Phase 5 & 5b: Props, terrain integration |
| [07_RUNTIME](07_RUNTIME.md) | Phase 6 & 7: LOD, streaming, integration |
| [08_TESTING](08_TESTING.md) | Quality assurance and testing strategy |
| [09_REFERENCE](09_REFERENCE.md) | Research references, glossary, appendices |

### Implementation Documents

| Document | Description |
|----------|-------------|
| [IMPL_OVERVIEW](IMPL_OVERVIEW.md) | System dependency graph, implementation order |
| [IMPL_FOUNDATION](IMPL_FOUNDATION.md) | Foundation layer: spatial utilities, config, terrain |
| [IMPL_GEOMETRY](IMPL_GEOMETRY.md) | Geometry layer: paths, layout, mesh, shape grammar |
| [IMPL_ASSEMBLY](IMPL_ASSEMBLY.md) | Assembly layer: building and settlement assemblers |
| [IMPL_RUNTIME](IMPL_RUNTIME.md) | Runtime layer: streaming and LOD systems |
| [IMPL_TOOLING](IMPL_TOOLING.md) | 2D preview, browser tools, TypeScript library |
| [IMPL_RECONCILIATION](IMPL_RECONCILIATION.md) | Mapping to existing codebase |

## Quick Start

1. **Understand the milestones** → [00_OVERVIEW](00_OVERVIEW.md)
2. **Review historical context** → [01_CONTEXT](01_CONTEXT.md)
3. **Check existing code** → [IMPL_RECONCILIATION](IMPL_RECONCILIATION.md)
4. **Start with foundation systems** → [IMPL_FOUNDATION](IMPL_FOUNDATION.md)

## Milestone Overview

```
M1        M2          M2.5       M3         M4          M5          M6         M7         M8
Markers → Footprints → Roamable → Blockout → Silhouette → Structure → Material → Facade → Props
  ▼          ▼           ▼          ▼          ▼           ▼           ▼          ▼         ▼
 ●●●       ▭▭▭         ═══        ▤▤▤        ⌂⌂⌂         🏠🏠🏠       ░░░       ▦▦▦       ⚐⚐⚐
Dots      Flat       Roads+     Boxes      Roofs       Types       Colors    Windows   Details
on map    shapes     Plots      extruded   added       visible     applied   doors     placed
                   ═══════════
                   KEY POINT:
                   Characters
                   can roam!
```

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              RUNTIME LAYER                                       │
├─────────────────────────────────────────────────────────────────────────────────┤
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                    │
│    │   Streaming  │───▶│     LOD      │───▶│   Renderer   │                    │
│    │    System    │    │    System    │    │  Integration │                    │
│    └──────────────┘    └──────────────┘    └──────────────┘                    │
├─────────────────────────────────────────────────────────────────────────────────┤
│                          BUILD-TIME GENERATION LAYER                            │
├─────────────────────────────────────────────────────────────────────────────────┤
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                    │
│    │   Settlement │    │   Building   │    │    Prop      │                    │
│    │   Assembler  │    │   Assembler  │    │   Assembler  │                    │
│    └──────────────┘    └──────────────┘    └──────────────┘                    │
├─────────────────────────────────────────────────────────────────────────────────┤
│                          GEOMETRY GENERATION LAYER                              │
├─────────────────────────────────────────────────────────────────────────────────┤
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                    │
│    │    Layout    │    │    Shape     │    │  Placement   │                    │
│    │    System    │    │   Grammar    │    │    System    │                    │
│    └──────────────┘    └──────────────┘    └──────────────┘                    │
│    ┌──────────────┐    ┌──────────────┐                                        │
│    │    Path      │    │    Mesh      │                                        │
│    │   Network    │    │  Generation  │                                        │
│    └──────────────┘    └──────────────┘                                        │
├─────────────────────────────────────────────────────────────────────────────────┤
│                             FOUNDATION LAYER                                    │
├─────────────────────────────────────────────────────────────────────────────────┤
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                    │
│    │   Terrain    │    │    Config    │    │   Spatial    │                    │
│    │ Integration  │    │    System    │    │   Utilities  │                    │
│    └──────────────┘    └──────────────┘    └──────────────┘                    │
└─────────────────────────────────────────────────────────────────────────────────┘
```
