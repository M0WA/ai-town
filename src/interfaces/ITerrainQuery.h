#pragma once

// ITerrainQuery — interface for simulation code to query terrain slope data
// without a direct dependency on TerrainSystem or Irrlicht mesh types.
//
// CitySimulation receives an ITerrainQuery* at construction (5th parameter)
// to compute earthworks cost at zone/road placement time:
//   cost = earthworks_base_cost_per_tile * clamp((slope_deg - 15) / 30, 0, 2)
//
// Production: TerrainSystem implements ITerrainQuery.
// Unit tests: inject a ManualTerrainQuery (simple stub returning configured slope).
//
// The interface is intentionally minimal — only slope queries are needed by
// simulation logic. Height queries for audio positioning (playPositionalSound Y)
// use Y=0 in Phase 6 (see phase-6.md earthworks deliverable note) and will be
// refined in Phase 10 if required.
//
// Source location: src/interfaces/ITerrainQuery.h
class ITerrainQuery {
public:
    virtual ~ITerrainQuery() = default;

    // Returns slope in degrees [0, 90] for the tile at grid position (tileX, tileZ).
    // Returns 0.0f for tiles outside map bounds (treated as flat — no earthworks cost).
    virtual float getSlopeDegrees(int tileX, int tileZ) const = 0;
};
