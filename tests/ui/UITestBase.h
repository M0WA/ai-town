#pragma once
// tests/ui/UITestBase.h
//
// UITestFixtureBase — shared base fixture for UIManager tests.
//
// Provides:
//   - NiceMock<MockUIBackend>      backend_
//   - NiceMock<MockAudioSystem>    audio_
//   - NiceMock<MockCitySimulation> sim_
//   - ManualClock                  clock_   (starts at 0.0; advance via clock_.advance())
//   - std::unique_ptr<UIManager>   ui_
//
// Declaration order is significant: ui_ is declared LAST so it is destroyed FIRST
// (reverse declaration order), preventing use-after-free when UIManager's destructor
// calls back into the backend mock.
//
// SetUp(): configures standard ON_CALL stubs for backend_ and sim_ that cover the
//   calls UIManager panels make during construction and draw(). Constructs ui_.
//   Subclasses may call UITestFixtureBase::SetUp() then add further setup.
//
// TearDown(): resets ui_ explicitly before mock destructors run, ensuring UIManager
//   releases its references before NiceMock destructor validation.
//
// Usage:
//   class MyTest : public UITestFixtureBase {
//   protected:
//       void SetUp() override {
//           UITestFixtureBase::SetUp();
//           ui_->transitionToGameplay(GameMode::Sandbox);
//       }
//   };

#include "src/ui/UIManager.h"
#include "src/ui/ui_types.h"
#include "src/platform/input_event.h"
#include "tests/ui/MockUIBackend.h"
#include "tests/ui/MockCitySimulation.h"
#include "tests/simulation/MockAudioSystem.h"
#include "tests/simulation/ManualClock.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::_;

class UITestFixtureBase : public ::testing::Test {
protected:
    void SetUp() override {
        // Standard backend stubs used by all panels during construction.
        ON_CALL(backend_, addStaticText(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, addButton(_, _, _, _, _)).WillByDefault(
            [this](const std::string&, int, int, int, int) { return ++nextHandle_; });
        ON_CALL(backend_, getVirtualWidth()).WillByDefault(Return(1920));
        ON_CALL(backend_, getVirtualHeight()).WillByDefault(Return(1080));
        ON_CALL(backend_, isElementVisible(_)).WillByDefault(Return(true));
        ON_CALL(backend_, isElementEnabled(_)).WillByDefault(Return(true));
        ON_CALL(backend_, getElementRect(_)).WillByDefault(Return(UIRect{0, 0, 140, 40}));

        // Standard sim stubs covering calls made by HUD, FinancesPanel, and
        // notification polling during update().
        ON_CALL(sim_, isPaused()).WillByDefault(Return(false));
        ON_CALL(sim_, getConsecutiveDeficitMonths()).WillByDefault(Return(0));
        ON_CALL(sim_, pollPendingNotification(_)).WillByDefault(Return(false));
        ON_CALL(sim_, getSpeedMultiplier()).WillByDefault(Return(SpeedMultiplier::x1));
        ON_CALL(sim_, hasUndoPendingAction()).WillByDefault(Return(false));
        ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getOutstandingDebt()).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getCityRating()).WillByDefault(Return(CityRatingTier::Village));
        ON_CALL(sim_, getTotalPopulation()).WillByDefault(Return(0));
        ON_CALL(sim_, getSimulationTime()).WillByDefault(Return(SimulationTime{1, 1}));
        ON_CALL(sim_, getZoneDemandFactor(_)).WillByDefault(Return(0.0f));
        ON_CALL(sim_, getUndoExpiryTimeSeconds()).WillByDefault(Return(0.0));

        ui_ = std::make_unique<UIManager>(&backend_, &audio_, &sim_, &clock_);
    }

    void TearDown() override {
        // Explicit destruction: UIManager is torn down here while all mocks are still
        // live. Prevents dangling-pointer callbacks if any panel destructor calls back
        // into the backend mock.
        ui_.reset();
    }

    // MANDATORY member declaration order (C++ reverse-destruction):
    //   Mocks/clock declared first -> destroyed LAST (they outlive UIManager).
    //   ui_ declared last          -> destroyed FIRST (UIManager destructor runs before mocks).
    NiceMock<MockUIBackend>      backend_;   // 1st declared -> destroyed last
    NiceMock<MockAudioSystem>    audio_;     // 2nd
    NiceMock<MockCitySimulation> sim_;       // 3rd
    ManualClock                  clock_;     // 4th
    UIElementHandle              nextHandle_{0};  // incrementing handle allocator
    std::unique_ptr<UIManager>   ui_;        // 5th declared -> destroyed first
};
