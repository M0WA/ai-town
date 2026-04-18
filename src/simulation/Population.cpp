// Population.cpp — population/city-rating/game-over sub-system for CitySimulation.
// Extracted verbatim from CitySimulation.cpp (Phase 11q1 decomposition).

#include "Population.h"
#include "Zoning.h"
#include "Traffic.h"
#include "Economy.h"
#include "SimTiming.h"
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/IRenderer.h"
#include "src/interfaces/IClock.h"
#include "src/interfaces/sound_ids.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>

namespace {

struct OuterTile { int x; int z; ZoneType zone; };

static bool checkZonedNeighbor(std::unordered_map<int64_t, TileData>& tiles,
                               const TileData& ft, int fx, int fz,
                               int64_t candKey, ZoneType targetZone,
                               DensityTier targetDensity,
                               std::vector<Population::DemoEntry>& toDemo) {
    int originX = (ft.footprintOriginX == -1) ? fx : ft.footprintOriginX;
    int originZ = (ft.footprintOriginZ == -1) ? fz : ft.footprintOriginZ;
    int64_t ftOriginKey = Zoning::tileKey(originX, originZ);

    auto ftOIt = tiles.find(ftOriginKey);
    if (ftOIt == tiles.end()) return true;
    const TileData& ftO = ftOIt->second;

    if (ftO.zone == targetZone && ftO.density < targetDensity) {
        if (ftOriginKey == candKey) return false;
        bool alreadyAdded = false;
        for (auto& de : toDemo) {
            if (de.originKey == ftOriginKey) { alreadyAdded = true; break; }
        }
        if (!alreadyAdded)
            toDemo.push_back({originX, originZ, ftOriginKey});
        return false;
    }
    return true;
}

static void clearFootprintCell(std::unordered_map<int64_t, TileData>& tiles,
                               int cellX, int cellZ,
                               int tx, int tz, int newN,
                               std::vector<OuterTile>& outerTiles) {
    auto tileIt = tiles.find(Zoning::tileKey(cellX, cellZ));
    if (tileIt == tiles.end()) return;
    TileData& t = tileIt->second;
    bool insideNewFP = (cellX >= tx && cellX < tx + newN &&
                        cellZ >= tz && cellZ < tz + newN);
    t.isZoned           = !insideNewFP;
    t.density           = insideNewFP ? t.density : DensityTier::Low;
    t.population        = 0.0f;
    t.footprintOriginX  = -1;
    t.footprintOriginZ  = -1;
    t.isAbandoned       = false;
    if (!insideNewFP) {
        outerTiles.push_back({cellX, cellZ, t.zone});
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

int Population::getTotalPopulation() const {
    return m_totalPopulation;
}

CityRatingTier Population::getCityRating() const {
    return m_cityRating;
}

int Population::getConsecutiveDeficitMonths() const {
    return m_consecutiveDeficitMonths;
}

DensityUnlockState Population::getDensityUnlockState() const {
    return m_densityUnlockState;
}

float Population::getNextUnlockThreshold(Difficulty d) const {
    float scale;
    switch (d) {
        case Difficulty::Easy:   scale = SimulationConstants::density_unlock_scale_easy;   break;
        case Difficulty::Normal: scale = SimulationConstants::density_unlock_scale_normal; break;
        case Difficulty::Hard:   scale = SimulationConstants::density_unlock_scale_hard;   break;
        default:                 scale = SimulationConstants::density_unlock_scale_normal; break;
    }

    auto getThreshold = [](int tierIdx) -> float {
        switch (tierIdx) {
            case 0: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_1);
            case 1: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_1);
            case 2: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_2);
            case 3: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_3);
            case 4: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_4);
            case 5: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_5);
            default: return std::numeric_limits<float>::max();
        }
    };

    for (int i = 0; i < 6; ++i) {
        if (m_densityUnlockState.unlock_flags[i]) continue;
        if (i == 3 && !m_densityUnlockState.unlock_flags[2]) continue;
        return getThreshold(i) * scale;
    }

    return SimulationConstants::kNoUnlockThreshold;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

/*static*/ float Population::computeZoneGrowthDelta(float currentPop, float demand, int maxPop) {
    float targetPop = static_cast<float>(maxPop) * demand;
    float delta     = targetPop - currentPop;
    float maxGrowth = SimulationConstants::population_growth_cap_fraction * static_cast<float>(maxPop);
    float maxDecay  = SimulationConstants::population_decay_cap_fraction  * static_cast<float>(maxPop);
    return std::max(-maxDecay, std::min(maxGrowth, delta));
}

/*static*/ int Population::maxPopulationForTile(ZoneType zone, DensityTier density) {
    return Zoning::maxPopulationForTile(zone, density);
}

/*static*/ float Population::getDensityUnlockThreshold(int tierIndex) {
    switch (tierIndex) {
        case 0: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_1);
        case 1: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_1);
        case 2: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_2);
        case 3: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_3);
        case 4: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_4);
        case 5: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_5);
        default: return std::numeric_limits<float>::max();
    }
}

// ---------------------------------------------------------------------------
// accumulateHouseDemand
// ---------------------------------------------------------------------------

void Population::accumulateHouseDemand(const Zoning& zoning,
                                        std::queue<SimulationNotification>& notifications) {
    m_totalPopulation = 0;
    for (auto& [key, tile] : zoning.tiles()) {
        if (tile.isZoned && tile.zone == ZoneType::Residential) {
            m_totalPopulation += static_cast<int>(tile.population);
        }
    }

    const int milestoneThresholds[5] = {
        SimulationConstants::population_milestone_threshold_1,
        SimulationConstants::population_milestone_threshold_2,
        SimulationConstants::population_milestone_threshold_3,
        SimulationConstants::population_milestone_threshold_4,
        SimulationConstants::population_milestone_threshold_5
    };
    for (int i = 0; i < 5; ++i) {
        if (!m_milestoneFired[i] && m_totalPopulation >= milestoneThresholds[i]) {
            m_milestoneFired[i] = true;
            notifications.push({NotificationType::PopulationMilestone, 0, 0, milestoneThresholds[i]});
        }
    }
}

// ---------------------------------------------------------------------------
// doPopulationTick
// ---------------------------------------------------------------------------

/*static*/ void Population::completeConstruction(TileData& tile, int64_t key,
                                                  const Traffic& traffic, IRenderer* renderer) {
    float effective_demand_factor = traffic.getZoneDemandFactor(tile.zone);
    if (effective_demand_factor >= SimulationConstants::construction_delay_demand_threshold) {
        tile.underConstruction = false;
        auto tileX = static_cast<int>(key >> 32);
        auto tileZ = static_cast<int>(static_cast<uint32_t>(key));
        std::string baseName = Zoning::zoneAssetBaseName(tile.zone, tile.density);
        int variantNum = tile.buildingVariantNum;
        if (baseName.size() >= 2 && variantNum >= 1 && variantNum <= 4) {
            baseName[baseName.size() - 2] = '0';
            baseName[baseName.size() - 1] = static_cast<char>('0' + variantNum);
        }
        if (renderer) {
            renderer->placeBuildingMesh(tileX, tileZ, baseName);
        }
    }
}

void Population::doPopulationTick(Zoning& zoning, const Traffic& traffic, const Economy& /*economy*/,
                                   IAudioSystem* /*audio*/, IRenderer* renderer,
                                   std::queue<SimulationNotification>& notifications) {
    for (auto& [key, tile] : zoning.tilesRef()) {
        if (!tile.isZoned || tile.isRoad) continue;

        if (tile.underConstruction && tile.footprintOriginX == -1) {
            completeConstruction(tile, key, traffic, renderer);
            if (tile.underConstruction) continue;
        }

        int maxPop = maxPopulationForTile(tile.zone, tile.density);
        float demand = traffic.getZoneDemandFactor(tile.zone);
        float delta = computeZoneGrowthDelta(tile.population, demand, maxPop);
        tile.population = std::max(0.0f, std::min(static_cast<float>(maxPop),
                                                    tile.population + delta));
    }

    accumulateHouseDemand(zoning, notifications);
}

// ---------------------------------------------------------------------------
// doDensityUnlockTick
// ---------------------------------------------------------------------------

std::vector<UpgradeCandidate>
Population::scanUnlockCandidates(const Zoning& zoning, ZoneType targetZone,
                                  DensityTier currentRequired) const {
    std::vector<UpgradeCandidate> candidates;
    for (const auto& [key, tile] : zoning.tiles()) {
        if (!tile.isZoned) continue;
        if (tile.zone != targetZone) continue;
        if (tile.density != currentRequired) continue;
        if (tile.footprintOriginX != -1) continue;
        auto tx = static_cast<int>(key >> 32);
        auto tz = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFFLL));
        candidates.push_back({key, tx, tz});
    }
    return candidates;
}

// ---------------------------------------------------------------------------
// applyDensityUpgrade — extracted helpers (Phase 11q3)
// ---------------------------------------------------------------------------

bool Population::validateUpgradeFootprint(Zoning& zoning, int tx, int tz,
                                          DensityTier targetDensity, int newN) const {
    int64_t candKey = Zoning::tileKey(tx, tz);
    bool hasBlocker = false;

    for (int dx = 0; dx < newN && !hasBlocker; ++dx) {
        for (int dz = 0; dz < newN && !hasBlocker; ++dz) {
            int fx = tx + dx, fz = tz + dz;
            if (fx < 0 || fz < 0) { hasBlocker = true; break; }
            int64_t fkey = Zoning::tileKey(fx, fz);
            if (fkey == candKey) continue;

            auto fit = zoning.m_tiles.find(fkey);
            if (fit == zoning.m_tiles.end()) { hasBlocker = true; break; }

            const TileData& ft = fit->second;
            if (ft.isRoad) { hasBlocker = true; break; }
        }
    }

    return !hasBlocker;
}

void Population::applyUpgradeFootprint(Zoning& zoning, int tx, int tz,
                                       ZoneType zone, DensityTier targetDensity, int newN) {
    int64_t candKey = Zoning::tileKey(tx, tz);

    TileData& originTile = zoning.m_tiles[candKey];
    originTile.isZoned    = true;
    originTile.density    = targetDensity;
    originTile.population = 0.0f;
    originTile.footprintOriginX = -1;
    originTile.footprintOriginZ = -1;

    for (int dx = 0; dx < newN; ++dx) {
        for (int dz = 0; dz < newN; ++dz) {
            if (dx == 0 && dz == 0) continue;
            int64_t fkey = Zoning::tileKey(tx + dx, tz + dz);
            TileData& ftile = zoning.m_tiles[fkey];
            ftile.isZoned    = true;
            ftile.isRoad     = false;
            ftile.zone       = zone;
            ftile.density    = targetDensity;
            ftile.population = 0.0f;
            ftile.desirability = static_cast<float>(SimulationConstants::desirability_base_value);
            ftile.isAbandoned  = false;
            ftile.footprintOriginX = tx;
            ftile.footprintOriginZ = tz;
        }
    }
}

void Population::notifyUpgradeResult(int tx, int tz, ZoneType zone, DensityTier targetDensity,
                                     IRenderer* renderer, IAudioSystem* audio,
                                     std::queue<SimulationNotification>& notifications,
                                     int& sfxCalls) {
    if (renderer) {
        renderer->removeBuildingMesh(tx, tz);
        // (Variant counter update and mesh placement are handled by the caller
        //  via the outer-tile loop and the main placement block below.)
    }

    if (audio && sfxCalls < SimulationConstants::sfx_zone_upgrade_per_tick_cap) {
        audio->playSound(SFX_ZONE_UPGRADE, SoundPriority::NORMAL, 1.0f);
        ++sfxCalls;
    }

    (void)zone;
    (void)targetDensity;
    (void)notifications;
}

// ---------------------------------------------------------------------------
// collectDemoTargets — Phase 11q5 extracted soft-blocker loop (S3776)
// ---------------------------------------------------------------------------

bool Population::collectDemoTargets(Zoning& zoning, int tx, int tz, int newN,
                                     ZoneType targetZone, DensityTier targetDensity,
                                     int64_t candKey,
                                     std::vector<Population::DemoEntry>& toDemo) const {
    for (int dx = 0; dx < newN; ++dx) {
        for (int dz = 0; dz < newN; ++dz) {
            int fx = tx + dx, fz = tz + dz;
            if (fx < 0 || fz < 0) return true;
            int64_t fkey = Zoning::tileKey(fx, fz);
            if (fkey == candKey) continue;

            auto fit = zoning.m_tiles.find(fkey);
            if (fit == zoning.m_tiles.end()) return true;

            const TileData& ft = fit->second;
            if (ft.isRoad) return true;
            if (ft.isZoned) {
                bool blocked = checkZonedNeighbor(zoning.m_tiles, ft, fx, fz,
                                                   candKey, targetZone, targetDensity, toDemo);
                if (blocked) return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// applyDensityUpgrade
// ---------------------------------------------------------------------------

bool Population::applyDensityUpgrade(Zoning& zoning, int tx, int tz, int64_t candKey,
                                      ZoneType targetZone, DensityTier targetDensity,
                                      DensityTier currentRequired, int& sfxCallsThisTick,
                                      IRenderer* renderer, IAudioSystem* audio,
                                      std::queue<SimulationNotification>& notifications) {
    auto it = zoning.m_tiles.find(candKey);
    if (it == zoning.m_tiles.end()) return false;
    if (!it->second.isZoned || it->second.zone != targetZone ||
        it->second.density != currentRequired) return false;

    const int newN = Zoning::footprintSize(targetDensity);

    int& retryCount = zoning.m_upgradeRetryCount[candKey];
    if (retryCount >= 12) {
        zoning.m_upgradeRetryCount.erase(candKey);
        notifications.push({NotificationType::UpgradeBlocked, tx, tz, 0});
        return false;
    }

    // Check for hard blockers (roads, out-of-bounds, missing tiles).
    if (!validateUpgradeFootprint(zoning, tx, tz, targetDensity, newN)) {
        retryCount++;
        return false;
    }

    // Check for soft blockers (zoned neighbours that can be demolished).
    std::vector<DemoEntry> toDemo;
    bool hasBlocker = collectDemoTargets(zoning, tx, tz, newN,
                                         targetZone, targetDensity,
                                         candKey, toDemo);
    if (hasBlocker) {
        retryCount++;
        return false;
    }

    std::vector<OuterTile> outerTiles;

    for (auto& de : toDemo) {
        auto dIt = zoning.m_tiles.find(de.originKey);
        if (dIt == zoning.m_tiles.end()) continue;
        const int oldN = Zoning::footprintSize(dIt->second.density);

        if (renderer) renderer->removeBuildingMesh(de.x, de.z);

        for (int ddx = 0; ddx < oldN; ++ddx) {
            for (int ddz = 0; ddz < oldN; ++ddz) {
                clearFootprintCell(zoning.m_tiles, de.x + ddx, de.z + ddz,
                                   tx, tz, newN, outerTiles);
            }
        }

        notifications.push({NotificationType::NeighbourCleared, de.x, de.z, 0});
    }

    for (auto& ot : outerTiles) {
        if (!renderer) break;
        auto zoneIdx = static_cast<int>(ot.zone);
        int idx = zoneIdx * 3 + 0;
        zoning.m_buildingVariantCounters[idx]++;
        int variantNum = ((zoning.m_buildingVariantCounters[idx] - 1) % 4) + 1;
        std::string baseName = Zoning::zoneAssetBaseName(ot.zone, DensityTier::Low);
        if (baseName.size() >= 2) {
            baseName[baseName.size() - 2] = '0';
            baseName[baseName.size() - 1] = static_cast<char>('0' + variantNum);
        }
        renderer->placeBuildingMesh(ot.x, ot.z, baseName);
    }

    zoning.m_upgradeRetryCount.erase(candKey);

    applyUpgradeFootprint(zoning, tx, tz, targetZone, targetDensity, newN);

    notifyUpgradeResult(tx, tz, targetZone, targetDensity, renderer, audio,
                        notifications, sfxCallsThisTick);

    // Place upgraded building mesh (after notifyUpgradeResult removes the old one).
    if (renderer) {
        auto zoneIdx2 = static_cast<int>(targetZone);
        auto tierIdx2 = static_cast<int>(targetDensity);
        int idx2     = zoneIdx2 * 3 + tierIdx2;
        zoning.m_buildingVariantCounters[idx2]++;
        int variantNum = ((zoning.m_buildingVariantCounters[idx2] - 1) % 4) + 1;
        std::string baseName = Zoning::zoneAssetBaseName(targetZone, targetDensity);
        if (baseName.size() >= 2) {
            baseName[baseName.size() - 2] = '0';
            baseName[baseName.size() - 1] = static_cast<char>('0' + variantNum);
        }
        renderer->placeBuildingMesh(tx, tz, baseName);
    }

    return true;
}

/*static*/ float Population::difficultyToUnlockScale(Difficulty d) {
    switch (d) {
        case Difficulty::Easy:   return SimulationConstants::density_unlock_scale_easy;
        case Difficulty::Normal: return SimulationConstants::density_unlock_scale_normal;
        case Difficulty::Hard:   return SimulationConstants::density_unlock_scale_hard;
        default:                 return SimulationConstants::density_unlock_scale_normal;
    }
}

void Population::tickUnlockProgress(const Economy& economy, float scale) {
    for (int i = 0; i < 6; ++i) {
        if (m_densityUnlockState.unlock_flags[i]) continue;
        if (i == 3 && !m_densityUnlockState.unlock_flags[2]) continue;

        const float threshold = getDensityUnlockThreshold(i) * scale;
        if (static_cast<float>(economy.m_treasury) >= threshold) {
            m_densityUnlockState.consecutive_months_above_threshold[i]++;
            if (m_densityUnlockState.consecutive_months_above_threshold[i] >= 3) {
                m_densityUnlockState.unlock_flags[i] = true;
                m_densityUnlockState.consecutive_months_above_threshold[i] = 0;
            }
        } else {
            m_densityUnlockState.consecutive_months_above_threshold[i] = 0;
        }
    }
}

void Population::processUpgradeWave(Zoning& zoning, const Traffic& traffic,
                                     const bool* wasAlreadyUnlocked,
                                     IRenderer* renderer, IAudioSystem* audio,
                                     int& sfxCallsThisTick,
                                     std::queue<SimulationNotification>& notifications) {
    struct TierMapping { ZoneType zone; DensityTier target; DensityTier current; };
    static constexpr TierMapping kTierMap[6] = {
        {ZoneType::Residential, DensityTier::Medium, DensityTier::Low},
        {ZoneType::Commercial,  DensityTier::Medium, DensityTier::Low},
        {ZoneType::Industrial,  DensityTier::Medium, DensityTier::Low},
        {ZoneType::Residential, DensityTier::High,   DensityTier::Medium},
        {ZoneType::Commercial,  DensityTier::High,   DensityTier::Medium},
        {ZoneType::Industrial,  DensityTier::High,   DensityTier::Medium},
    };

    for (int tierIdx = 0; tierIdx < 6; ++tierIdx) {
        if (!m_densityUnlockState.unlock_flags[tierIdx]) continue;
        if (!wasAlreadyUnlocked[tierIdx]) continue;

        const auto& tm = kTierMap[tierIdx];

        const float demandForZone = traffic.getZoneDemandFactor(tm.zone);
        if (demandForZone < SimulationConstants::density_upgrade_wave_demand_threshold) continue;

        int totalZoneTiles = 0;
        for (const auto& [key, tile] : zoning.tiles()) {
            if (tile.isZoned && tile.zone == tm.zone) totalZoneTiles++;
        }
        // Scale upgrade quota linearly by how far demand exceeds the gate threshold.
        // demandExcess == 0 at threshold → near-zero rate (trickle of 1 tile).
        // demandExcess == 1 at peak demand → full density_max_upgrade_rate_per_tick.
        float demandExcess = (demandForZone - SimulationConstants::density_upgrade_wave_demand_threshold)
                             / (1.0f - SimulationConstants::density_upgrade_wave_demand_threshold);
        demandExcess = std::min(1.0f, demandExcess);   // hard cap: never exceed peak-demand rate
        float scaledRate  = demandExcess * SimulationConstants::density_max_upgrade_rate_per_tick;
        int maxUpgrades   = std::max(1, static_cast<int>(scaledRate * static_cast<float>(totalZoneTiles)));

        std::vector<UpgradeCandidate> candidates = scanUnlockCandidates(zoning, tm.zone, tm.current);

        int upgradeCount = 0;
        for (auto& cand : candidates) {
            if (upgradeCount >= maxUpgrades) break;
            if (applyDensityUpgrade(zoning, cand.tx, cand.tz, cand.key,
                                    tm.zone, tm.target, tm.current,
                                    sfxCallsThisTick, renderer, audio, notifications)) {
                ++upgradeCount;
            }
        }
    }
}

void Population::doDensityUnlockTick(Zoning& zoning, const Traffic& traffic, const Economy& economy,
                                      Difficulty difficulty, IRenderer* renderer, IAudioSystem* audio,
                                      std::queue<SimulationNotification>& notifications) {
    int sfxCallsThisTick = 0;
    float scale = difficultyToUnlockScale(difficulty);
    bool wasAlreadyUnlocked[6];
    for (int i = 0; i < 6; ++i) wasAlreadyUnlocked[i] = m_densityUnlockState.unlock_flags[i];
    tickUnlockProgress(economy, scale);
    processUpgradeWave(zoning, traffic, wasAlreadyUnlocked, renderer, audio, sfxCallsThisTick, notifications);
}

// ---------------------------------------------------------------------------
// doGameOverTick
// ---------------------------------------------------------------------------

void Population::doGameOverTick(const Economy& economy, SimTiming& timing, IClock& clock) {
    if ((clock.nowSeconds() - timing.getConstructionTimeSeconds()) <
        SimulationConstants::grace_period_real_seconds) {
        return;
    }

    if (economy.getBudgetSurplusPct() <= -0.50f) {
        m_consecutiveDeficitMonths++;
        if (m_consecutiveDeficitMonths == 1 && !timing.isPaused()) {
            timing.setSpeed(SpeedMultiplier::x1);
            m_month1AutoSlowed = true;
        }
    } else {
        m_consecutiveDeficitMonths = 0;
        m_month1AutoSlowed = false;
    }
}

// ---------------------------------------------------------------------------
// checkCityRatingTransition
// ---------------------------------------------------------------------------

void Population::checkCityRatingTransition(std::queue<SimulationNotification>& notifications) {
    CityRatingTier newTier;
    if (m_totalPopulation >= SimulationConstants::city_rating_megalopolis_threshold) {
        newTier = CityRatingTier::Megalopolis;
    } else if (m_totalPopulation >= SimulationConstants::city_rating_metropolis_threshold) {
        newTier = CityRatingTier::Metropolis;
    } else if (m_totalPopulation >= SimulationConstants::city_rating_city_threshold) {
        newTier = CityRatingTier::City;
    } else if (m_totalPopulation >= SimulationConstants::city_rating_town_threshold) {
        newTier = CityRatingTier::Town;
    } else {
        newTier = CityRatingTier::Village;
    }

    if (newTier != m_cityRating) {
        notifications.push({
            NotificationType::CityRatingTransition,
            0,
            0,
            static_cast<int>(newTier)
        });
        m_cityRating = newTier;
    }
}

// ---------------------------------------------------------------------------
// updateMusicIntensity
// ---------------------------------------------------------------------------

void Population::updateMusicIntensity(const Economy& /*economy*/, IAudioSystem* audio) {
    if (!audio) return;

    MusicIntensity intensity = MusicIntensity::CALM;
    if (m_consecutiveDeficitMonths >= 2) {
        intensity = MusicIntensity::CRISIS;
    } else if (m_totalPopulation > m_prevPopulation &&
               m_consecutiveDeficitMonths == 0) {
        intensity = MusicIntensity::GROWTH;
    }
    if (intensity != m_lastSentMusicIntensity) {
        audio->setMusicIntensity(intensity);
        m_lastSentMusicIntensity = intensity;
    }

    m_prevPopulation = m_totalPopulation;
}

// ---------------------------------------------------------------------------
// testForceUnlockDensityTier
// ---------------------------------------------------------------------------

#ifdef AITOWN_TESTING_ENABLED
void Population::testForceUnlockDensityTier(ZoneType zone, DensityTier tier) {
    int tierIdx = -1;
    if (tier == DensityTier::Medium) {
        switch (zone) {
            case ZoneType::Residential: tierIdx = 0; break;
            case ZoneType::Commercial:  tierIdx = 1; break;
            case ZoneType::Industrial:  tierIdx = 2; break;
        }
    } else if (tier == DensityTier::High) {
        switch (zone) {
            case ZoneType::Residential: tierIdx = 3; break;
            case ZoneType::Commercial:  tierIdx = 4; break;
            case ZoneType::Industrial:  tierIdx = 5; break;
        }
    }
    if (tierIdx < 0 || tierIdx > 5) return;

    m_densityUnlockState.unlock_flags[tierIdx] = true;
    m_densityUnlockState.consecutive_months_above_threshold[tierIdx] = 0;
}
#endif
