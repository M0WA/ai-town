// src/ui/HUD.cpp
//
// HUD — the in-gameplay heads-up display.
// Creates all UI elements in the constructor using IUIBackend.
// Updates element text/visibility/alpha in draw() and update() each frame.
// All coordinates are in virtual 1920x1080 space.

#include "src/ui/HUD.h"
#include "src/ui/BudgetDetailPanel.h"
#include "src/ui/hud_sprite_ids.h"
#include "src/interfaces/IAudioSystem.h"

#include <cmath>
#include <string>
#include <cstdio>

// ---------------------------------------------------------------------------
// Helper: format a float as a dollar string like "$1,234"
// ---------------------------------------------------------------------------
static std::string formatDollar(float value) {
    char buf[64];
    if (value < 0.0f) {
        std::snprintf(buf, sizeof(buf), "-$%.0f", -value);
    } else {
        std::snprintf(buf, sizeof(buf), "$%.0f", value);
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Helper: CityRatingTier to display name
// ---------------------------------------------------------------------------
static const char* ratingName(CityRatingTier tier) {
    switch (tier) {
        case CityRatingTier::Village:    return "Village";
        case CityRatingTier::Town:       return "Town";
        case CityRatingTier::City:       return "City";
        case CityRatingTier::Metropolis: return "Metropolis";
        case CityRatingTier::Megalopolis:return "Megalopolis";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
HUD::HUD(IUIBackend* backend, IAudioSystem* audio, ICitySimulation* sim, IClock* clock)
    : m_backend(backend)
    , m_audio(audio)
    , m_sim(sim)
    , m_clock(clock)
    , m_budgetDetail(nullptr)
    , m_visible(false)
    , m_gameStartTime(clock ? clock->nowSeconds() : 0.0)
    , m_gracePeriodExpired(false)
    , m_graceFadeAlpha(1.0f)
    , m_budgetFlashTimer(0.0f)
{
    if (!m_backend) return;

    m_budgetDetail = new BudgetDetailPanel(m_backend, m_sim);

    // --- Resource / budget bar (top, y:0-56) ---
    m_treasuryLabel   = m_backend->addStaticText("$0", 8, 8, 200, 48);
    m_backend->setElementMonoFont(m_treasuryLabel);              // numeric: monospace required (phase-10)
    m_backend->setElementTextColor(m_treasuryLabel, 240, 180, 41); // amber #F0B429 (phase-10c)
    m_debtLabel       = m_backend->addStaticText("", 216, 8, 200, 48);
    m_backend->setElementMonoFont(m_debtLabel);                  // numeric: monospace required (phase-10)
    m_backend->setElementTextColor(m_debtLabel, 240, 180, 41);     // amber #F0B429 (phase-10c)
    m_ratingLabel     = m_backend->addStaticText("Village", 424, 8, 160, 48);
    m_populationLabel = m_backend->addStaticText("Pop: 0", 592, 8, 160, 48);
    m_backend->setElementMonoFont(m_populationLabel);            // numeric: monospace required (phase-10)
    m_backend->setElementTextColor(m_populationLabel, 240, 180, 41); // amber #F0B429 (phase-10c)
    // w=360: "Year 10, Month 12" (18 chars × ~11px mono) needs ~198px physical;
    // at 1280×720 physical w = (360×1280)/1920 = 240px → fits comfortably.
    m_dateLabel       = m_backend->addStaticText("Year 1, Month 1", 760, 8, 360, 48);
    m_backend->setElementMonoFont(m_dateLabel);                  // numeric: monospace required (phase-10)
    m_backend->setElementTextColor(m_dateLabel, 240, 180, 41);      // amber #F0B429 (phase-10c)

    // --- Primary toolbar (left, x:8-72, y:64-600), 5 tool buttons ---
    int toolY = 64;
    constexpr int kToolBtnSize = 48;
    constexpr int kToolPad = 8;
    constexpr int kToolX = 8;

    m_btnZone      = m_backend->addButton("",  kToolX, toolY, kToolBtnSize + 8, kToolBtnSize);
    m_backend->setElementImage(m_btnZone,      kSpriteToolZoneActive);
    toolY += kToolBtnSize + kToolPad;
    m_btnRoad      = m_backend->addButton("",  kToolX, toolY, kToolBtnSize + 8, kToolBtnSize);
    m_backend->setElementImage(m_btnRoad,      kSpriteToolRoadActive);
    toolY += kToolBtnSize + kToolPad;
    m_btnUtilities = m_backend->addButton("",  kToolX, toolY, kToolBtnSize + 8, kToolBtnSize);
    m_backend->setElementImage(m_btnUtilities, kSpriteToolUtilitiesActive);
    toolY += kToolBtnSize + kToolPad;
    m_btnDemolish  = m_backend->addButton("",  kToolX, toolY, kToolBtnSize + 8, kToolBtnSize);
    m_backend->setElementImage(m_btnDemolish,  kSpriteToolDemolishActive);
    toolY += kToolBtnSize + kToolPad;
    m_btnQuery     = m_backend->addButton("",  kToolX, toolY, kToolBtnSize + 8, kToolBtnSize);
    m_backend->setElementImage(m_btnQuery,     kSpriteToolQueryActive);

    // --- Undo button (x:8-72, y:608-656) ---
    m_btnUndo = m_backend->addButton("Undo", 8, 608, 64, 48);

    // --- Grace period indicator (x:80-1912, y:60-92) ---
    // Starts at x=80 (after the 64px-wide toolbar column) so it does not
    // visually overlap the tool buttons on the left side of the screen.
    m_gracePeriodLabel = m_backend->addStaticText("Cost waiver: 120s remaining", 80, 60, 1832, 32);

    // --- Demand pressure bars (x:8-72, y:664-748) ---
    // 3 bars each ~20px wide in the 64px toolbar width.
    // Label h=28: at 720p → 18.7px physical (vs 22px font) — less clipped than h=16.
    // Bars start at y=664+28=692, h=56, end at y=748; active tool below at y=752.
    m_demandLabelR = m_backend->addStaticText("R",  8,  664, 20, 28);
    m_demandLabelC = m_backend->addStaticText("C",  30, 664, 20, 28);
    m_demandLabelI = m_backend->addStaticText("I",  52, 664, 20, 28);
    m_demandBarR   = m_backend->addStaticText("",   8,  692, 20, 56);
    m_demandBarC   = m_backend->addStaticText("",   30, 692, 20, 56);
    m_demandBarI   = m_backend->addStaticText("",   52, 692, 20, 56);

    // --- Active tool indicator (x:8-72, y:752-784) ---
    m_activeToolLabel = m_backend->addStaticText("No tool", 8, 752, 64, 32);

    // --- Notification bell (x:1820-1868, y:8-56) ---
    m_notifBell = m_backend->addButton("", 1820, 8, 48, 48);
    m_backend->setElementImage(m_notifBell, kSpriteNotificationBell);

    // --- Unsaved changes dot (x:1796-1812, y:8-24) ---
    m_unsavedDotHandle = m_backend->addStaticText("*", 1796, 8, 16, 16);
    m_backend->setElementVisible(m_unsavedDotHandle, false);

    // --- Speed selector (top-right, x:1600-1796, y:8-56) ---
    m_btnPause   = m_backend->addButton("||",  1600, 8, 48, 48);
    m_btnSpeed1  = m_backend->addButton("1x",  1652, 8, 48, 48);
    m_btnSpeed3  = m_backend->addButton("3x",  1704, 8, 48, 48);
    m_btnSpeed10 = m_backend->addButton("10x", 1756, 8, 40, 48);

    // --- Tax rate pending indicator (shown temporarily after tax changes) ---
    m_taxPendingLabel = m_backend->addStaticText("Tax rates updating next budget cycle", 968, 60, 400, 24);
    m_backend->setElementVisible(m_taxPendingLabel, false);

    // Initially hidden until show() is called
    hide();
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
HUD::~HUD() {
    delete m_budgetDetail;
    m_budgetDetail = nullptr;
}

// ---------------------------------------------------------------------------
// show / hide
// ---------------------------------------------------------------------------
void HUD::show() {
    m_visible = true;
    if (!m_backend) return;

    m_backend->setElementVisible(m_treasuryLabel,   true);
    m_backend->setElementVisible(m_debtLabel,       true);
    m_backend->setElementVisible(m_ratingLabel,     true);
    m_backend->setElementVisible(m_populationLabel, true);
    m_backend->setElementVisible(m_dateLabel,       true);

    m_backend->setElementVisible(m_btnZone,      true);
    m_backend->setElementVisible(m_btnRoad,      true);
    m_backend->setElementVisible(m_btnUtilities, true);
    m_backend->setElementVisible(m_btnDemolish,  true);
    m_backend->setElementVisible(m_btnQuery,     true);

    m_backend->setElementVisible(m_btnUndo,         true);
    m_backend->setElementVisible(m_gracePeriodLabel, !m_gracePeriodExpired);

    m_backend->setElementVisible(m_demandBarR,   true);
    m_backend->setElementVisible(m_demandBarC,   true);
    m_backend->setElementVisible(m_demandBarI,   true);
    m_backend->setElementVisible(m_demandLabelR, true);
    m_backend->setElementVisible(m_demandLabelC, true);
    m_backend->setElementVisible(m_demandLabelI, true);

    m_backend->setElementVisible(m_activeToolLabel, true);
    m_backend->setElementVisible(m_notifBell,       true);

    m_backend->setElementVisible(m_btnPause,   true);
    m_backend->setElementVisible(m_btnSpeed1,  true);
    m_backend->setElementVisible(m_btnSpeed3,  true);
    m_backend->setElementVisible(m_btnSpeed10, true);
}

void HUD::hide() {
    m_visible = false;
    if (!m_backend) return;

    m_backend->setElementVisible(m_treasuryLabel,    false);
    m_backend->setElementVisible(m_debtLabel,        false);
    m_backend->setElementVisible(m_ratingLabel,      false);
    m_backend->setElementVisible(m_populationLabel,  false);
    m_backend->setElementVisible(m_dateLabel,        false);

    m_backend->setElementVisible(m_btnZone,      false);
    m_backend->setElementVisible(m_btnRoad,      false);
    m_backend->setElementVisible(m_btnUtilities, false);
    m_backend->setElementVisible(m_btnDemolish,  false);
    m_backend->setElementVisible(m_btnQuery,     false);

    m_backend->setElementVisible(m_btnUndo,          false);
    m_backend->setElementVisible(m_gracePeriodLabel, false);

    m_backend->setElementVisible(m_demandBarR,   false);
    m_backend->setElementVisible(m_demandBarC,   false);
    m_backend->setElementVisible(m_demandBarI,   false);
    m_backend->setElementVisible(m_demandLabelR, false);
    m_backend->setElementVisible(m_demandLabelC, false);
    m_backend->setElementVisible(m_demandLabelI, false);

    m_backend->setElementVisible(m_activeToolLabel, false);
    m_backend->setElementVisible(m_notifBell,       false);
    m_backend->setElementVisible(m_unsavedDotHandle, false);

    m_backend->setElementVisible(m_btnPause,   false);
    m_backend->setElementVisible(m_btnSpeed1,  false);
    m_backend->setElementVisible(m_btnSpeed3,  false);
    m_backend->setElementVisible(m_btnSpeed10, false);

    m_backend->setElementVisible(m_taxPendingLabel, false);
}

// ---------------------------------------------------------------------------
// draw — refresh all HUD element text/state from simulation each frame
// ---------------------------------------------------------------------------
void HUD::draw() {
    if (!m_visible || !m_backend || !m_sim) return;

    // Treasury
    float treasury = m_sim->getTreasuryBalance();
    m_backend->setElementText(m_treasuryLabel, formatDollar(treasury));

    // Debt indicator (hidden when zero)
    float debt = m_sim->getOutstandingDebt();
    if (debt > 0.0f) {
        m_backend->setElementText(m_debtLabel, "Debt: " + formatDollar(debt));
        m_backend->setElementVisible(m_debtLabel, true);
    } else {
        m_backend->setElementVisible(m_debtLabel, false);
    }

    // City rating
    CityRatingTier rating = m_sim->getCityRating();
    m_backend->setElementText(m_ratingLabel, ratingName(rating));

    // Population
    int pop = m_sim->getTotalPopulation();
    m_backend->setElementText(m_populationLabel, "Pop: " + std::to_string(pop));

    // Simulation date
    SimulationTime simTime = m_sim->getSimulationTime();
    std::string dateStr = "Year " + std::to_string(simTime.year)
                        + ", Month " + std::to_string(simTime.month);
    m_backend->setElementText(m_dateLabel, dateStr);

    // Undo button — grayed when no action pending; countdown when pending
    bool hasUndo = m_sim->hasUndoPendingAction();
    m_backend->setElementEnabled(m_btnUndo, hasUndo);
    if (hasUndo && m_clock) {
        double expiryTime = m_sim->getUndoExpiryTimeSeconds();
        double now = m_clock->nowSeconds();
        double remaining = expiryTime - now;
        if (remaining < 0.0) remaining = 0.0;
        char undoBuf[64];
        std::snprintf(undoBuf, sizeof(undoBuf), "Undo (%ds)", static_cast<int>(remaining));
        m_backend->setElementText(m_btnUndo, undoBuf);
    } else {
        m_backend->setElementText(m_btnUndo, "Undo");
    }

    // Demand pressure bars (R/C/I) — getDemandPressurePct returns [0.0, 1.0]
    float demandR = m_sim->getDemandPressurePct(ZoneType::Residential) * 100.0f;
    float demandC = m_sim->getDemandPressurePct(ZoneType::Commercial)  * 100.0f;
    float demandI = m_sim->getDemandPressurePct(ZoneType::Industrial)  * 100.0f;

    char demBuf[32];
    std::snprintf(demBuf, sizeof(demBuf), "%.0f%%", demandR);
    m_backend->setElementText(m_demandBarR, demBuf);
    std::snprintf(demBuf, sizeof(demBuf), "%.0f%%", demandC);
    m_backend->setElementText(m_demandBarC, demBuf);
    std::snprintf(demBuf, sizeof(demBuf), "%.0f%%", demandI);
    m_backend->setElementText(m_demandBarI, demBuf);

    // Speed selector — poll sim speed and highlight active button
    SpeedMultiplier speed = m_sim->getSpeedMultiplier();
    m_backend->setElementText(m_btnPause,   speed == SpeedMultiplier::Paused ? "[||]" : "||");
    m_backend->setElementText(m_btnSpeed1,  speed == SpeedMultiplier::x1     ? "[1x]" : "1x");
    m_backend->setElementText(m_btnSpeed3,  speed == SpeedMultiplier::x3     ? "[3x]" : "3x");
    m_backend->setElementText(m_btnSpeed10, speed == SpeedMultiplier::x10    ? "[10x]": "10x");

    // Budget detail sub-panel draw
    if (m_budgetDetail) {
        m_budgetDetail->draw();
    }
}

// ---------------------------------------------------------------------------
// update — per-frame state update (undo countdown, grace period, budget flash)
// ---------------------------------------------------------------------------
void HUD::update(float dt) {
    if (!m_visible || !m_backend) return;

    // --- Grace period indicator ---
    // 120 real-second wall-clock grace period from game start
    if (!m_gracePeriodExpired && m_clock) {
        double elapsed = m_clock->nowSeconds() - m_gameStartTime;
        double remaining = 120.0 - elapsed;

        if (remaining <= 0.0) {
            // Begin fade-out over 0.5s
            m_graceFadeAlpha -= dt / 0.5f;
            if (m_graceFadeAlpha <= 0.0f) {
                m_graceFadeAlpha = 0.0f;
                m_gracePeriodExpired = true;
                m_backend->setElementVisible(m_gracePeriodLabel, false);
            } else {
                m_backend->setElementAlpha(m_gracePeriodLabel, m_graceFadeAlpha);
            }
        } else {
            int secs = static_cast<int>(remaining);
            std::string graceText = "Cost waiver: " + std::to_string(secs) + "s remaining";
            m_backend->setElementText(m_gracePeriodLabel, graceText);

            // Amber warning when < 20s remaining
            // (represented via alpha hint — full color control is post-V1)
            if (remaining < 20.0) {
                m_backend->setElementAlpha(m_gracePeriodLabel, 0.8f);
            } else {
                m_backend->setElementAlpha(m_gracePeriodLabel, 1.0f);
            }
        }
    }

    // --- Red flashing budget indicator ---
    // When consecutive deficit months >= 2, pulse treasury alpha 0.3-1.0 at ~1 Hz
    if (m_sim) {
        int deficitMonths = m_sim->getConsecutiveDeficitMonths();
        if (deficitMonths >= 2) {
            m_budgetFlashTimer += dt;
            // Sine wave oscillation: period ~1s (2*pi rad/s)
            float pulse = 0.65f + 0.35f * std::sin(m_budgetFlashTimer * 6.2832f);
            m_backend->setElementAlpha(m_treasuryLabel, pulse);
        } else {
            m_budgetFlashTimer = 0.0f;
            m_backend->setElementAlpha(m_treasuryLabel, 1.0f);
        }
    }

    // Budget detail sub-panel update
    if (m_budgetDetail) {
        m_budgetDetail->update();
    }
}

// ---------------------------------------------------------------------------
// setUnsavedChanges
// ---------------------------------------------------------------------------
void HUD::setUnsavedChanges(bool unsaved) {
    if (m_backend) {
        m_backend->setElementVisible(m_unsavedDotHandle, unsaved);
    }
}

// ---------------------------------------------------------------------------
// setActiveToolLabel
// ---------------------------------------------------------------------------
void HUD::setActiveToolLabel(const std::string& text) {
    if (m_backend) {
        m_backend->setElementText(m_activeToolLabel, text);
    }
}
