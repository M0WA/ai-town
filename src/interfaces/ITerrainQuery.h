#pragma once

// ITerrainQuery — interface for simulation code to query terrain slope and height data
// without a direct dependency on TerrainSystem or Irrlicht mesh types.
//
// CitySimulation receives an ITerrainQuery* at construction (5th parameter)
// to compute earthworks cost at zone/road placement time:
//   cost = earthworks_base_cost_per_tile * clamp((slope_deg - 15) / 30, 0, 2)
//
// IrrlichtRenderer receives an ITerrainQuery* via setTerrainQuery() (called from
// main.cpp after terrain generation) to query tile heights for zone overlay and
// hover highlight Y-positions in pickTerrainTile() / setTileHoverHighlight() /
// setZoneOverlay() (Phase 9b deliverables B, C, E).
//
// Production: TerrainSystem implements ITerrainQuery.
// Unit tests: inject a ManualTerrainQuery (simple stub returning configured slope/height).
//
// LOD contract for getHeightAt(): MUST query TerrainSystem's persistent LOD0 heightmap
// array, never the active scene-node mesh geometry (which may be at LOD1 or LOD2 for
// distant chunks). The returned value is the exact grid-centre height sample with no
// interpolation. This contract is authoritative for Phase 9b ray-march cursor-to-terrain
// intersection queries (pickTerrainTile()) per
// architecture/graphics-architecture/procedural-terrain.md — Heightmap Query API.
//
// Source location: src/interfaces/ITerrainQuery.h
class ITerrainQuery {
public:
    virtual ~ITerrainQuery() = default;

    // Returns slope in degrees [0, 90] for the tile at grid position (tileX, tileZ).
    // Returns 0.0f for tiles outside map bounds (treated as flat — no earthworks cost).
    virtual float getSlopeDegrees(int tileX, int tileZ) const = 0;

    // Returns the Y-axis terrain height in world-space metres for the tile centre at
    // grid position (tileX, tileZ).
    // Returns 0.0f for out-of-bounds coordinates or before generate() is called.
    // Always queries the persistent LOD0 heightmap array — never scene-node geometry.
    // Used by IrrlichtRenderer::setTileHoverHighlight() and setZoneOverlay() to
    // position overlay quads above terrain surface (Phase 9b Deliverables B, C, E).
    // (ref: architecture/graphics-architecture/procedural-terrain.md — Heightmap Query API)
    virtual float getHeightAt(int tileX, int tileZ) const = 0;

    // Sets the persistent LOD0 heightmap height at (tileX, tileZ) to height,
    // applies weighted neighbour blending to the 8 surrounding tiles, and enqueues
    // ChunkRebuildRequests for all affected chunks.
    // Out-of-bounds coordinates are silently ignored.
    virtual void setTileHeight(int tileX, int tileZ, float height) = 0;

    /// Flush all pending terrain chunk rebuilds synchronously.
    /// Called after setTileHeight to ensure terrain geometry matches the new
    /// heightmap data before the next render frame.
    virtual void flushTerrainRebuilds() = 0;
};
