#pragma once
// Phase 3 stub. Phase 5 implements the full TerrainChunk.
// LOD contract: terrain chunks use FULL NODE REBUILD (not setMesh swap).
// Always store chunk IDs (ChunkId), never raw node pointers.
// See architecture/graphics-architecture/scene-graph-ownership.md.
using ChunkId = uint32_t;
class TerrainChunk {};
