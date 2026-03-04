// CitySimulation.cpp — Full V1 simulation engine implementation for AI Town.
//
// Implements: ICitySimulation (economy, traffic, zoning, population, service coverage, undo).
// See CitySimulation.h for the class declaration and all private member documentation.
//
// Source: src/simulation/CitySimulation.cpp

#include "CitySimulation.h"
#include "simulation_constants.h"
#include "sound_ids.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>
#include <queue>

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

// zoneAssetBaseName — map (ZoneType, DensityTier) to the Phase 10 _01 asset
// base name used by IRenderer::placeBuildingMesh().
// Phase 10 policy (locked): always use the _01 suffix; variant cycling (_02, _03)
// is deferred to Phase 11.  Returns empty string on unknown combination.
/*static*/ std::string CitySimulation::zoneAssetBaseName(ZoneType zone, DensityTier density) {
    const char* zoneStr  = nullptr;
    const char* densStr  = nullptr;
    switch (zone) {
        case ZoneType::Residential: zoneStr = "res"; break;
        case ZoneType::Commercial:  zoneStr = "com"; break;
        case ZoneType::Industrial:  zoneStr = "ind"; break;
    }
    switch (density) {
        case DensityTier::Low:    densStr = "low";  break;
        case DensityTier::Medium: densStr = "med";  break;
        case DensityTier::High:   densStr = "high"; break;
    }
    if (!zoneStr || !densStr) return {};
    return std::string(zoneStr) + "_" + densStr + "_01";
}

/*static*/ float CitySimulation::speedValue(SpeedMultiplier s) {
    switch (s) {
        case SpeedMultiplier::Paused: return 0.0f;
        case SpeedMultiplier::x1:    return 1.0f;
        case SpeedMultiplier::x3:    return 3.0f;
        case SpeedMultiplier::x10:   return 10.0f;
    }
    return 0.0f;
}

/*static*/ int64_t CitySimulation::tileKey(int x, int z) {
    return (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(z);
}

CitySimulation::TileData* CitySimulation::findTile(int x, int z) {
    auto it = m_tiles.find(tileKey(x, z));
    return (it != m_tiles.end()) ? &it->second : nullptr;
}

const CitySimulation::TileData* CitySimulation::findTile(int x, int z) const {
    auto it = m_tiles.find(tileKey(x, z));
    return (it != m_tiles.end()) ? &it->second : nullptr;
}

/*static*/ float CitySimulation::smoothstep(float t) {
    // Standard cubic S-curve: 3t^2 - 2t^3
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/*static*/ float CitySimulation::travelTimeDemand(float t, float fullTime, float zeroTime) {
    // Returns smoothstep-based demand [0,1]:
    //   t <= fullTime  → 1.0f (full demand)
    //   t >= zeroTime  → 0.0f (zero demand)
    //   otherwise      → smoothstep interpolation
    if (t <= fullTime) return 1.0f;
    if (t >= zeroTime) return 0.0f;
    float x = (zeroTime - t) / (zeroTime - fullTime);
    return smoothstep(x);
}

/*static*/ int CitySimulation::maxPopulationForTile(ZoneType zone, DensityTier density) {
    switch (zone) {
        case ZoneType::Residential:
            switch (density) {
                case DensityTier::Low:    return 100;
                case DensityTier::Medium: return 400;
                case DensityTier::High:   return 1000;
            }
            break;
        case ZoneType::Commercial:
            switch (density) {
                case DensityTier::Low:    return 25;
                case DensityTier::Medium: return 100;
                case DensityTier::High:   return 300;
            }
            break;
        case ZoneType::Industrial:
            switch (density) {
                case DensityTier::Low:    return 50;
                case DensityTier::Medium: return 200;
                case DensityTier::High:   return 600;
            }
            break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

CitySimulation::CitySimulation(IRenderer*      renderer,
                               IAudioSystem*   audio,
                               ISimulationRNG* rng,
                               IClock*         clock,
                               ITerrainQuery*  terrain,
                               Difficulty      difficulty)
    : m_renderer(renderer)
    , m_audio(audio)
    , m_rng(rng)
    , m_clock(clock)
    , m_terrain(terrain)
    , m_difficulty(difficulty)
{
    // Starting funds by difficulty
    switch (difficulty) {
        case Difficulty::Easy:   m_treasury = SimulationConstants::starting_funds_easy;   break;
        case Difficulty::Normal: m_treasury = SimulationConstants::starting_funds_normal; break;
        case Difficulty::Hard:   m_treasury = SimulationConstants::starting_funds_hard;   break;
    }

    // Bond uses by difficulty
    switch (difficulty) {
        case Difficulty::Easy:   m_outstandingBondUses = SimulationConstants::bond_max_uses_easy;   break;
        case Difficulty::Normal: m_outstandingBondUses = SimulationConstants::bond_max_uses_normal; break;
        case Difficulty::Hard:   m_outstandingBondUses = SimulationConstants::bond_max_uses_hard;   break;
    }

    // Default speed: x3 (kDefaultSimSpeed = SpeedMultiplier::x3)
    m_speed = kDefaultSimSpeed;

    // Default tax rates: 5% for all zone types
    m_taxRates[0] = 0.05f;
    m_taxRates[1] = 0.05f;
    m_taxRates[2] = 0.05f;

    // Initialize traffic rolling windows to 0.0 (zero-initialised, unwritten slots
    // excluded via partial-window averaging: divisor = min(totalTicks, window_size)).
    // Pre-filling with null_path_demand_default would make sumR = 5×0.5 = 2.5 on
    // tick 0 while the partial-window divisor is 1, producing a factor of 2.5 and
    // violating the [0,1] interface contract.
    for (int i = 0; i < SimulationConstants::traffic_rolling_window_r_c; ++i) {
        m_trafficWindowR[i] = 0.0f;
        m_trafficWindowC[i] = 0.0f;
    }
    for (int i = 0; i < SimulationConstants::traffic_rolling_window_i; ++i) {
        m_trafficWindowI[i] = 0.0f;
    }
    m_trafficDemandFactorR = SimulationConstants::null_path_demand_default;
    m_trafficDemandFactorC = SimulationConstants::null_path_demand_default;
    m_trafficDemandFactorI = SimulationConstants::null_path_demand_default;

    // Record wall-clock time at construction for clock-based grace period checks
    m_constructionTimeSeconds = m_clock->nowSeconds();
}

// ---------------------------------------------------------------------------
// Speed / pause control
// ---------------------------------------------------------------------------

void CitySimulation::setPaused(bool paused) {
    if (paused) {
        m_speed = SpeedMultiplier::Paused;
    } else {
        if (m_speed == SpeedMultiplier::Paused) {
            m_speed = SpeedMultiplier::x1;
        }
    }
}

void CitySimulation::setSpeed(SpeedMultiplier speed) {
    m_speed = speed;
}

bool CitySimulation::isPaused() const {
    return m_speed == SpeedMultiplier::Paused;
}

SpeedMultiplier CitySimulation::getSpeedMultiplier() const {
    return m_speed;
}

// ---------------------------------------------------------------------------
// Main simulation tick
// ---------------------------------------------------------------------------

void CitySimulation::tick(float realDeltaSeconds) {
    // Paused: no accumulation, no ticks
    if (m_speed == SpeedMultiplier::Paused) {
        return;
    }

    // Accumulate sim seconds
    float sv = speedValue(m_speed);
    m_accumulatedSimSeconds += realDeltaSeconds * sv;

    // Fire budget ticks while accumulated >= threshold
    while (m_accumulatedSimSeconds >= SimulationConstants::SECONDS_PER_BUDGET_TICK) {
        m_accumulatedSimSeconds -= SimulationConstants::SECONDS_PER_BUDGET_TICK;
        m_totalTicks++;

        // Advance in-game month/year
        m_month++;
        if (m_month > 12) {
            m_month = 1;
            m_year++;
        }

        // Advance in-game hours accumulator
        // 1 budget tick = 1 in-game month = 30 in-game days = 720 in-game hours
        // TimeOfDay cycle: DAY (6:00-17:59), DUSK (18:00-19:59), NIGHT (20:00-5:59), DAWN (6:00-7:59)
        // We track hours modulo 24 per in-game day. Each budget tick = 720 hours elapsed.
        // For simplicity, we advance the hour accumulator and update TimeOfDay accordingly.
        // Each budget tick at 1x speed = 30 real seconds = 30 in-game days.
        // hours per budget tick = 30 * 24 = 720 in-game hours
        // We cycle through 24-hour days. Track total hours modulo 24.
        m_hoursAccumulator += 720.0f; // 720 in-game hours per budget tick
        float dayHours = std::fmod(m_hoursAccumulator, 24.0f);
        // Time of day thresholds (simplified 4-phase cycle):
        // DAY:   6:00 - 17:59
        // DUSK: 18:00 - 19:59
        // NIGHT: 20:00 - 3:59 (wraps)
        // DAWN:  4:00 -  5:59 (precedes DAY in the cycle)
        TimeOfDay prevTimeOfDay = m_timeOfDay;
        if (dayHours >= 6.0f && dayHours < 18.0f) {
            m_timeOfDay = TimeOfDay::DAY;
        } else if (dayHours >= 18.0f && dayHours < 20.0f) {
            m_timeOfDay = TimeOfDay::DUSK;
        } else if (dayHours >= 4.0f && dayHours < 6.0f) {
            m_timeOfDay = TimeOfDay::DAWN;
        } else {
            // dayHours >= 20.0f || dayHours < 4.0f
            m_timeOfDay = TimeOfDay::NIGHT;
        }

        // Phase 10: notify AudioSystem whenever time-of-day changes.
        // IAudioSystem::setTimeOfDay() is idempotent when called with the same value
        // but we only call on transition to avoid unnecessary crossfade commands.
        if (m_audio && m_timeOfDay != prevTimeOfDay) {
            m_audio->setTimeOfDay(m_timeOfDay);
        }

        doBudgetTick();

        // Phase 10: setMusicIntensity() — called after doBudgetTick() so that
        // m_consecutiveDeficitMonths, m_budgetSurplusPct, and m_totalPopulation
        // all reflect the just-completed tick.
        //
        // Priority order (highest wins): CRISIS > GROWTH > CALM.
        //   CRISIS:  consecutive_deficit_months >= 2 (economy-model.md Music Intensity Tiers)
        //   GROWTH:  net population this tick increased AND no deficit streak
        //             (m_totalPopulation > m_prevPopulation && m_consecutiveDeficitMonths == 0)
        //   CALM:    all other cases (normal/surplus budget, no pop increase)
        //
        // Time-of-day forced-Calm override (DUSK/NIGHT/DAWN) is applied internally
        // by AudioSystem; CitySimulation does NOT suppress GROWTH/CRISIS calls during
        // off-hours.
        //
        // Edge-detect: only call setMusicIntensity() when tier changes — avoids
        // redundant audio thread atomic stores every single tick.
        if (m_audio) {
            MusicIntensity intensity = MusicIntensity::CALM;
            if (m_consecutiveDeficitMonths >= 2) {
                intensity = MusicIntensity::CRISIS;
            } else if (m_totalPopulation > m_prevPopulation &&
                       m_consecutiveDeficitMonths == 0) {
                // GROWTH tier: net-positive population change this tick, no deficit streak.
                // m_prevPopulation is the snapshot from the end of the PREVIOUS tick's
                // doPopulationTick(); m_totalPopulation is the snapshot from this tick.
                intensity = MusicIntensity::GROWTH;
            }
            if (intensity != m_lastSentMusicIntensity) {
                m_audio->setMusicIntensity(intensity);
                m_lastSentMusicIntensity = intensity;
            }
        }

        // Phase 10: update m_prevPopulation AFTER setMusicIntensity() so that
        // the GROWTH comparison above uses the previous tick's value correctly.
        m_prevPopulation = m_totalPopulation;
    }

    // Phase 10: traffic signal tick — runs every frame (real-time), NOT per budget tick.
    // Advances signal phase timers and fires sfx_intersection_tick when a signal changes
    // phase, pre-culled at > 80 m from the listener (getListenerPosition()).
    // Called outside the budget-tick while-loop so signals fire at real-time frequency
    // regardless of simulation speed.
    doTrafficSignalTick(realDeltaSeconds);
}

// ---------------------------------------------------------------------------
// Budget tick orchestration
// ---------------------------------------------------------------------------

void CitySimulation::doBudgetTick() {
    // Pre-compute m_budgetSurplusPct BEFORE doServiceDegradationTick() reads it.
    // doEconomyTick() (later in this same tick) will re-compute and also modify
    // the treasury; this block is read-only — it mirrors the same calculation.
    {
        bool inGracePeriod = ((m_clock->nowSeconds() - m_constructionTimeSeconds) <
                              SimulationConstants::grace_period_real_seconds);
        int64_t taxRevC = computeTaxRevenue(ZoneType::Commercial);
        int64_t taxRevI = computeTaxRevenue(ZoneType::Industrial);
        int64_t totalCIRevenue = taxRevC + taxRevI;
        int64_t wages      = computeWagesCost(totalCIRevenue);
        int64_t svcUpkeep  = inGracePeriod ? 0LL : computeServiceUpkeepCost();
        int64_t roadMaint  = inGracePeriod ? 0LL : computeRoadMaintenanceCost();
        int64_t utilFees   = computeUtilityFeeRevenue();
        int64_t totalRevenue  = computeTaxRevenue(ZoneType::Residential) + taxRevC + taxRevI + utilFees;
        int64_t totalExpenses = wages + svcUpkeep + roadMaint;
        m_budgetSurplusPct = computeBudgetSurplusPct(totalRevenue, totalExpenses);
    }

    // Sub-methods called in EXACTLY this order (per spec):
    computeTrafficDemand();
    computeEffectiveDemand();
    doServiceDegradationTick();
    doDesirabilityTick();
    doPopulationTick();
    doDensityUnlockTick();
    doEconomyTick();
    doGameOverTick();
    checkCityRatingTransition();

    // Undo expiry check
    if (m_undoExpiryTickTarget >= 0 && m_totalTicks >= m_undoExpiryTickTarget) {
        m_pendingUndo.reset();
        m_undoExpiryTickTarget = -1;
    }

    // Loan cooldown countdown
    if (m_loanCooldownTicks > 0) {
        m_loanCooldownTicks--;
    }
}

// ---------------------------------------------------------------------------
// Traffic demand computation
// ---------------------------------------------------------------------------

void CitySimulation::computeTrafficDemand() {
    // Count road-adjacent tiles for each zone type
    int rAdjacentCount = 0;
    int cAdjacentCount = 0;
    int iAdjacentCount = 0;

    // Directions: up, down, left, right
    const int dx[] = {0, 0, -1, 1};
    const int dz[] = {-1, 1, 0, 0};

    for (auto& [key, tile] : m_tiles) {
        if (!tile.isZoned) continue;

        // Decode tile position from key
        int x = static_cast<int>(key >> 32);
        int z = static_cast<int>(static_cast<uint32_t>(key));

        // Check 4-directional adjacency for roads
        bool hasRoadAdjacentTile = false;
        for (int d = 0; d < 4; ++d) {
            int nx = x + dx[d];
            int nz = z + dz[d];
            const TileData* neighbor = findTile(nx, nz);
            if (neighbor && neighbor->isRoad) {
                hasRoadAdjacentTile = true;
                break;
            }
        }

        if (hasRoadAdjacentTile) {
            switch (tile.zone) {
                case ZoneType::Residential:  rAdjacentCount++; break;
                case ZoneType::Commercial:   cAdjacentCount++; break;
                case ZoneType::Industrial:   iAdjacentCount++; break;
            }
        }
    }

    // Count total zoned tiles for zone-count-based traffic load
    int totalZonedTiles = 0;
    for (auto& [zkey, ztile] : m_tiles) {
        if (ztile.isZoned) totalZonedTiles++;
    }

    // Compute traffic load based on zone count vs road capacity
    // (zone count, not population, to give meaningful load signal from placement)
    int totalRoadTiles = m_roadTileCount;
    int roadCapacity = std::max(1, totalRoadTiles) * SimulationConstants::road_segment_capacity_per_tile;
    float trafficLoad = static_cast<float>(totalZonedTiles) / static_cast<float>(roadCapacity);

    // Speed fraction: clamped to [min_speed_fraction, 1.0]
    float speedFraction = std::max(SimulationConstants::min_speed_fraction,
                                   1.0f - trafficLoad);
    speedFraction = std::min(1.0f, speedFraction);
    m_roadSpeedFraction = speedFraction;

    // Compute traffic demand samples for each zone type.
    // When trafficLoad > 1.0 (overcapacity): road-adjacent tiles record timeout (0.0).
    // Otherwise: travel time = tile_size_m / (road_max_speed_mps * speed_fraction).
    float sampleR, sampleC, sampleI;

    if (rAdjacentCount == 0) {
        sampleR = SimulationConstants::null_path_demand_default;
    } else if (trafficLoad > 1.0f) {
        sampleR = 0.0f;  // timeout: overcongested road network
    } else {
        float travelTime = kTileSizeMeters /
                           (SimulationConstants::road_max_speed_mps * speedFraction);
        sampleR = travelTimeDemand(travelTime, 25.0f, 60.0f);
    }

    if (cAdjacentCount == 0) {
        sampleC = SimulationConstants::null_path_demand_default;
    } else if (trafficLoad > 1.0f) {
        sampleC = 0.0f;  // timeout
    } else {
        float travelTime = kTileSizeMeters /
                           (SimulationConstants::road_max_speed_mps * speedFraction);
        sampleC = travelTimeDemand(travelTime, 30.0f, 65.0f);
    }

    if (iAdjacentCount == 0) {
        sampleI = SimulationConstants::null_path_demand_default;
    } else if (trafficLoad > 1.0f) {
        sampleI = 0.0f;  // timeout
    } else {
        float travelTime = kTileSizeMeters /
                           (SimulationConstants::road_max_speed_mps * speedFraction);
        sampleI = travelTimeDemand(travelTime, 40.0f, 80.0f);
    }

    // Update rolling windows for R and C (shared index m_trafficWindowIdxRC)
    m_trafficWindowR[m_trafficWindowIdxRC % SimulationConstants::traffic_rolling_window_r_c] = sampleR;
    m_trafficWindowC[m_trafficWindowIdxRC % SimulationConstants::traffic_rolling_window_r_c] = sampleC;
    m_trafficWindowIdxRC = (m_trafficWindowIdxRC + 1) % SimulationConstants::traffic_rolling_window_r_c;

    // Update rolling window for I
    m_trafficWindowI[m_trafficWindowIdxI % SimulationConstants::traffic_rolling_window_i] = sampleI;
    m_trafficWindowIdxI = (m_trafficWindowIdxI + 1) % SimulationConstants::traffic_rolling_window_i;

    // Compute rolling averages using partial average (divide by samples written,
    // not window size) so early ticks are not diluted by zero-initialised slots.
    // Arrays are zero-initialised; unwritten slots stay 0 and are excluded.
    int samplesRC = std::min(m_totalTicks, SimulationConstants::traffic_rolling_window_r_c);
    int samplesI  = std::min(m_totalTicks, SimulationConstants::traffic_rolling_window_i);
    // Clamp to at least 1 to avoid division by zero on tick 0 (before first write)
    samplesRC = std::max(1, samplesRC);
    samplesI  = std::max(1, samplesI);

    float sumR = 0.0f;
    for (int i = 0; i < SimulationConstants::traffic_rolling_window_r_c; ++i) sumR += m_trafficWindowR[i];
    m_trafficDemandFactorR = sumR / static_cast<float>(samplesRC);

    float sumC = 0.0f;
    for (int i = 0; i < SimulationConstants::traffic_rolling_window_r_c; ++i) sumC += m_trafficWindowC[i];
    m_trafficDemandFactorC = sumC / static_cast<float>(samplesRC);

    float sumI = 0.0f;
    for (int i = 0; i < SimulationConstants::traffic_rolling_window_i; ++i) sumI += m_trafficWindowI[i];
    m_trafficDemandFactorI = sumI / static_cast<float>(samplesI);
}

// ---------------------------------------------------------------------------
// Effective demand computation (post-traffic, bootstrap, capacity-ratio, floor)
// ---------------------------------------------------------------------------

void CitySimulation::computeEffectiveDemand() {
    // Bootstrap decay terms (only active for first demand_bootstrapping_ticks ticks)
    float bootstrapR = 0.0f, bootstrapC = 0.0f, bootstrapI = 0.0f;
    if (m_totalTicks < SimulationConstants::demand_bootstrapping_ticks) {
        float progress = static_cast<float>(m_totalTicks) /
                         static_cast<float>(SimulationConstants::demand_bootstrapping_ticks);
        bootstrapR = 0.50f * (1.0f - progress);
        bootstrapC = 0.25f * (1.0f - progress);
        bootstrapI = 0.15f * (1.0f - progress);
    }

    // Gather capacity and population totals
    float totalRPop = 0.0f;
    float totalCPop = 0.0f;
    float totalIPop = 0.0f;
    float totalCCapacity = 0.0f;
    float totalICapacity = 0.0f;

    for (auto& [key, tile] : m_tiles) {
        if (!tile.isZoned) continue;
        switch (tile.zone) {
            case ZoneType::Residential:
                totalRPop += tile.population;
                break;
            case ZoneType::Commercial:
                totalCPop += tile.population;
                totalCCapacity += static_cast<float>(maxPopulationForTile(tile.zone, tile.density));
                break;
            case ZoneType::Industrial:
                totalIPop += tile.population;
                totalICapacity += static_cast<float>(maxPopulationForTile(tile.zone, tile.density));
                break;
        }
    }

    // Capacity-ratio signals
    // R demand: how many C+I worker slots exist relative to R residents
    float totalCIWorkerCapacity = totalCCapacity + totalICapacity;
    float R_demand = std::min(1.0f, totalCIWorkerCapacity / std::max(1.0f, totalRPop));

    // C demand: how many R residents relative to C capacity
    float C_demand = std::min(1.0f, totalRPop / std::max(1.0f, totalCCapacity));

    // I demand: derived from R raw material need and C goods consumption
    float I_demand;
    if (totalICapacity == 0.0f) {
        I_demand = 1.0f;  // default when no I zones
    } else {
        float rRawDemand = totalRPop * SimulationConstants::R_raw_material_rate;
        float cGoodsDemand = totalCPop * SimulationConstants::C_goods_consumption_rate;
        I_demand = std::min(1.0f, (rRawDemand + cGoodsDemand) / std::max(1.0f, totalICapacity));
    }

    // Effective demand:
    //   R: travel-time signal + bootstrap (CI-capacity gate: zero when no CI zones)
    //   C: capacity-ratio × traffic_factor + bootstrap (unchanged)
    //   I: 1.0 when no I zones (fully unsatisfied); formula otherwise
    float effectiveR = std::min(1.0f, std::max(0.0f,
        m_trafficDemandFactorR + bootstrapR));
    float effectiveC = std::min(1.0f, std::max(0.0f,
        m_trafficDemandFactorC * C_demand + bootstrapC));
    float effectiveI;
    if (totalICapacity == 0.0f) {
        effectiveI = 1.0f;  // no I zones → demand fully unsatisfied
    } else {
        effectiveI = std::min(1.0f, std::max(0.0f,
            m_trafficDemandFactorI * I_demand + bootstrapI));
    }

    // Apply demand floors if at least one road tile exists
    if (m_roadTileCount > 0) {
        effectiveR = std::max(SimulationConstants::demand_floor_residential, effectiveR);
        effectiveC = std::max(SimulationConstants::demand_floor_commercial, effectiveC);
        effectiveI = std::max(SimulationConstants::demand_floor_industrial, effectiveI);
    }

    // CI-capacity gate: zero out R demand when no C/I zones exist.
    // Applied AFTER floor so that roads cannot override the gate.
    if (totalCIWorkerCapacity == 0.0f) {
        effectiveR = 0.0f;
    }

    m_demandPressurePct[static_cast<int>(ZoneType::Residential)] = effectiveR;
    m_demandPressurePct[static_cast<int>(ZoneType::Commercial)]  = effectiveC;
    m_demandPressurePct[static_cast<int>(ZoneType::Industrial)]  = effectiveI;
}

// ---------------------------------------------------------------------------
// Service degradation tick
// ---------------------------------------------------------------------------

void CitySimulation::doServiceDegradationTick() {
    if (m_budgetSurplusPct <= SimulationConstants::service_deficit_radius_halving_threshold) {
        // Budget deficit: stochastic degradation in Fire → Police → Water order
        // (Power plant uses a deterministic BFS brownout — no RNG roll, no audio).
        // Sort service buildings by type to guarantee order
        auto typeOrder = [](ServiceType t) -> int {
            switch (t) {
                case ServiceType::FireStation:   return 0;
                case ServiceType::PoliceStation: return 1;
                case ServiceType::WaterTower:    return 2;
                case ServiceType::PowerPlant:    return 3;
            }
            return 4;
        };

        // Create sorted index list
        std::vector<size_t> indices(m_serviceBuildings.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            return typeOrder(m_serviceBuildings[a].type) < typeOrder(m_serviceBuildings[b].type);
        });

        for (size_t idx : indices) {
            ServiceBuilding& sb = m_serviceBuildings[idx];
            if (sb.type == ServiceType::PowerPlant) {
                // Power brownout is deterministic: mark degraded silently.
                // Coverage reduction is handled by computePowerCoverage() via
                // the m_budgetSurplusPct BFS-depth cutoff — no audio needed.
                sb.degraded = true;
                continue;
            }
            if (!sb.degraded) {
                float roll = m_rng->nextFloat();
                if (roll < SimulationConstants::service_degradation_probability_per_tick) {
                    sb.degraded = true;
                    if (m_audio) {
                        m_audio->playSound(SFX_SERVICE_DEGRADE, SoundPriority::NORMAL, 1.0f);
                    }
                    m_notifications.push({NotificationType::ServiceDegraded, 0, 0, 0});
                }
            }
        }
    } else {
        // Budget OK: recover all degraded buildings
        for (ServiceBuilding& sb : m_serviceBuildings) {
            sb.degraded = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Desirability tick
// ---------------------------------------------------------------------------

void CitySimulation::doDesirabilityTick() {
    for (auto& [key, tile] : m_tiles) {
        if (!tile.isZoned) continue;

        int x = static_cast<int>(key >> 32);
        int z = static_cast<int>(static_cast<uint32_t>(key));

        float desirability = tile.desirability;

        if (tile.zone == ZoneType::Residential) {
            // Check 5x5 Chebyshev neighborhood for adjacency effects
            for (int dz = -5; dz <= 5; ++dz) {
                for (int dx = -5; dx <= 5; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    int chebyshevDist = std::max(std::abs(dx), std::abs(dz));
                    if (chebyshevDist > 5) continue;

                    const TileData* neighbor = findTile(x + dx, z + dz);
                    if (!neighbor || !neighbor->isZoned) continue;

                    if (neighbor->zone == ZoneType::Industrial) {
                        // Industrial penalty (falloff: full at d=1, zero at d=5)
                        float falloff = 1.0f - static_cast<float>(chebyshevDist - 1) / 4.0f;
                        desirability -= SimulationConstants::adjacency_industrial_residential_base_penalty
                                        * falloff;
                    } else if (neighbor->zone == ZoneType::Commercial && chebyshevDist == 1) {
                        // Commercial bonus at distance 1 only
                        desirability += static_cast<float>(SimulationConstants::adjacency_commercial_residential_bonus);
                    }
                }
            }

            // Service coverage desirability effect
            bool anyUncovered = false;
            // Check if service buildings of each type exist and if this tile is covered
            bool hasFireStation = false, hasPolice = false, hasWater = false, hasPower = false;
            for (const ServiceBuilding& sb : m_serviceBuildings) {
                switch (sb.type) {
                    case ServiceType::FireStation:   hasFireStation = true; break;
                    case ServiceType::PoliceStation: hasPolice      = true; break;
                    case ServiceType::WaterTower:    hasWater       = true; break;
                    case ServiceType::PowerPlant:    hasPower       = true; break;
                }
            }

            // No service buildings at all → every residential tile is uncovered
            if (!hasFireStation && !hasPolice && !hasWater && !hasPower) {
                anyUncovered = true;
            }

            if (hasFireStation) {
                float cov = computeRadialCoverage(x, z, ServiceType::FireStation);
                if (cov == 0.0f) anyUncovered = true;
            }
            if (hasPolice) {
                float cov = computeRadialCoverage(x, z, ServiceType::PoliceStation);
                if (cov == 0.0f) anyUncovered = true;
            }

            // Phase 10: water coverage SFX — fire SFX_WATER_OUT exactly once per
            // coverage-loss event (wasPowered / wasWaterCovered flag gates re-fire).
            // Only fires for residential tiles (water billing affects residential coverage).
            bool currentlyWaterCovered = false;
            if (hasWater) {
                float cov = computeRadialCoverage(x, z, ServiceType::WaterTower);
                if (cov == 0.0f) {
                    anyUncovered = true;
                } else {
                    currentlyWaterCovered = true;
                }
            }
            if (tile.wasWaterCovered && !currentlyWaterCovered && hasWater) {
                // Water just lost — fire SFX once (NORMAL priority per V1 manifest).
                if (m_audio) {
                    m_audio->playSound(SFX_WATER_OUT, SoundPriority::NORMAL, 1.0f);
                }
                tile.wasWaterCovered = false;
            } else if (currentlyWaterCovered) {
                tile.wasWaterCovered = true;
            }

            // Phase 10: power coverage SFX — fire SFX_POWER_OUT exactly once per
            // coverage-loss event.
            bool currentlyPowered = false;
            if (hasPower) {
                float cov = computePowerCoverage(x, z);
                if (cov == 0.0f) {
                    anyUncovered = true;
                } else {
                    currentlyPowered = true;
                }
            }
            if (tile.wasPowered && !currentlyPowered && hasPower) {
                // Power just lost — fire SFX once (NORMAL priority per V1 manifest).
                if (m_audio) {
                    m_audio->playSound(SFX_POWER_OUT, SoundPriority::NORMAL, 1.0f);
                }
                tile.wasPowered = false;
            } else if (currentlyPowered) {
                tile.wasPowered = true;
            }

            if (anyUncovered) {
                // Grace: skip service penalty on the very first desirability tick
                // (newly placed tiles should not immediately lose desirability).
                if (!tile.firstDesirabilityTick) {
                    desirability -= static_cast<float>(SimulationConstants::service_uncovered_desirability_penalty_per_tick);
                }
            } else if (hasFireStation || hasPolice || hasWater || hasPower) {
                // At least one service type exists and all covered → recovery
                desirability += static_cast<float>(SimulationConstants::service_recovery_desirability_per_tick);
            }
        }

        // Clear first-tick grace flag after processing (applies to all zone types)
        tile.firstDesirabilityTick = false;

        // Clamp desirability to [0, 100]
        tile.desirability = std::min(100.0f, std::max(0.0f, desirability));

        // Phase 10: service-alert SFX — fire SFX_FIRE_ALERT or SFX_POLICE_ALERT
        // exactly once per crisis episode. Fire takes priority over Police when
        // both stations cover this tile. Only residential tiles are tested.
        // alertFired prevents re-fire while desirability stays at or below threshold.
        // Resets when desirability recovers above service_alert_desirability_threshold.
        if (tile.isZoned && tile.zone == ZoneType::Residential && m_audio) {
            if (tile.desirability <= static_cast<float>(SimulationConstants::service_alert_desirability_threshold)) {
                if (!tile.alertFired) {
                    // Determine which alert to fire: Fire takes priority over Police.
                    bool hasFire  = false;
                    bool hasPoliceForAlert = false;
                    for (const ServiceBuilding& sb : m_serviceBuildings) {
                        if (sb.type == ServiceType::FireStation)   hasFire = true;
                        if (sb.type == ServiceType::PoliceStation) hasPoliceForAlert = true;
                    }
                    if (hasFire) {
                        m_audio->playPositionalSound(SFX_FIRE_ALERT,
                            vec3{static_cast<float>(x), 0.0f, static_cast<float>(z)},
                            SoundPriority::CRITICAL, 1.0f);
                    } else if (hasPoliceForAlert) {
                        m_audio->playPositionalSound(SFX_POLICE_ALERT,
                            vec3{static_cast<float>(x), 0.0f, static_cast<float>(z)},
                            SoundPriority::CRITICAL, 1.0f);
                    }
                    tile.alertFired = true;
                }
            } else {
                // Desirability recovered — allow alert to re-fire in a future episode.
                tile.alertFired = false;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Population tick
// ---------------------------------------------------------------------------

void CitySimulation::doPopulationTick() {
    for (auto& [key, tile] : m_tiles) {
        if (!tile.isZoned) continue;

        int maxPop = maxPopulationForTile(tile.zone, tile.density);
        float demand = effectiveDemandForTile(tile);
        float targetPop = static_cast<float>(maxPop) * demand;

        float delta = targetPop - tile.population;

        // Cap delta
        float maxGrowth = SimulationConstants::population_growth_cap_fraction * static_cast<float>(maxPop);
        float maxDecay  = SimulationConstants::population_decay_cap_fraction  * static_cast<float>(maxPop);
        delta = std::max(-maxDecay, std::min(maxGrowth, delta));

        tile.population = std::max(0.0f, std::min(static_cast<float>(maxPop),
                                                    tile.population + delta));
    }

    // Recompute total population from residential tiles only
    m_totalPopulation = 0;
    for (auto& [key, tile] : m_tiles) {
        if (tile.isZoned && tile.zone == ZoneType::Residential) {
            m_totalPopulation += static_cast<int>(tile.population);
        }
    }

    // Check population milestones
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
            m_notifications.push({NotificationType::PopulationMilestone, 0, 0, milestoneThresholds[i]});
        }
    }
}

// ---------------------------------------------------------------------------
// Density unlock tick
// ---------------------------------------------------------------------------

void CitySimulation::doDensityUnlockTick() {
    // Returns the treasury threshold for a density tier index (0-5)
    auto getDensityUnlockThreshold = [](int tierIndex) -> float {
        switch (tierIndex) {
            case 0: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_1); // Med-R
            case 1: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_1); // Med-C
            case 2: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_2); // Med-I
            case 3: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_3); // High-R
            case 4: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_4); // High-C
            case 5: return static_cast<float>(SimulationConstants::density_unlock_base_threshold_5); // High-I
            default: return std::numeric_limits<float>::max();
        }
    };

    // Phase 10: per-wave-tick audio cap — tracks total sfx_zone_upgrade calls this tick
    // across ALL tier loops combined. Tiles beyond the cap are upgraded silently.
    // (SimulationConstants::sfx_zone_upgrade_per_tick_cap = 3)
    int sfxCallsThisTick = 0;

    float scale = getDensityUnlockScale();

    // Snapshot which tiers were already unlocked before this tick's unlock check.
    // The upgrade wave must not fire on the same tick a tier first becomes unlocked.
    bool wasAlreadyUnlocked[6];
    for (int i = 0; i < 6; ++i) {
        wasAlreadyUnlocked[i] = m_densityUnlockState.unlock_flags[i];
    }

    for (int i = 0; i < 6; ++i) {
        if (m_densityUnlockState.unlock_flags[i]) continue;

        // Special prerequisite: High-R (tier 3) requires Med-I (tier 2) unlocked
        if (i == 3 && !m_densityUnlockState.unlock_flags[2]) continue;

        float threshold = getDensityUnlockThreshold(i) * scale;

        if (static_cast<float>(m_treasury) >= threshold) {
            m_densityUnlockState.consecutive_months_above_threshold[i]++;
            if (m_densityUnlockState.consecutive_months_above_threshold[i] >= 3) {
                m_densityUnlockState.unlock_flags[i] = true;
                m_densityUnlockState.consecutive_months_above_threshold[i] = 0;
            }
        } else {
            m_densityUnlockState.consecutive_months_above_threshold[i] = 0;
        }
    }

    // Density upgrade wave: upgrade eligible tiles that are unlocked
    // Map tier index to zone type and density
    // tier 0 = Med-R, tier 1 = Med-C, tier 2 = Med-I,
    // tier 3 = High-R, tier 4 = High-C, tier 5 = High-I

    // For each zone type, count how many tiles are eligible and cap upgrades
    for (int tierIdx = 0; tierIdx < 6; ++tierIdx) {
        if (!m_densityUnlockState.unlock_flags[tierIdx]) continue;
        // Skip upgrade wave on the tick a tier first becomes unlocked
        if (!wasAlreadyUnlocked[tierIdx]) continue;

        ZoneType   targetZone;
        DensityTier targetDensity;  // The NEW density that becomes available
        DensityTier currentRequired; // The current density a tile must have to be upgraded

        switch (tierIdx) {
            case 0: targetZone = ZoneType::Residential; targetDensity = DensityTier::Medium; currentRequired = DensityTier::Low; break;
            case 1: targetZone = ZoneType::Commercial;  targetDensity = DensityTier::Medium; currentRequired = DensityTier::Low; break;
            case 2: targetZone = ZoneType::Industrial;  targetDensity = DensityTier::Medium; currentRequired = DensityTier::Low; break;
            case 3: targetZone = ZoneType::Residential; targetDensity = DensityTier::High;   currentRequired = DensityTier::Medium; break;
            case 4: targetZone = ZoneType::Commercial;  targetDensity = DensityTier::High;   currentRequired = DensityTier::Medium; break;
            case 5: targetZone = ZoneType::Industrial;  targetDensity = DensityTier::High;   currentRequired = DensityTier::Medium; break;
            default: continue;
        }

        // Count total tiles of this zone type for the cap
        int totalZoneTiles = 0;
        for (auto& [key, tile] : m_tiles) {
            if (tile.isZoned && tile.zone == targetZone) totalZoneTiles++;
        }

        int maxUpgrades = static_cast<int>(
            SimulationConstants::density_max_upgrade_rate_per_tick * static_cast<float>(totalZoneTiles));
        maxUpgrades = std::max(1, maxUpgrades); // at least 1 if any tiles exist

        int upgradeCount = 0;

        float demandForZone = m_demandPressurePct[static_cast<int>(targetZone)];
        if (demandForZone < SimulationConstants::density_upgrade_wave_demand_threshold) continue;

        for (auto& [key, tile] : m_tiles) {
            if (upgradeCount >= maxUpgrades) break;
            if (!tile.isZoned) continue;
            if (tile.zone != targetZone) continue;
            if (tile.density != currentRequired) continue;

            // Upgrade this tile
            tile.density = targetDensity;
            tile.population = 0.0f;  // population grows fresh

            // Phase 10: swap the building mesh to match the new density tier.
            // Decode tile coordinates from the map key (key = (x << 32) | (uint32_t)z).
            // Remove the old mesh first, then place the new one so the renderer
            // never has two overlapping meshes at the same tile.
            if (m_renderer) {
                int tx = static_cast<int>(key >> 32);
                int tz = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFFLL));
                m_renderer->removeBuildingMesh(tx, tz);
                m_renderer->placeBuildingMesh(tx, tz, zoneAssetBaseName(targetZone, targetDensity));
            }

            // Phase 10: sfx_zone_upgrade is non-positional (AL_SOURCE_RELATIVE = AL_TRUE).
            // Cap at sfx_zone_upgrade_per_tick_cap calls total across all tiers this tick.
            if (m_audio && sfxCallsThisTick < SimulationConstants::sfx_zone_upgrade_per_tick_cap) {
                m_audio->playSound(SFX_ZONE_UPGRADE, SoundPriority::NORMAL, 1.0f);
                ++sfxCallsThisTick;
            }
            upgradeCount++;
        }
    }
}

// ---------------------------------------------------------------------------
// Economy tick
// ---------------------------------------------------------------------------

void CitySimulation::doEconomyTick() {
    bool inGracePeriod = ((m_clock->nowSeconds() - m_constructionTimeSeconds) <
                          SimulationConstants::grace_period_real_seconds);

    // Compute revenue components
    int64_t taxRevR = computeTaxRevenue(ZoneType::Residential);
    int64_t taxRevC = computeTaxRevenue(ZoneType::Commercial);
    int64_t taxRevI = computeTaxRevenue(ZoneType::Industrial);

    int64_t totalCIRevenue = taxRevC + taxRevI;
    int64_t wages = computeWagesCost(totalCIRevenue);

    int64_t svcUpkeep  = inGracePeriod ? 0LL : computeServiceUpkeepCost();
    int64_t roadMaint  = inGracePeriod ? 0LL : computeRoadMaintenanceCost();
    int64_t utilFees   = computeUtilityFeeRevenue();

    int64_t totalRevenue = taxRevR + taxRevC + taxRevI + utilFees;
    int64_t totalExpenses = wages + svcUpkeep + roadMaint;
    int64_t net = totalRevenue - totalExpenses;

    // Update treasury
    m_treasury += net;

    // Process loan repayments
    processLoanRepayments(m_treasury);

    // Compute budget surplus percentage
    m_budgetSurplusPct = computeBudgetSurplusPct(totalRevenue, totalExpenses);

    // Cache line items
    m_lastMonthTaxRevenue[0] = static_cast<float>(taxRevR);
    m_lastMonthTaxRevenue[1] = static_cast<float>(taxRevC);
    m_lastMonthTaxRevenue[2] = static_cast<float>(taxRevI);
    m_lastMonthWagesCost          = static_cast<float>(wages);
    m_lastMonthRoadMaintenanceCost = static_cast<float>(roadMaint);
    m_lastMonthServiceUpkeepCost  = static_cast<float>(svcUpkeep);
    m_lastMonthUtilityFeeRevenue  = static_cast<float>(utilFees);
    m_currentMonthlyRevenue       = static_cast<float>(totalRevenue);

    // First revenue check
    if (totalRevenue > 0) {
        m_firstRevenueTicked = true;
    }

    // Budget deficit warning at -25%
    // Gate on m_firstRevenueTicked: only warn after the city has generated some
    // revenue (matching the forced-loan gate). Prevents spurious warnings when
    // service buildings exist but no zones have been placed yet.
    if (m_firstRevenueTicked && m_budgetSurplusPct <= -0.25f) {
        if (!m_budgetWarnFired) {
            m_budgetWarnFired = true;
            m_notifications.push({NotificationType::BudgetDeficitWarn, 0, 0, 0});
            if (m_audio) {
                m_audio->playSound(SFX_BUDGET_WARN, SoundPriority::NORMAL, 1.0f);
            }
        }
    } else {
        m_budgetWarnFired = false;
    }

    // Check forced loan
    checkAndIssueForcedLoan();
}

// ---------------------------------------------------------------------------
// Budget surplus percentage
// ---------------------------------------------------------------------------

float CitySimulation::computeBudgetSurplusPct(int64_t revenue, int64_t expenses) const {
    if (revenue == 0) {
        // No revenue: if there are expenses, that's maximum deficit; else neutral.
        return (expenses > 0) ? -1.0f : 0.0f;
    }
    return static_cast<float>(revenue - expenses) / static_cast<float>(revenue);
}

// ---------------------------------------------------------------------------
// Tax revenue computation
// ---------------------------------------------------------------------------

int64_t CitySimulation::computeTaxRevenue(ZoneType zone) const {
    int zoneIdx = static_cast<int>(zone);
    float taxRate = m_taxRates[zoneIdx];

    // Determine base income per resident based on density
    auto incomeForDensity = [](DensityTier d) -> int {
        switch (d) {
            case DensityTier::Low:    return SimulationConstants::base_income_per_resident_low;
            case DensityTier::Medium: return SimulationConstants::base_income_per_resident_medium;
            case DensityTier::High:   return SimulationConstants::base_income_per_resident_high;
        }
        return SimulationConstants::base_income_per_resident_low;
    };

    int64_t total = 0;
    for (auto& [key, tile] : m_tiles) {
        if (!tile.isZoned || tile.zone != zone) continue;
        int pop = static_cast<int>(tile.population);
        int income = incomeForDensity(tile.density);
        total += static_cast<int64_t>(static_cast<float>(income * pop) * taxRate);
    }

    // Apply congestion penalty
    float penalty = 0.0f;
    if (m_roadSpeedFraction <= SimulationConstants::congestion_high_threshold) {
        penalty = SimulationConstants::congestion_penalty_high;
    } else if (m_roadSpeedFraction <= SimulationConstants::congestion_low_threshold) {
        penalty = SimulationConstants::congestion_penalty_medium;
    } else if (m_roadSpeedFraction <= SimulationConstants::congestion_none_threshold) {
        penalty = SimulationConstants::congestion_penalty_low;
    }

    total = static_cast<int64_t>(static_cast<float>(total) * (1.0f - penalty));
    return total;
}

// ---------------------------------------------------------------------------
// Wages cost
// ---------------------------------------------------------------------------

int64_t CitySimulation::computeWagesCost(int64_t totalCIRevenue) const {
    return static_cast<int64_t>(static_cast<float>(totalCIRevenue) *
                                 SimulationConstants::wage_fraction_of_revenue);
}

// ---------------------------------------------------------------------------
// Service upkeep cost
// ---------------------------------------------------------------------------

int64_t CitySimulation::computeServiceUpkeepCost() const {
    int64_t total = 0;
    for (const ServiceBuilding& sb : m_serviceBuildings) {
        switch (sb.type) {
            case ServiceType::FireStation:
                total += SimulationConstants::service_upkeep_fire_station_per_tick; break;
            case ServiceType::PoliceStation:
                total += SimulationConstants::service_upkeep_police_station_per_tick; break;
            case ServiceType::WaterTower:
                total += SimulationConstants::service_upkeep_water_tower_per_tick; break;
            case ServiceType::PowerPlant:
                total += SimulationConstants::service_upkeep_power_plant_per_tick; break;
        }
    }
    return total;
}

// ---------------------------------------------------------------------------
// Road maintenance cost
// ---------------------------------------------------------------------------

int64_t CitySimulation::computeRoadMaintenanceCost() const {
    return static_cast<int64_t>(m_roadTileCount) *
           static_cast<int64_t>(SimulationConstants::road_maintenance_cost_per_tile);
}

// ---------------------------------------------------------------------------
// Utility fee revenue
// ---------------------------------------------------------------------------

int64_t CitySimulation::computeUtilityFeeRevenue() const {
    // Count residential tiles with power and water coverage
    // For Phase 6 simplicity: charge all residential tiles that are covered
    // by at least one water tower and power plant respectively.
    // If no service buildings of that type exist: no fee for that service.

    bool hasPower = false, hasWater = false;
    for (const ServiceBuilding& sb : m_serviceBuildings) {
        if (sb.type == ServiceType::PowerPlant) hasPower = true;
        if (sb.type == ServiceType::WaterTower) hasWater = true;
    }

    int64_t total = 0;
    for (auto& [key, tile] : m_tiles) {
        if (!tile.isZoned || tile.zone != ZoneType::Residential) continue;

        int x = static_cast<int>(key >> 32);
        int z = static_cast<int>(static_cast<uint32_t>(key));

        if (hasPower) {
            float powerCov = computePowerCoverage(x, z);
            if (powerCov > 0.0f) {
                total += SimulationConstants::utility_fee_power_per_tile;
            }
        }
        if (hasWater) {
            float waterCov = computeRadialCoverage(x, z, ServiceType::WaterTower);
            if (waterCov > 0.0f) {
                total += SimulationConstants::utility_fee_water_per_tile;
            }
        }
    }
    return total;
}

// ---------------------------------------------------------------------------
// Loan repayments
// ---------------------------------------------------------------------------

void CitySimulation::processLoanRepayments(int64_t& treasury) {
    std::vector<size_t> toRemove;

    for (size_t i = 0; i < m_loans.size(); ++i) {
        LoanEntry& loan = m_loans[i];
        if (loan.ticksRemaining > 1) {
            // Regular repayment: principal portion
            int64_t repayment = loan.remainingPrincipal / static_cast<int64_t>(loan.ticksRemaining);
            treasury -= repayment;
            loan.remainingPrincipal -= repayment;

            // Monthly interest: outstanding * (0.05 / ticks_per_year)
            float interest = static_cast<float>(loan.remainingPrincipal) *
                             (0.05f / static_cast<float>(SimulationConstants::ticks_per_year));
            treasury -= static_cast<int64_t>(interest);

            loan.ticksRemaining--;
        } else {
            // Final payment: absorb remaining principal
            treasury -= loan.remainingPrincipal;
            loan.remainingPrincipal = 0;
            toRemove.push_back(i);
        }
    }

    // Remove paid-off loans (in reverse order to preserve indices)
    for (int i = static_cast<int>(toRemove.size()) - 1; i >= 0; --i) {
        m_loans.erase(m_loans.begin() + static_cast<ptrdiff_t>(toRemove[static_cast<size_t>(i)]));
    }
}

// ---------------------------------------------------------------------------
// Forced loan / emergency bond
// ---------------------------------------------------------------------------

void CitySimulation::checkAndIssueForcedLoan() {
    // Pre-conditions (ALL must be true)
    if (m_budgetSurplusPct > -0.25f) return;
    if ((m_clock->nowSeconds() - m_constructionTimeSeconds) <
        SimulationConstants::grace_period_real_seconds) return;
    if (!m_firstRevenueTicked) return;
    if (m_loanCooldownTicks > 0) return;

    // Compute outstanding debt
    int64_t outstandingDebt = 0;
    for (const LoanEntry& loan : m_loans) {
        outstandingDebt += loan.remainingPrincipal;
    }

    // Debt cap = 3 × max(currentMonthlyRevenue, 1000)
    float revenueCap = std::max(m_currentMonthlyRevenue, 1000.0f);
    int64_t debtCap = static_cast<int64_t>(3.0f * revenueCap);

    // Check if debt cap exhausted → try emergency bond
    if (outstandingDebt >= debtCap) {
        if (m_outstandingBondUses > 0) {
            // Issue emergency municipal bond
            int64_t bondPrincipal = 2LL * outstandingDebt;
            // Cap to a reasonable amount (2x outstanding is already the spec)
            m_treasury += bondPrincipal;
            m_outstandingBondUses--;
            m_loanCooldownTicks = SimulationConstants::loan_cooldown_ticks;

            LoanEntry bondLoan;
            bondLoan.principal         = bondPrincipal;
            bondLoan.remainingPrincipal = bondPrincipal;
            bondLoan.ticksRemaining    = SimulationConstants::bond_repayment_ticks;
            bondLoan.isBond            = true;
            m_loans.push_back(bondLoan);

            m_notifications.push({
                NotificationType::BondIssued,
                static_cast<int>(bondPrincipal),
                SimulationConstants::bond_repayment_ticks,
                0
            });
            if (m_audio) {
                m_audio->playSound(SFX_LOAN_ISSUED, SoundPriority::NORMAL, 1.0f);
            }
        }
        return;
    }

    // Issue forced loan
    float totalExpenses = m_lastMonthWagesCost + m_lastMonthRoadMaintenanceCost +
                          m_lastMonthServiceUpkeepCost;
    float totalRevenue  = m_currentMonthlyRevenue;
    float monthlyShortfall = std::max(0.0f, totalExpenses - totalRevenue);

    float principalF = std::max({monthlyShortfall * 3.0f,
                                  m_currentMonthlyRevenue * 0.5f,
                                  10000.0f});

    // Cap to remaining debt capacity
    int64_t remainingDebtRoom = debtCap - outstandingDebt;
    if (remainingDebtRoom <= 0) return;  // at cap, skip

    int64_t principal = std::min(static_cast<int64_t>(principalF), remainingDebtRoom);
    if (principal <= 0) return;

    m_treasury += principal;
    m_loanCooldownTicks = SimulationConstants::loan_cooldown_ticks;

    LoanEntry newLoan;
    newLoan.principal          = principal;
    newLoan.remainingPrincipal = principal;
    newLoan.ticksRemaining     = SimulationConstants::loan_repayment_ticks;
    newLoan.isBond             = false;
    m_loans.push_back(newLoan);

    m_notifications.push({
        NotificationType::ForcedLoanIssued,
        static_cast<int>(principal),
        SimulationConstants::loan_repayment_ticks,
        0
    });
    if (m_audio) {
        m_audio->playSound(SFX_LOAN_ISSUED, SoundPriority::NORMAL, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Game over tick
// ---------------------------------------------------------------------------

void CitySimulation::doGameOverTick() {
    // Grace period: no game-over counter increment
    if ((m_clock->nowSeconds() - m_constructionTimeSeconds) <
        SimulationConstants::grace_period_real_seconds) {
        return;
    }

    if (m_budgetSurplusPct <= -0.50f) {
        m_consecutiveDeficitMonths++;
        // Month 1 auto-slow
        if (m_consecutiveDeficitMonths == 1 && m_speed != SpeedMultiplier::Paused) {
            setSpeed(SpeedMultiplier::x1);
            m_month1AutoSlowed = true;
        }
    } else {
        m_consecutiveDeficitMonths = 0;
        m_month1AutoSlowed = false;
    }
}

// ---------------------------------------------------------------------------
// City rating transition
// ---------------------------------------------------------------------------

void CitySimulation::checkCityRatingTransition() {
    CityRatingTier newTier;
    if (m_totalPopulation >= 500000) {
        newTier = CityRatingTier::Megalopolis;
    } else if (m_totalPopulation >= 50000) {
        newTier = CityRatingTier::Metropolis;
    } else if (m_totalPopulation >= 10000) {
        newTier = CityRatingTier::City;
    } else if (m_totalPopulation >= 1000) {
        newTier = CityRatingTier::Town;
    } else {
        newTier = CityRatingTier::Village;
    }

    if (newTier != m_cityRating) {
        m_notifications.push({
            NotificationType::CityRatingTransition,
            0,
            0,
            static_cast<int>(newTier)
        });
        m_cityRating = newTier;
        // NOTE: triggerStinger(StingerType::MILESTONE) is NOT called here.
        // Per Phase 10 spec, the stinger is UIManager's responsibility:
        // UIManager polls CityRatingTransition notifications and calls
        // m_audio->triggerStinger(MILESTONE) in its onCityRatingTransition() handler.
        // See implementation/phase-10.md §UIManager City Rating milestone callback.
    }
}

// ---------------------------------------------------------------------------
// Phase 10: Traffic signal tick — advance timers and fire sfx_intersection_tick.
// ---------------------------------------------------------------------------
//
// Called from tick() with real delta seconds (never sim-speed-scaled).
// Each TrafficSignal fires sfx_intersection_tick every phaseSeconds real seconds.
// Pre-acquisition distance cull: skips the call if the listener is beyond 80 m.
// The listener position is obtained once per tick() call via m_renderer->getListenerPosition().
//
// Intersection detection: a road tile is treated as an intersection when it is adjacent
// to 2 or more other road tiles. Signals are maintained incrementally by placeRoad()
// and demolishTile(). This ensures O(1) per-tile maintenance rather than a full
// map scan each tick.

void CitySimulation::doTrafficSignalTick(float realDeltaSeconds) {
    if (!m_audio || m_trafficSignals.empty()) return;

    // Obtain listener position once — avoids repeated virtual dispatch per signal.
    // m_renderer may be null in tests that do not provide a renderer; guard with null check.
    vec3 listenerPos{0.0f, 0.0f, 0.0f};
    if (m_renderer) {
        listenerPos = m_renderer->getListenerPosition();
    }

    const float cullDistSq =
        SimulationConstants::traffic_signal_cull_distance_meters *
        SimulationConstants::traffic_signal_cull_distance_meters;

    for (TrafficSignal& sig : m_trafficSignals) {
        sig.phaseTimer += realDeltaSeconds;
        if (sig.phaseTimer < sig.phaseSeconds) continue;

        // Phase changed (green→red or red→green).
        sig.phaseTimer -= sig.phaseSeconds;

        // Pre-acquisition distance cull (phase-10.md: skip if distance > 80 m).
        vec3 signalPos{static_cast<float>(sig.tileX), 0.0f, static_cast<float>(sig.tileZ)};
        float dx = signalPos.x - listenerPos.x;
        float dz = signalPos.z - listenerPos.z;
        float distSq = dx * dx + dz * dz;
        if (distSq > cullDistSq) continue;

        m_audio->playPositionalSound(SFX_INTERSECTION_TICK, signalPos,
                                     SoundPriority::LOW, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Density unlock scale
// ---------------------------------------------------------------------------

float CitySimulation::getDensityUnlockScale() const {
    switch (m_difficulty) {
        case Difficulty::Easy:   return SimulationConstants::density_unlock_scale_easy;
        case Difficulty::Normal: return SimulationConstants::density_unlock_scale_normal;
        case Difficulty::Hard:   return SimulationConstants::density_unlock_scale_hard;
    }
    return SimulationConstants::density_unlock_scale_normal;
}

// ---------------------------------------------------------------------------
// Effective demand for a single tile
// ---------------------------------------------------------------------------

float CitySimulation::effectiveDemandForTile(const TileData& tile) const {
    return m_demandPressurePct[static_cast<int>(tile.zone)];
}

// ---------------------------------------------------------------------------
// Service coverage helpers
// ---------------------------------------------------------------------------

float CitySimulation::computeServiceCoverageRadius(ServiceType type, bool degraded) const {
    float radius;
    switch (type) {
        case ServiceType::FireStation:
            radius = static_cast<float>(SimulationConstants::fire_station_coverage_radius_m);
            break;
        case ServiceType::PoliceStation:
            radius = static_cast<float>(SimulationConstants::police_station_coverage_radius_m);
            break;
        case ServiceType::WaterTower:
            radius = static_cast<float>(SimulationConstants::water_tower_coverage_radius_m);
            break;
        case ServiceType::PowerPlant:
            // Power plant uses a large default radius (same as fire station for simplicity)
            radius = static_cast<float>(SimulationConstants::fire_station_coverage_radius_m);
            break;
    }
    if (degraded) radius *= 0.5f;
    return radius;
}

float CitySimulation::computeRadialCoverage(int tileX, int tileZ, ServiceType type) const {
    for (const ServiceBuilding& sb : m_serviceBuildings) {
        if (sb.type != type) continue;
        float radius = computeServiceCoverageRadius(type, sb.degraded);
        float radiusTiles = radius / kTileSizeMeters;
        float dx = static_cast<float>(sb.x - tileX);
        float dz = static_cast<float>(sb.z - tileZ);
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist <= radiusTiles) {
            return 1.0f;
        }
    }
    return 0.0f;
}

float CitySimulation::computePowerCoverage(int tileX, int tileZ) const {
    // Power coverage uses BFS from the plant through placed tiles.
    // During budget deficit (m_budgetSurplusPct <= threshold), the power grid
    // "brownouts": coverage is limited to BFS depth <= floor(maxDepth * 0.70).
    // maxDepth = actual furthest BFS depth reached across all placed tiles.
    // A tile is "not covered" if its BFS depth exceeds the brownout cutoff.
    const int dx4[] = {0, 0, -1, 1};
    const int dz4[] = {-1, 1, 0, 0};

    for (const ServiceBuilding& sb : m_serviceBuildings) {
        if (sb.type != ServiceType::PowerPlant) continue;

        // BFS from this plant's location through all adjacent placed tiles
        std::unordered_map<int64_t, int> bfsDepth;
        std::queue<std::pair<int,int>> bfsQueue;

        int64_t plantKey = tileKey(sb.x, sb.z);
        bfsDepth[plantKey] = 0;
        bfsQueue.push({sb.x, sb.z});

        int maxDepth = 0;

        while (!bfsQueue.empty()) {
            auto [cx, cz] = bfsQueue.front();
            bfsQueue.pop();
            int depth = bfsDepth[tileKey(cx, cz)];

            for (int d = 0; d < 4; ++d) {
                int nx = cx + dx4[d];
                int nz = cz + dz4[d];
                int64_t nkey = tileKey(nx, nz);
                if (bfsDepth.count(nkey)) continue;
                if (!isBuildableTile(nx, nz)) continue;

                int newDepth = depth + 1;
                bfsDepth[nkey] = newDepth;
                if (newDepth > maxDepth) maxDepth = newDepth;
                bfsQueue.push({nx, nz});
            }
        }

        // Check if target tile is reachable at all
        int64_t targetKey = tileKey(tileX, tileZ);
        auto it = bfsDepth.find(targetKey);
        if (it == bfsDepth.end()) {
            // BFS couldn't reach via placed tiles; fall back to radial distance.
            // This handles disconnected grids (e.g. plant at (0,0) and tile at (10,0)
            // with no intermediate tiles).
            float radiusTiles = computeServiceCoverageRadius(ServiceType::PowerPlant, sb.degraded)
                                / kTileSizeMeters;
            float fdx = static_cast<float>(tileX - sb.x);
            float fdz = static_cast<float>(tileZ - sb.z);
            float dist = std::sqrt(fdx * fdx + fdz * fdz);
            if (dist <= radiusTiles) return 1.0f;
            continue;
        }

        int targetDepth = it->second;

        // During deficit: limit coverage to floor(maxDepth * 0.70)
        if (m_budgetSurplusPct <=
            SimulationConstants::service_deficit_radius_halving_threshold) {
            int coverDepth = static_cast<int>(
                std::floor(static_cast<float>(maxDepth) * 0.70f));
            if (targetDepth > coverDepth) continue;
        }

        return 1.0f;
    }
    return 0.0f;
}

bool CitySimulation::isBuildableTile(int x, int z) const {
    const TileData* tile = findTile(x, z);
    return (tile != nullptr && (tile->isZoned || tile->isRoad));
}

// ---------------------------------------------------------------------------
// Undo helpers
// ---------------------------------------------------------------------------

void CitySimulation::recordUndoAction(const UndoAction& action) {
    m_pendingUndo = action;
    m_undoExpiryTickTarget = m_totalTicks + 2;

    // Compute wall-clock expiry time
    float sv = speedValue(m_speed);
    if (sv > 0.0f) {
        double realSecondsRemaining = static_cast<double>(
            (2.0f * SimulationConstants::SECONDS_PER_BUDGET_TICK - m_accumulatedSimSeconds) / sv);
        m_undoExpiryWallSeconds = m_clock->nowSeconds() + realSecondsRemaining;
    } else {
        // Paused: set a far future time (undo doesn't expire while paused)
        m_undoExpiryWallSeconds = m_clock->nowSeconds() +
            static_cast<double>(2.0f * SimulationConstants::SECONDS_PER_BUDGET_TICK);
    }
}

// ---------------------------------------------------------------------------
// placeZone
// ---------------------------------------------------------------------------

void CitySimulation::placeZone(int tileX, int tileZ, ZoneType type, DensityTier tier,
                               int earthworksCostOverride) {
    // Record previous state for undo
    UndoAction undoAction;
    undoAction.actionType = UndoAction::Type::PlaceZone;
    undoAction.tileX = tileX;
    undoAction.tileZ = tileZ;

    int64_t key = tileKey(tileX, tileZ);
    auto it = m_tiles.find(key);
    if (it != m_tiles.end()) {
        undoAction.previousState = it->second;
        // Track road count if we're replacing a road
        if (it->second.isRoad) {
            m_roadTileCount--;
        }
    } else {
        undoAction.previousState = TileData{};
    }
    undoAction.costPaid = static_cast<int64_t>(earthworksCostOverride);

    // Deduct earthworks cost
    m_treasury -= static_cast<int64_t>(earthworksCostOverride);

    // Update tile
    TileData& tile = m_tiles[key];
    tile.isZoned     = true;
    tile.isRoad      = false;
    tile.zone        = type;
    tile.density     = tier;
    tile.population  = 0.0f;
    tile.desirability = static_cast<float>(SimulationConstants::desirability_base_value);

    // Play audio
    if (earthworksCostOverride > 0) {
        if (m_audio) {
            m_audio->playPositionalSound(SFX_EARTHWORKS,
                vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
                SoundPriority::NORMAL, 1.0f);
        }
    }
    if (m_audio) {
        m_audio->playPositionalSound(SFX_BUILD_PLACE,
            vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
            SoundPriority::NORMAL, 1.0f);
    }

    // Phase 10: spawn building mesh for the placed zone tile.
    // Phase 10 variant policy: always _01 suffix (round-robin deferred to Phase 11).
    if (m_renderer) {
        m_renderer->placeBuildingMesh(tileX, tileZ, zoneAssetBaseName(type, tier));
    }

    // Record undo
    recordUndoAction(undoAction);
}

// ---------------------------------------------------------------------------
// placeRoad
// ---------------------------------------------------------------------------

void CitySimulation::placeRoad(int tileX, int tileZ, int earthworksCostOverride) {
    // Record previous state for undo
    UndoAction undoAction;
    undoAction.actionType = UndoAction::Type::PlaceRoad;
    undoAction.tileX = tileX;
    undoAction.tileZ = tileZ;

    int64_t key = tileKey(tileX, tileZ);
    auto it = m_tiles.find(key);
    if (it != m_tiles.end()) {
        undoAction.previousState = it->second;
        if (it->second.isRoad) {
            // Already a road — nothing to change for road count
        } else if (it->second.isZoned) {
            // Was a zone — now becomes road
        }
    } else {
        undoAction.previousState = TileData{};
    }

    int64_t totalCost = static_cast<int64_t>(SimulationConstants::road_placement_cost_per_tile) +
                        static_cast<int64_t>(earthworksCostOverride);
    undoAction.costPaid = totalCost;

    // Deduct cost
    m_treasury -= totalCost;

    // Previous tile: if it was a road, adjust count first
    bool wasRoad = (it != m_tiles.end() && it->second.isRoad);

    // Update tile
    TileData& tile = m_tiles[key];
    tile.isRoad      = true;
    tile.isZoned     = false;
    tile.population  = 0.0f;
    tile.desirability = static_cast<float>(SimulationConstants::desirability_base_value);

    // Update road tile count
    if (!wasRoad) {
        m_roadTileCount++;
    }

    // Phase 10: traffic signal maintenance.
    // If the new road tile is adjacent to 2+ existing road tiles (in any cardinal direction),
    // it qualifies as an intersection and receives a TrafficSignal entry.
    // Also re-check each neighbor — a neighbor that now has 2+ road adjacencies may need
    // a new signal added (if it didn't qualify before this road was placed).
    // Staggered initial phaseTimer: use tileX ^ tileZ hashed to [0, phaseSeconds) to
    // prevent all signals from firing simultaneously on the first frame.
    // Guard: only add signals for newly-placed road tiles (wasRoad == false).
    if (!wasRoad) {
        const int neighbors[4][2] = {{tileX-1,tileZ},{tileX+1,tileZ},{tileX,tileZ-1},{tileX,tileZ+1}};

        // Helper lambda: count road-adjacent tiles for a given position (4 cardinal dirs).
        auto countRoadNeighbors = [&](int cx, int cz) -> int {
            const int nbs[4][2] = {{cx-1,cz},{cx+1,cz},{cx,cz-1},{cx,cz+1}};
            int count = 0;
            for (auto& nb2 : nbs) {
                int64_t nkey = tileKey(nb2[0], nb2[1]);
                auto nit = m_tiles.find(nkey);
                if (nit != m_tiles.end() && nit->second.isRoad) ++count;
            }
            return count;
        };

        // Helper lambda: check if signal already exists for a position.
        auto hasSignal = [&](int cx, int cz) -> bool {
            for (const TrafficSignal& sig : m_trafficSignals) {
                if (sig.tileX == cx && sig.tileZ == cz) return true;
            }
            return false;
        };

        // Helper lambda: generate staggered initial phase offset from tile coordinates.
        auto phaseOffset = [&](int cx, int cz) -> float {
            unsigned int seed = static_cast<unsigned int>(cx * 73856093 ^ cz * 19349663);
            return (static_cast<float>(seed & 0xFFFFu) / 65535.0f) *
                   SimulationConstants::traffic_signal_phase_seconds;
        };

        // Check the newly placed tile.
        if (countRoadNeighbors(tileX, tileZ) >= 2 && !hasSignal(tileX, tileZ)) {
            TrafficSignal sig;
            sig.tileX      = tileX;
            sig.tileZ      = tileZ;
            sig.phaseTimer = phaseOffset(tileX, tileZ);
            m_trafficSignals.push_back(sig);
        }

        // Re-check each cardinal neighbor — they may now qualify as intersections.
        for (auto& nb : neighbors) {
            int64_t nkey = tileKey(nb[0], nb[1]);
            auto nit = m_tiles.find(nkey);
            if (nit == m_tiles.end() || !nit->second.isRoad) continue;
            if (countRoadNeighbors(nb[0], nb[1]) >= 2 && !hasSignal(nb[0], nb[1])) {
                TrafficSignal sig;
                sig.tileX      = nb[0];
                sig.tileZ      = nb[1];
                sig.phaseTimer = phaseOffset(nb[0], nb[1]);
                m_trafficSignals.push_back(sig);
            }
        }
    }

    // Play audio
    if (earthworksCostOverride > 0) {
        if (m_audio) {
            m_audio->playPositionalSound(SFX_EARTHWORKS,
                vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
                SoundPriority::NORMAL, 1.0f);
        }
    }
    if (m_audio) {
        m_audio->playPositionalSound(SFX_ROAD_BUILD,
            vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
            SoundPriority::NORMAL, 1.0f);
    }

    // Phase 10: spawn road tile mesh.
    if (m_renderer) {
        m_renderer->placeRoadMesh(tileX, tileZ);
    }

    // Record undo
    recordUndoAction(undoAction);
}

// ---------------------------------------------------------------------------
// demolishTile
// ---------------------------------------------------------------------------

void CitySimulation::demolishTile(int tileX, int tileZ) {
    int64_t key = tileKey(tileX, tileZ);
    auto it = m_tiles.find(key);

    // Service buildings are registered in m_serviceBuildings but do NOT create
    // a TileData entry in m_tiles.  Check for a service building at this tile
    // BEFORE the m_tiles guard so demolishing a service-building tile is not
    // silently rejected.
    bool hadServiceBuilding = false;
    for (const ServiceBuilding& sb : m_serviceBuildings) {
        if (sb.x == tileX && sb.z == tileZ) { hadServiceBuilding = true; break; }
    }

    // Nothing to demolish if no tile data AND no service building.
    if (it == m_tiles.end() && !hadServiceBuilding) return;

    // Record previous state for undo.
    UndoAction undoAction;
    undoAction.actionType    = UndoAction::Type::Demolish;
    undoAction.tileX         = tileX;
    undoAction.tileZ         = tileZ;
    undoAction.previousState = (it != m_tiles.end()) ? it->second : TileData{};
    undoAction.costPaid      = 0;  // demolish is free

    bool wasRoad = (it != m_tiles.end()) && it->second.isRoad;

    if (it != m_tiles.end()) {
        // Clear tile
        TileData& tile = it->second;
        tile.isZoned    = false;
        tile.isRoad     = false;
        tile.population = 0.0f;
    }

    // Update road tile count
    if (wasRoad) {
        m_roadTileCount--;
    }

    // Phase 10: remove traffic signal for the demolished tile (if any).
    if (wasRoad) {
        m_trafficSignals.erase(
            std::remove_if(m_trafficSignals.begin(), m_trafficSignals.end(),
                [tileX, tileZ](const TrafficSignal& sig) {
                    return sig.tileX == tileX && sig.tileZ == tileZ;
                }),
            m_trafficSignals.end());
    }

    // Play audio
    if (m_audio) {
        m_audio->playPositionalSound(SFX_BUILD_DEMOLISH,
            vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
            SoundPriority::NORMAL, 1.0f);
    }

    // Phase 10: remove mesh for the demolished tile.
    // Priority: road → service building → zone.
    // hadServiceBuilding was computed before any mutations above.
    if (m_renderer) {
        if (wasRoad) {
            m_renderer->removeRoadMesh(tileX, tileZ);
        } else if (hadServiceBuilding) {
            m_renderer->removeServiceBuildingMesh(tileX, tileZ);
        } else if (undoAction.previousState.isZoned) {
            m_renderer->removeBuildingMesh(tileX, tileZ);
        }
    }

    // Remove any service building registered at this tile.
    m_serviceBuildings.erase(
        std::remove_if(m_serviceBuildings.begin(), m_serviceBuildings.end(),
            [tileX, tileZ](const ServiceBuilding& sb) {
                return sb.x == tileX && sb.z == tileZ;
            }),
        m_serviceBuildings.end());

    // Record undo
    recordUndoAction(undoAction);
}

// ---------------------------------------------------------------------------
// undoLastAction
// ---------------------------------------------------------------------------
// placeServiceBuilding — Phase 9b stub.
// Full implementation (cost deduction, tile occupation, undo entry, audio
// callback) is delivered in Phase 9b.
// ---------------------------------------------------------------------------

void CitySimulation::placeServiceBuilding(int tileX, int tileZ,
                                          ServiceBuildingType type,
                                          int earthworksCostOverride) {
    // One-building-per-tile invariant: no-op if the tile is already occupied.
    for (const ServiceBuilding& sb : m_serviceBuildings) {
        if (sb.x == tileX && sb.z == tileZ) {
            return;  // Already occupied — no cost, no undo entry.
        }
    }

    // Map ServiceBuildingType (public) to ServiceType (private).
    // ServiceBuildingType: PowerPlant=0, WaterTower=1, FireStation=2, PoliceStation=3.
    // ServiceType:         FireStation=0, PoliceStation=1, WaterTower=2, PowerPlant=3.
    ServiceType internalType;
    int placementCost = 0;
    switch (type) {
        case ServiceBuildingType::PowerPlant:
            internalType  = ServiceType::PowerPlant;
            placementCost = SimulationConstants::service_placement_cost_power_plant;
            break;
        case ServiceBuildingType::WaterTower:
            internalType  = ServiceType::WaterTower;
            placementCost = SimulationConstants::service_placement_cost_water_tower;
            break;
        case ServiceBuildingType::FireStation:
            internalType  = ServiceType::FireStation;
            placementCost = SimulationConstants::service_placement_cost_fire_station;
            break;
        case ServiceBuildingType::PoliceStation:
            internalType  = ServiceType::PoliceStation;
            placementCost = SimulationConstants::service_placement_cost_police_station;
            break;
        default:
            return;
    }

    // Record previous tile state for undo.
    UndoAction undoAction;
    undoAction.actionType = UndoAction::Type::PlaceZone;  // re-uses PlaceZone type for undo record
    undoAction.tileX = tileX;
    undoAction.tileZ = tileZ;
    int64_t key = tileKey(tileX, tileZ);
    auto it = m_tiles.find(key);
    undoAction.previousState = (it != m_tiles.end()) ? it->second : TileData{};

    int64_t totalCost = static_cast<int64_t>(placementCost)
                        + static_cast<int64_t>(earthworksCostOverride);
    undoAction.costPaid = totalCost;

    // Deduct cost from treasury.
    m_treasury -= totalCost;

    // Register the service building.
    ServiceBuilding sb;
    sb.x       = tileX;
    sb.z       = tileZ;
    sb.type    = internalType;
    sb.degraded = false;
    m_serviceBuildings.push_back(sb);

    // Play audio.
    if (earthworksCostOverride > 0) {
        if (m_audio) {
            m_audio->playPositionalSound(SFX_EARTHWORKS,
                vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
                SoundPriority::NORMAL, 1.0f);
        }
    }
    if (m_audio) {
        m_audio->playPositionalSound(SFX_BUILD_PLACE,
            vec3{static_cast<float>(tileX), 0.0f, static_cast<float>(tileZ)},
            SoundPriority::NORMAL, 1.0f);
    }

    // Phase 10: spawn service building mesh.
    // type is the public ServiceBuildingType — matches IRenderer::placeServiceBuildingMesh().
    if (m_renderer) {
        m_renderer->placeServiceBuildingMesh(tileX, tileZ, type);
    }

    // Record undo entry.
    recordUndoAction(undoAction);
}

// ---------------------------------------------------------------------------

void CitySimulation::undoLastAction() {
    if (!m_pendingUndo.has_value()) return;
    if (m_modalOpen) return;

    const UndoAction& action = m_pendingUndo.value();

    int64_t key = tileKey(action.tileX, action.tileZ);

    // Get current tile state before restoring (for road count adjustment)
    auto it = m_tiles.find(key);
    bool currentlyRoad  = (it != m_tiles.end() && it->second.isRoad);

    // Restore tile to previous state
    m_tiles[key] = action.previousState;

    // Recompute road tile count:
    bool prevWasRoad = action.previousState.isRoad;
    if (currentlyRoad && !prevWasRoad) {
        m_roadTileCount--;
    } else if (!currentlyRoad && prevWasRoad) {
        m_roadTileCount++;
    }

    // Refund cost: clamp treasury to starting funds (cannot refund more than starting capital)
    int64_t startingFunds;
    switch (m_difficulty) {
        case Difficulty::Easy:   startingFunds = SimulationConstants::starting_funds_easy;   break;
        case Difficulty::Normal: startingFunds = SimulationConstants::starting_funds_normal; break;
        case Difficulty::Hard:   startingFunds = SimulationConstants::starting_funds_hard;   break;
        default:                 startingFunds = SimulationConstants::starting_funds_normal; break;
    }

    m_treasury = std::min(m_treasury + action.costPaid, startingFunds);

    // Clear undo
    m_pendingUndo.reset();
    m_undoExpiryTickTarget = -1;
}

// ---------------------------------------------------------------------------
// queryTile
// ---------------------------------------------------------------------------

QueryResult CitySimulation::queryTile(int tileX, int tileZ) const {
    QueryResult result;
    result.tileX  = tileX;
    result.tileZ  = tileZ;
    result.isZoned = false;

    const TileData* tile = findTile(tileX, tileZ);
    if (!tile || !tile->isZoned) {
        return result;
    }

    result.isZoned     = true;
    result.zoneType    = tile->zone;
    result.densityTier = tile->density;
    result.population  = static_cast<int>(tile->population);
    result.desirability = tile->desirability;

    // Per-tile effective demand factor
    float effDemand = effectiveDemandForTile(*tile);
    // demandPressurePct = (1.0f - effective_demand_factor) * 100.0f
    result.demandPressurePct = (1.0f - effDemand) * 100.0f;

    // Service coverage
    // -1.0f = N/A (no buildings of that type exist)
    // 0.0f  = covered by 0 buildings
    // 1.0f  = covered
    bool hasFireStation = false, hasPolice = false, hasWater = false, hasPower = false;
    for (const ServiceBuilding& sb : m_serviceBuildings) {
        switch (sb.type) {
            case ServiceType::FireStation:   hasFireStation = true; break;
            case ServiceType::PoliceStation: hasPolice      = true; break;
            case ServiceType::WaterTower:    hasWater       = true; break;
            case ServiceType::PowerPlant:    hasPower       = true; break;
        }
    }

    result.coverage.fire   = hasFireStation ? computeRadialCoverage(tileX, tileZ, ServiceType::FireStation)   : -1.0f;
    result.coverage.police = hasPolice      ? computeRadialCoverage(tileX, tileZ, ServiceType::PoliceStation) : -1.0f;
    result.coverage.water  = hasWater       ? computeRadialCoverage(tileX, tileZ, ServiceType::WaterTower)    : -1.0f;
    result.coverage.power  = hasPower       ? computePowerCoverage(tileX, tileZ)                              : -1.0f;

    return result;
}

// ---------------------------------------------------------------------------
// Economy / treasury accessors
// ---------------------------------------------------------------------------

float CitySimulation::getTreasuryBalance() const {
    return static_cast<float>(m_treasury);
}

float CitySimulation::getCurrentMonthlyRevenue() const {
    return m_currentMonthlyRevenue;
}

float CitySimulation::getOutstandingDebt() const {
    int64_t total = 0;
    for (const LoanEntry& loan : m_loans) {
        total += loan.remainingPrincipal;
    }
    return static_cast<float>(total);
}

float CitySimulation::estimateMonthlyUpkeep() const {
    if ((m_clock->nowSeconds() - m_constructionTimeSeconds) <
        SimulationConstants::grace_period_real_seconds) {
        return 0.0f;
    }
    return static_cast<float>(computeServiceUpkeepCost() + computeRoadMaintenanceCost());
}

float CitySimulation::getNextUnlockThreshold(Difficulty d) const {
    float scale;
    switch (d) {
        case Difficulty::Easy:   scale = SimulationConstants::density_unlock_scale_easy;   break;
        case Difficulty::Normal: scale = SimulationConstants::density_unlock_scale_normal; break;
        case Difficulty::Hard:   scale = SimulationConstants::density_unlock_scale_hard;   break;
        default:                 scale = SimulationConstants::density_unlock_scale_normal; break;
    }

    // Map tier index to threshold constant
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

        // Special prerequisite: High-R (tier 3) only shown if Med-I (tier 2) is unlocked
        if (i == 3 && !m_densityUnlockState.unlock_flags[2]) continue;

        return getThreshold(i) * scale;
    }

    // All tiers unlocked
    return SimulationConstants::kNoUnlockThreshold;
}

// ---------------------------------------------------------------------------
// City rating accessor
// ---------------------------------------------------------------------------

CityRatingTier CitySimulation::getCityRating() const {
    return m_cityRating;
}

// ---------------------------------------------------------------------------
// Demand accessors
// ---------------------------------------------------------------------------

float CitySimulation::getDemandPressurePct(ZoneType zone) const {
    return m_demandPressurePct[static_cast<int>(zone)];
}

float CitySimulation::getTrafficDemandFactor(ZoneType zone) const {
    switch (zone) {
        case ZoneType::Residential: return m_trafficDemandFactorR;
        case ZoneType::Commercial:  return m_trafficDemandFactorC;
        case ZoneType::Industrial:  return m_trafficDemandFactorI;
    }
    return SimulationConstants::null_path_demand_default;
}

// ---------------------------------------------------------------------------
// Population accessor
// ---------------------------------------------------------------------------

int CitySimulation::getTotalPopulation() const {
    return m_totalPopulation;
}

// ---------------------------------------------------------------------------
// Undo state accessors
// ---------------------------------------------------------------------------

bool CitySimulation::hasUndoPendingAction() const {
    return m_pendingUndo.has_value();
}

double CitySimulation::getUndoExpiryTimeSeconds() const {
    if (!m_pendingUndo.has_value()) return 0.0;
    return m_undoExpiryWallSeconds;
}

// ---------------------------------------------------------------------------
// Game-over accessor
// ---------------------------------------------------------------------------

int CitySimulation::getConsecutiveDeficitMonths() const {
    return m_consecutiveDeficitMonths;
}

// ---------------------------------------------------------------------------
// Density unlock state accessor
// ---------------------------------------------------------------------------

DensityUnlockState CitySimulation::getDensityUnlockState() const {
    return m_densityUnlockState;
}

// ---------------------------------------------------------------------------
// Simulation time accessor
// ---------------------------------------------------------------------------

SimulationTime CitySimulation::getSimulationTime() const {
    return SimulationTime{m_year, m_month};
}

// ---------------------------------------------------------------------------
// Notification queue
// ---------------------------------------------------------------------------

bool CitySimulation::pollPendingNotification(SimulationNotification& out) {
    if (m_notifications.empty()) return false;
    out = m_notifications.front();
    m_notifications.pop();
    return true;
}

// ---------------------------------------------------------------------------
// Tax rate accessors
// ---------------------------------------------------------------------------

void CitySimulation::setTaxRate(ZoneType zone, float rate) {
    // Clamp to [0.01, 0.25]
    rate = std::min(0.25f, std::max(0.01f, rate));
    m_taxRates[static_cast<int>(zone)] = rate;
}

float CitySimulation::getTaxRate(ZoneType zone) const {
    return m_taxRates[static_cast<int>(zone)];
}

// ---------------------------------------------------------------------------
// Budget line-item accessors
// ---------------------------------------------------------------------------

float CitySimulation::getTaxRevenue(ZoneType zone) const {
    return m_lastMonthTaxRevenue[static_cast<int>(zone)];
}

float CitySimulation::getWagesCost() const {
    return m_lastMonthWagesCost;
}

float CitySimulation::getRoadMaintenanceCost() const {
    return m_lastMonthRoadMaintenanceCost;
}

float CitySimulation::getServiceUpkeepCost() const {
    return m_lastMonthServiceUpkeepCost;
}

float CitySimulation::getUtilityFeeRevenue() const {
    return m_lastMonthUtilityFeeRevenue;
}

// ---------------------------------------------------------------------------
// Bond use count accessor
// ---------------------------------------------------------------------------

int CitySimulation::getOutstandingBondUses() const {
    return m_outstandingBondUses;
}

// ---------------------------------------------------------------------------
// Time of day accessor
// ---------------------------------------------------------------------------

TimeOfDay CitySimulation::getTimeOfDay() const {
    return m_timeOfDay;
}

// ---------------------------------------------------------------------------
// Test-only API: inject a service building directly (not via ICitySimulation).
// serviceTypeInt: 0=FireStation, 1=PoliceStation, 2=WaterTower, 3=PowerPlant.
// ---------------------------------------------------------------------------
void CitySimulation::addServiceBuilding(int x, int z, int serviceTypeInt) {
    ServiceBuilding sb;
    sb.x = x;
    sb.z = z;
    sb.type = static_cast<ServiceType>(serviceTypeInt);
    sb.degraded = false;
    m_serviceBuildings.push_back(sb);
}

// ---------------------------------------------------------------------------
// Test-only API: simulate a blocking modal dialog being active.
// When open, undoLastAction() is a no-op per architecture/game-design/undo-system.md.
// ---------------------------------------------------------------------------
void CitySimulation::setModalOpen(bool open) {
    m_modalOpen = open;
}
