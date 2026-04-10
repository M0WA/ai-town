#pragma once
#include "simulation_constants.h"
#include "src/interfaces/simulation_types.h"
#include "src/interfaces/audio_types.h"

#include <queue>
#include <vector>

// Forward declarations for cross-system references
struct TileData;
class Zoning;
class Traffic;
class Economy;
class SimTiming;
class IAudioSystem;
class IRenderer;
class IClock;

// UpgradeCandidate — used by doDensityUnlockTick / scanUnlockCandidates.
// Moved from CitySimulation.h (Phase 11q1 decomposition).
struct UpgradeCandidate {
    int64_t key;
    int tx;
    int tz;
};

// Population — sub-system owning population/city-rating/game-over state.
// Extracted from CitySimulation as part of Phase 11q1 decomposition.
class Population {
public:
    // ---- Fields ----
    int            m_totalPopulation{0};
    int            m_prevPopulation{0};
    CityRatingTier m_cityRating{CityRatingTier::Village};
    bool           m_milestoneFired[5]{};
    int            m_consecutiveDeficitMonths{0};
    bool           m_month1AutoSlowed{false};
    MusicIntensity m_lastSentMusicIntensity{MusicIntensity::CALM};
    DensityUnlockState m_densityUnlockState{};

    // ---- Public accessors ----
    int              getTotalPopulation()          const;
    CityRatingTier   getCityRating()               const;
    int              getConsecutiveDeficitMonths()  const;
    DensityUnlockState getDensityUnlockState()     const;
    float            getNextUnlockThreshold(Difficulty d) const;

    // ---- Tick methods ----
    void doPopulationTick(Zoning& zoning, const Traffic& traffic, const Economy& economy,
                          IAudioSystem* audio, IRenderer* renderer,
                          std::queue<SimulationNotification>& notifications);
    void doDensityUnlockTick(Zoning& zoning, const Traffic& traffic, const Economy& economy,
                             Difficulty difficulty, IRenderer* renderer, IAudioSystem* audio,
                             std::queue<SimulationNotification>& notifications);
    void doGameOverTick(const Economy& economy, SimTiming& timing, IClock& clock);
    void checkCityRatingTransition(std::queue<SimulationNotification>& notifications);
    void updateMusicIntensity(const Economy& economy, IAudioSystem* audio);

    // Phase 11q5 — promoted from anonymous namespace; used by collectDemoTargets
    struct DemoEntry { int x; int z; int64_t originKey; };

#ifdef AITOWN_TESTING_ENABLED
    void testForceUnlockDensityTier(ZoneType zone, DensityTier tier);
#endif

private:
    void accumulateHouseDemand(const Zoning& zoning,
                               std::queue<SimulationNotification>& notifications);
    static float computeZoneGrowthDelta(float currentPop, float demand, int maxPop);
    static int   maxPopulationForTile(ZoneType zone, DensityTier density);
    static float getDensityUnlockThreshold(int tierIndex);

    std::vector<UpgradeCandidate> scanUnlockCandidates(const Zoning& zoning,
                                                        ZoneType targetZone,
                                                        DensityTier currentRequired) const;
    bool applyDensityUpgrade(Zoning& zoning, int tx, int tz, int64_t candKey,
                             ZoneType targetZone, DensityTier targetDensity,
                             DensityTier currentRequired, int& sfxCallsThisTick,
                             IRenderer* renderer, IAudioSystem* audio,
                             std::queue<SimulationNotification>& notifications);

    static void completeConstruction(TileData& tile, int64_t key,
                                     const Traffic& traffic, IRenderer* renderer);
    static float difficultyToUnlockScale(Difficulty d);
    void tickUnlockProgress(const Economy& economy, float scale);
    void processUpgradeWave(Zoning& zoning, const Traffic& traffic,
                            const bool* wasAlreadyUnlocked,
                            IRenderer* renderer, IAudioSystem* audio,
                            int& sfxCallsThisTick,
                            std::queue<SimulationNotification>& notifications);

    // Phase 11q5 — soft-blocker collection helper for applyDensityUpgrade (S3776)
    bool collectDemoTargets(Zoning& zoning, int tx, int tz, int newN,
                            ZoneType targetZone, DensityTier targetDensity,
                            int64_t candKey,
                            std::vector<DemoEntry>& toDemo) const;

    // Phase 11q3 — extracted helpers for applyDensityUpgrade (S3776)
    bool validateUpgradeFootprint(Zoning& zoning, int tx, int tz,
                                  DensityTier targetDensity, int newN) const;
    void applyUpgradeFootprint(Zoning& zoning, int tx, int tz,
                               ZoneType zone, DensityTier targetDensity, int newN);
    void notifyUpgradeResult(int tx, int tz, ZoneType zone, DensityTier targetDensity,
                             IRenderer* renderer, IAudioSystem* audio,
                             std::queue<SimulationNotification>& notifications,
                             int& sfxCalls);
};
