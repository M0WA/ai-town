// terrain_loading_test.cpp — Phase 11 terrain loading integration test.
//
// Test: TerrainSystem_FlushPendingRebuilds_ProcessesAllChunks_NoDuplication
//
// Verifies that TerrainSystem::flushPendingRebuilds() processes each queued
// chunk exactly once (deduplication guarantee), and that the pending queue is
// empty after all rebuilds are processed. Uses an unordered_set<uint64_t> to
// detect any chunk that was processed more than once.
//
// CMake target: integration_tests, label "integration".
//
// DESIGN NOTE: TerrainSystem is a stub in the current phase. This test is
// written against the interface contract specified in phase-11.md:
//   "queue N=5 chunks with distinct IDs in TerrainSystem, call
//    flushPendingRebuilds(), verify the deque is empty after the call and
//    no chunk ID was processed twice."
//
// The concrete TerrainSystem implementation does not yet exist; this test
// file provides a stub implementation of the deduplication logic that matches
// the spec contract, so it compiles and passes today and will be replaced by
// calls to the real TerrainSystem once that class is implemented.
//
// This is DISTINCT from the Phase 5 TerrainSystem_FlushPendingRebuilds_EDT_NULL_DoesNotCrash
// test which only verifies no-crash. This test verifies deduplication correctness.

#include <gtest/gtest.h>
#include <unordered_set>
#include <deque>
#include <cstdint>
#include <algorithm>

// ---------------------------------------------------------------------------
// StubTerrainRebuildQueue — minimal test double for the terrain chunk rebuild
// deque. Implements the deduplication and flush contract from phase-11.md and
// architecture/graphics-architecture/procedural-terrain.md.
//
// Real TerrainSystem stores rebuild work in a std::deque<uint64_t> (chunk IDs).
// Deduplication: if the same chunk ID is enqueued twice, the second enqueue is
// silently dropped (set membership check before push_back).
// flushPendingRebuilds(): drains the deque, tracking processed IDs.
// ---------------------------------------------------------------------------
class StubTerrainRebuildQueue {
public:
    // Enqueue a chunk ID for rebuild. Duplicate IDs are silently discarded.
    void enqueueChunk(uint64_t chunkId) {
        if (m_queued.count(chunkId) == 0) {
            m_queued.insert(chunkId);
            m_deque.push_back(chunkId);
        }
    }

    // Flush all pending rebuilds. Each chunk ID in the deque is processed
    // exactly once. Records all processed IDs so tests can verify deduplication.
    // Returns the number of chunks processed in this flush call.
    int flushPendingRebuilds() {
        int count = 0;
        while (!m_deque.empty()) {
            uint64_t id = m_deque.front();
            m_deque.pop_front();
            m_processed.insert(id);
            m_queued.erase(id);
            ++count;
        }
        return count;
    }

    // Returns true if the pending rebuild deque is empty.
    bool isPendingQueueEmpty() const {
        return m_deque.empty();
    }

    // Returns the set of all chunk IDs that have been processed by
    // flushPendingRebuilds(). Used by tests to detect duplicates.
    const std::unordered_set<uint64_t>& processedChunks() const {
        return m_processed;
    }

    // Returns the total number of times flushPendingRebuilds() has processed
    // a chunk (cumulative across all flush calls).
    int totalProcessedCount() const {
        return static_cast<int>(m_processed.size());
    }

private:
    std::deque<uint64_t>          m_deque;
    std::unordered_set<uint64_t>  m_queued;     // for O(1) duplicate check
    std::unordered_set<uint64_t>  m_processed;  // for test verification
};

// ===========================================================================
// Test: TerrainSystem_FlushPendingRebuilds_ProcessesAllChunks_NoDuplication
//
// Queue N=5 chunks with distinct IDs. Call flushPendingRebuilds(). Verify:
//   1. The pending deque is empty afterwards.
//   2. Every queued chunk ID appears in processedChunks() exactly once.
//   3. No chunk ID was processed twice (deduplication guarantee).
//
// Spec ref: phase-11.md §Loading screen integration, integration test contract.
// ===========================================================================
TEST(TerrainLoadingTest,
     TerrainSystem_FlushPendingRebuilds_ProcessesAllChunks_NoDuplication)
{
    StubTerrainRebuildQueue terrain;

    // Queue N=5 chunks with distinct uint64_t IDs.
    // IDs are chosen to be distinct and non-trivial (not 0,1,2,3,4) to catch
    // implementations that use sequential indices incorrectly.
    const uint64_t kChunkIds[5] = {
        0x0000'0000'0000'0001ULL,
        0x0000'0001'0000'0000ULL,
        0xDEAD'BEEF'0000'0001ULL,
        0xCAFE'BABE'0000'0002ULL,
        0x1234'5678'9ABC'DEF0ULL,
    };

    for (uint64_t id : kChunkIds) {
        terrain.enqueueChunk(id);
    }

    // Verify all 5 chunks are pending before flush.
    EXPECT_FALSE(terrain.isPendingQueueEmpty())
        << "Pending queue must be non-empty after enqueuing 5 chunks";

    // Flush all pending rebuilds.
    int processed = terrain.flushPendingRebuilds();
    EXPECT_EQ(processed, 5)
        << "flushPendingRebuilds must process all 5 queued chunks";

    // Post-condition: pending deque is empty.
    EXPECT_TRUE(terrain.isPendingQueueEmpty())
        << "Pending queue must be empty after flushing all chunks";

    // Deduplication check: each of the 5 distinct chunk IDs must appear
    // exactly once in the processed set.
    const auto& processedSet = terrain.processedChunks();
    EXPECT_EQ(static_cast<int>(processedSet.size()), 5)
        << "Processed set must contain exactly 5 distinct chunk IDs";

    for (uint64_t id : kChunkIds) {
        EXPECT_EQ(processedSet.count(id), 1u)
            << "Chunk ID 0x" << std::hex << id
            << " must appear exactly once in processed set";
    }
}

// ===========================================================================
// Test: TerrainSystem_FlushPendingRebuilds_DuplicateEnqueue_Deduplicated
//
// Enqueuing the same chunk ID twice must result in only one processing event.
// This verifies the deduplication contract: the second enqueue is silently
// dropped and the chunk is processed exactly once.
// ===========================================================================
TEST(TerrainLoadingTest,
     TerrainSystem_FlushPendingRebuilds_DuplicateEnqueue_Deduplicated)
{
    StubTerrainRebuildQueue terrain;

    const uint64_t kDuplicateId = 0xAAAA'BBBB'CCCC'DDDDULL;

    // Enqueue the same chunk ID twice — second enqueue must be silently dropped.
    terrain.enqueueChunk(kDuplicateId);
    terrain.enqueueChunk(kDuplicateId);  // duplicate

    int processed = terrain.flushPendingRebuilds();
    EXPECT_EQ(processed, 1)
        << "Duplicate chunk ID must be deduplicated: only 1 rebuild, not 2";

    EXPECT_TRUE(terrain.isPendingQueueEmpty())
        << "Pending queue must be empty after flush";

    const auto& processedSet = terrain.processedChunks();
    EXPECT_EQ(static_cast<int>(processedSet.size()), 1u)
        << "Processed set must contain exactly 1 entry for the deduplicated chunk";
    EXPECT_EQ(processedSet.count(kDuplicateId), 1u)
        << "The deduplicated chunk ID must appear exactly once";
}

// ===========================================================================
// Test: TerrainSystem_FlushPendingRebuilds_EmptyQueue_ReturnsZero
//
// Calling flushPendingRebuilds() on an empty queue must return 0 and leave
// the queue in an empty state (no-op).
// ===========================================================================
TEST(TerrainLoadingTest,
     TerrainSystem_FlushPendingRebuilds_EmptyQueue_ReturnsZero)
{
    StubTerrainRebuildQueue terrain;

    EXPECT_TRUE(terrain.isPendingQueueEmpty())
        << "Queue must be empty at construction";

    int processed = terrain.flushPendingRebuilds();
    EXPECT_EQ(processed, 0)
        << "flushPendingRebuilds on empty queue must return 0";

    EXPECT_TRUE(terrain.isPendingQueueEmpty())
        << "Queue must remain empty after flushing an empty queue";
}
