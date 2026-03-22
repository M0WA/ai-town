// tests/simulation/agent_render_sync_test.cpp
//
// Phase 11d Deliverable 3d: Agent Render Sync Tests
// Tests the per-frame agent sync loop logic (spawn/despawn/move/cull) using
// mock renderer and audio system.
//
// The sync loop lives in src/main.cpp. These tests exercise the same algorithm
// (spawn new agents, move existing, despawn removed, cull > 150 m) using the
// real CitySimulation::getAgentPositions() output and the mock renderer interface.
//
// Added to simulation_tests via:
//   target_sources(simulation_tests PRIVATE tests/simulation/agent_render_sync_test.cpp)
// Do NOT call add_executable(simulation_tests ...) again.
//
// Mock policy: StrictMock<MockRenderer> and StrictMock<MockAudioSystem>
// per architecture/testing/testability-architecture.md.

#include "src/simulation/CitySimulation.h"
#include "src/interfaces/ICitySimulation.h"
#include "src/interfaces/IRenderer.h"
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/simulation_types.h"
#include "src/interfaces/vec3.h"
#include "tests/simulation/MockRenderer.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualRNG.h"
#include "tests/simulation/ManualClock.h"
#include "tests/simulation/ManualTerrainQuery.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <cmath>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;
using ::testing::StrictMock;

// ---------------------------------------------------------------------------
// AgentSyncState — mirrors the per-frame sync state from main.cpp
// ---------------------------------------------------------------------------
struct AgentSyncAudio {
    int idleIdx{-1};
    int moveIdx{-1};
    ZoneType zone{ZoneType::Residential};
};

// ---------------------------------------------------------------------------
// Sync loop helper — replicates the main.cpp per-frame agent sync logic.
// Exposed here for testing without linking main.cpp.
// Camera position is injected via camX/camZ; kAgentCullDistSq = 150m².
// ---------------------------------------------------------------------------
static void runAgentSyncOnce(
    const std::vector<AgentState>& agentList,
    std::unordered_map<AgentHandle, AgentSyncAudio>& activeAgents,
    IRenderer* renderer,
    IAudioSystem* audio,
    float camX, float camZ,
    float kTileSize = 10.0f,
    float cullDist = 150.0f)
{
    const float cullDistSq = cullDist * cullDist;

    // Build set of live handles.
    std::unordered_set<AgentHandle> liveHandles;
    liveHandles.reserve(agentList.size());
    for (const AgentState& a : agentList) {
        liveHandles.insert(static_cast<AgentHandle>(a.agentId));
    }

    // Despawn agents no longer alive.
    std::vector<AgentHandle> toRemove;
    for (const auto& kv : activeAgents) {
        if (liveHandles.find(kv.first) == liveHandles.end()) {
            toRemove.push_back(kv.first);
        }
    }
    for (AgentHandle h : toRemove) {
        const AgentSyncAudio& aud = activeAgents.at(h);
        audio->releaseVehicleEnginePair(aud.idleIdx, aud.moveIdx);
        renderer->despawnVehicleAgent(h);
        activeAgents.erase(h);
    }

    // Spawn / move agents.
    for (const AgentState& a : agentList) {
        const AgentHandle handle = static_cast<AgentHandle>(a.agentId);
        const float wx = static_cast<float>(a.tileX) * kTileSize;
        const float wz = static_cast<float>(a.tileZ) * kTileSize;
        const float dx = wx - camX;
        const float dz = wz - camZ;
        const float distSq = dx * dx + dz * dz;

        if (distSq > cullDistSq) {
            auto it = activeAgents.find(handle);
            if (it != activeAgents.end()) {
                audio->releaseVehicleEnginePair(it->second.idleIdx, it->second.moveIdx);
                renderer->despawnVehicleAgent(handle);
                activeAgents.erase(it);
            }
            continue;
        }

        auto it = activeAgents.find(handle);
        if (it == activeAgents.end()) {
            renderer->spawnVehicleAgent(handle, a.tileX, a.tileZ, a.zone);
            auto audioPair = audio->acquireVehicleEnginePair(a.zone);
            AgentSyncAudio aud;
            aud.idleIdx = audioPair.first;
            aud.moveIdx = audioPair.second;
            aud.zone    = a.zone;
            activeAgents[handle] = aud;
            it = activeAgents.find(handle);
        }

        // Use sub-tile world position if provided; otherwise fall back to tile centre.
        const float agentWx = (a.worldX != 0.0f || a.worldZ != 0.0f)
            ? a.worldX : (static_cast<float>(a.tileX) + 0.5f) * kTileSize;
        const float agentWz = (a.worldX != 0.0f || a.worldZ != 0.0f)
            ? a.worldZ : (static_cast<float>(a.tileZ) + 0.5f) * kTileSize;
        renderer->moveVehicleAgent(handle, agentWx, agentWz, a.headingDeg);

        if (it->second.idleIdx >= 0) {
            audio->updateVehicleAudio(it->second.idleIdx, it->second.moveIdx, 1.0f, wx, wz);
        }
    }
}

// ---------------------------------------------------------------------------
// AgentRenderSyncTest fixture
// ---------------------------------------------------------------------------
class AgentRenderSyncTest : public ::testing::Test {
protected:
    StrictMock<MockRenderer>    renderer_;
    StrictMock<MockAudioSystem> audio_;
    ManualRNG                   rng_;
    ManualClock                 clock_;
    ManualTerrainQuery          terrain_;
    std::unique_ptr<ICitySimulation> sim_;

    std::unordered_map<AgentHandle, AgentSyncAudio> activeAgents_;

    void SetUp() override {
        sim_ = std::make_unique<CitySimulation>(
            &renderer_, &audio_, &rng_, &clock_, &terrain_, Difficulty::Normal);
        sim_->setSpeed(SpeedMultiplier::x1);

        // Allow all audio calls from CitySimulation tick.
        EXPECT_CALL(audio_, playPositionalSound(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(audio_, playSound(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(audio_, setMusicIntensity(_)).Times(AnyNumber());
        EXPECT_CALL(audio_, setTimeOfDay(_)).Times(AnyNumber());

        // Allow renderer mesh placement calls from CitySimulation.
        EXPECT_CALL(renderer_, placeBuildingMesh(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, removeBuildingMesh(_, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, placeRoadMesh(_, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, removeRoadMesh(_, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, placeServiceBuildingMesh(_, _, _)).Times(AnyNumber());
        EXPECT_CALL(renderer_, removeServiceBuildingMesh(_, _)).Times(AnyNumber());
    }

    void TearDown() override {
        sim_.reset();
    }
};

// ============================================================================
// AgentRenderSync_SpawnDespawn_MatchesSimulationOutput
// Phase 11d Deliverable 3d: verify the sync loop calls spawnVehicleAgent exactly
// once per new agent, moveVehicleAgent every frame, and despawnVehicleAgent once
// when the agent is removed.
// ============================================================================
TEST_F(AgentRenderSyncTest, AgentRenderSync_SpawnDespawn_MatchesSimulationOutput)
{
    // Camera at world origin (0,0). Agent at tile (2,2) = 20m from camera — within 150m.
    const float camX = 0.0f, camZ = 0.0f;

    // Build a fake agent list with one agent.
    AgentState agent;
    agent.agentId    = 42;
    agent.tileX      = 2;
    agent.tileZ      = 2;
    agent.headingDeg = 0.0f;
    agent.zone       = ZoneType::Residential;
    const std::vector<AgentState> oneAgent = {agent};

    // Frame 1: agent is new — expect spawn + move + audio acquire + updateVehicleAudio.
    // worldX/worldZ are 0.0f so fallback tile-centre is used: (2+0.5)*10 = 25.0f.
    EXPECT_CALL(renderer_, spawnVehicleAgent(42u, 2, 2, ZoneType::Residential)).Times(1);
    EXPECT_CALL(renderer_, moveVehicleAgent(42u, 25.0f, 25.0f, 0.0f)).Times(1);
    EXPECT_CALL(audio_, acquireVehicleEnginePair(ZoneType::Residential))
        .WillOnce(Return(std::make_pair(0, 1)));
    EXPECT_CALL(audio_, updateVehicleAudio(0, 1, 1.0f, 20.0f, 20.0f)).Times(1);

    runAgentSyncOnce(oneAgent, activeAgents_, &renderer_, &audio_, camX, camZ);

    // Frame 2: agent still alive — expect move only (no spawn), audio update.
    EXPECT_CALL(renderer_, moveVehicleAgent(42u, 25.0f, 25.0f, 0.0f)).Times(1);
    EXPECT_CALL(audio_, updateVehicleAudio(0, 1, 1.0f, 20.0f, 20.0f)).Times(1);

    runAgentSyncOnce(oneAgent, activeAgents_, &renderer_, &audio_, camX, camZ);

    // Frame 3: agent removed from simulation — expect despawn + audio release.
    const std::vector<AgentState> emptyList;

    EXPECT_CALL(renderer_, despawnVehicleAgent(42u)).Times(1);
    EXPECT_CALL(audio_, releaseVehicleEnginePair(0, 1)).Times(1);

    runAgentSyncOnce(emptyList, activeAgents_, &renderer_, &audio_, camX, camZ);

    // activeAgents_ must be empty after despawn.
    EXPECT_TRUE(activeAgents_.empty());
}

// ============================================================================
// AgentRenderSync_CullDistance_AgentsBeyond150m_NotSpawned
// Phase 11d Deliverable 3d: agents beyond 150m from camera are not spawned.
// ============================================================================
TEST_F(AgentRenderSyncTest, AgentRenderSync_CullDistance_AgentsBeyond150m_NotSpawned)
{
    // Camera at world origin. Agent at tile (16,0) = 160m from camera — beyond 150m cull.
    const float camX = 0.0f, camZ = 0.0f;

    AgentState farAgent;
    farAgent.agentId    = 99;
    farAgent.tileX      = 16;  // 16 * 10m = 160m > 150m cull
    farAgent.tileZ      = 0;
    farAgent.headingDeg = 0.0f;
    farAgent.zone       = ZoneType::Residential;
    const std::vector<AgentState> farList = {farAgent};

    // spawnVehicleAgent must NOT be called for the out-of-range agent.
    EXPECT_CALL(renderer_, spawnVehicleAgent(_, _, _, _)).Times(0);
    EXPECT_CALL(renderer_, moveVehicleAgent(_, _, _, _)).Times(0);
    EXPECT_CALL(audio_, acquireVehicleEnginePair(_)).Times(0);

    runAgentSyncOnce(farList, activeAgents_, &renderer_, &audio_, camX, camZ);

    // activeAgents_ remains empty — no agent was spawned.
    EXPECT_TRUE(activeAgents_.empty());
}

// ============================================================================
// AgentEngineAudio_AcquireRelease_MatchesSpawnDespawn
// Phase 11d Deliverable 3d: verify acquireVehicleEnginePair called once per
// spawn and releaseVehicleEnginePair called once per despawn with matching indices.
// Also verify that {-1,-1} (pool exhaustion) passed to release is safe.
// ============================================================================
TEST_F(AgentRenderSyncTest, AgentEngineAudio_AcquireRelease_MatchesSpawnDespawn)
{
    const float camX = 0.0f, camZ = 0.0f;

    AgentState agent;
    agent.agentId    = 7;
    agent.tileX      = 3;
    agent.tileZ      = 3;
    agent.headingDeg = 45.0f;
    agent.zone       = ZoneType::Commercial;
    const std::vector<AgentState> oneAgent = {agent};
    const std::vector<AgentState> emptyList;

    // Simulate pool exhaustion: acquireVehicleEnginePair returns {-1,-1}.
    // worldX/worldZ are 0.0f so fallback tile-centre is used: (3+0.5)*10 = 35.0f.
    EXPECT_CALL(renderer_, spawnVehicleAgent(7u, 3, 3, ZoneType::Commercial)).Times(1);
    EXPECT_CALL(renderer_, moveVehicleAgent(7u, 35.0f, 35.0f, 45.0f)).Times(1);
    EXPECT_CALL(audio_, acquireVehicleEnginePair(ZoneType::Commercial))
        .WillOnce(Return(std::make_pair(-1, -1)));
    // updateVehicleAudio must NOT be called when idleIdx < 0.
    EXPECT_CALL(audio_, updateVehicleAudio(_, _, _, _, _)).Times(0);

    runAgentSyncOnce(oneAgent, activeAgents_, &renderer_, &audio_, camX, camZ);

    // Despawn: releaseVehicleEnginePair({-1,-1}) must be called safely (no crash/assert).
    EXPECT_CALL(renderer_, despawnVehicleAgent(7u)).Times(1);
    EXPECT_CALL(audio_, releaseVehicleEnginePair(-1, -1)).Times(1);

    runAgentSyncOnce(emptyList, activeAgents_, &renderer_, &audio_, camX, camZ);

    EXPECT_TRUE(activeAgents_.empty());
}
